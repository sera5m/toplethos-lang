#!/usr/bin/env python3
"""Desktop Toplethos runner — BASE language (not GTK).

LUT sin/cos (degrees, wrap). Includes, arrays, @memory_hard, native/py interop.
Errors include source line numbers.
"""
from __future__ import annotations

import ctypes
import importlib
import json
import math
import os
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

SIN_Q15 = [int(round(math.sin(2 * math.pi * i / 256) * 32767)) for i in range(256)]
HARD_DIR = Path(os.environ.get("TOPLETHOS_HARD", Path.cwd() / ".toplethos_hard"))

# Desktop defaults — watch firmware uses RSVM_MAX_STEPS / HEAP_BYTES / threads=1
DEFAULT_MAX_RUN = int(os.environ.get("TOPLETHOS_MAX_RUN", "1000000"))
DEFAULT_RAM_CELLS = int(os.environ.get("TOPLETHOS_RAM_CELLS", "1000000"))
DEFAULT_STORAGE = int(os.environ.get("TOPLETHOS_STORAGE", str(64 * 1024 * 1024)))
DEFAULT_THREADS = int(os.environ.get("TOPLETHOS_THREADS", str(os.cpu_count() or 4)))


def _phase_from_deg(deg: int) -> int:
    return (int(deg) * 11930465) & 0xFFFFFFFF


def _lut_lerp(table, ph: int) -> int:
    i = (ph >> 24) & 255
    j = (i + 1) & 255
    f = (ph >> 16) & 255
    a, b = table[i], table[j]
    return a + (((b - a) * f) >> 8)


def lut_sin_deg(deg) -> int:
    return _lut_lerp(SIN_Q15, _phase_from_deg(int(deg)))


def lut_cos_deg(deg) -> int:
    return _lut_lerp(SIN_Q15, _phase_from_deg(int(deg) + 90))


def lut_sin_amp(deg, amp) -> int:
    return (lut_sin_deg(deg) * int(amp)) >> 15


class RunError(Exception):
    def __init__(self, msg, line=0):
        self.line = line
        prefix = f"line {line}: " if line else ""
        super().__init__(prefix + msg)


def _strip_comments(src: str) -> str:
    out = []
    for line in src.splitlines():
        if "//" in line:
            line = line[: line.index("//")]
        out.append(line)
    return "\n".join(out)


def _line_at(src: str, pos: int) -> int:
    return src[: max(0, pos)].count("\n") + 1


class Arr:
    """RAM or @memory_hard array. Flat storage + dims. Cells may be dicts (structs)."""

    def __init__(self, dims, data=None, hard=None):
        self.dims = [int(d) for d in dims]
        n = 1
        for d in self.dims:
            n *= max(0, d)
        self.data = list(data) if data is not None else [0] * n
        if len(self.data) < n:
            self.data.extend([0] * (n - len(self.data)))
        self.hard = hard  # variable name if persisted

    def _off(self, idx):
        if len(idx) != len(self.dims):
            raise RunError("array rank %d, got %d indices" % (len(self.dims), len(idx)))
        off = 0
        for i, d in zip(idx, self.dims):
            i = int(i)
            if i < 0 or i >= d:
                raise RunError("index %s out of %s" % (idx, self.dims))
            off = off * d + i
        return off

    def get(self, idx):
        return self.data[self._off(idx)]

    def set(self, idx, v):
        self.data[self._off(idx)] = v
        if self.hard:
            _hard_save(self.hard, self)

    def __repr__(self):
        return "arr%s" % (self.dims,)


def _hard_path(name: str) -> Path:
    HARD_DIR.mkdir(parents=True, exist_ok=True)
    return HARD_DIR / (re.sub(r"[^\w.-]", "_", name) + ".json")


def _hard_save(name: str, arr: Arr):
    _hard_path(name).write_text(
        json.dumps({"dims": arr.dims, "data": arr.data}), encoding="utf-8"
    )


def _hard_load(name: str, dims) -> Arr:
    p = _hard_path(name)
    if p.exists():
        blob = json.loads(p.read_text(encoding="utf-8"))
        return Arr(blob.get("dims", dims), blob.get("data"), hard=name)
    a = Arr(dims, hard=name)
    _hard_save(name, a)
    return a


def _py_call(spec: str, *args):
    """py(\"math.sqrt\", 9) or py(\"os.path.join\", \"a\", \"b\")."""
    parts = spec.split(".")
    mod = importlib.import_module(".".join(parts[:-1]) if len(parts) > 1 else "builtins")
    fn = getattr(mod, parts[-1])
    return fn(*args)


def _native_call(spec: str, *args):
    """native(\"libc.so.6:strlen\", \"hi\") or native(\"strlen\", \"hi\") via libc."""
    if ":" in spec:
        libn, fn = spec.split(":", 1)
    elif "." in spec and not spec.startswith("lib"):
        return _py_call(spec, *args)
    else:
        libn, fn = "libc.so.6", spec
    try:
        lib = ctypes.CDLL(libn)
    except OSError:
        lib = ctypes.CDLL("libc.so.6")
    f = getattr(lib, fn)
    cargs = []
    for a in args:
        if isinstance(a, str):
            cargs.append(a.encode())
        elif isinstance(a, float):
            cargs.append(ctypes.c_double(a))
        else:
            cargs.append(int(a))
    return f(*cargs)


NSEQ_NIDS = {
    "nop": 0,
    "wave": 1,
    "wave_stop": 2,
    "wave_freq": 3,
    "wave_duty": 4,
    "wave_amp": 5,
    "sweep": 6,
    "adc": 7,
    "scope": 8,
    "delay": 9,
    "sleep": 9,
    "gpio_wr": 10,
    "gpio_rd": 11,
    "pin_mode": 12,
    "dig_wr": 13,
    "dig_rd": 14,
    "corz": 15,
    "siggen_start": 1,
    "siggen_stop": 2,
    "scope_mv": 7,
}


def _split_top(s: str, sep: str = ","):
    parts, depth, cur = [], 0, []
    for c in s:
        if c in "([":
            depth += 1
            cur.append(c)
        elif c in ")]":
            depth -= 1
            cur.append(c)
        elif c == sep and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if cur:
        parts.append("".join(cur).strip())
    return [p for p in parts if p]


class Runner:
    def __init__(self, base: Path | None = None):
        self.env = {}
        self.structs = {}
        self.out = []
        self.base = Path(base) if base else Path.cwd()
        self.include_stack = []
        self.include_seen = set()
        self.bind = {}
        self.stop_this = False
        self.line = 1
        self.src = ""
        self.steps = 0
        self.max_run = DEFAULT_MAX_RUN
        self.ram_cells = DEFAULT_RAM_CELLS
        self.storage_bytes = DEFAULT_STORAGE
        self.thread_cap = max(1, DEFAULT_THREADS)
        self.parallel_n = 1
        self.ram_used = 0
        self.retval = None

    def _tick(self, n=1):
        self.steps += n
        if self.steps > self.max_run:
            raise RunError("max_run exceeded (%d)" % self.max_run, self.line)

    def _ram_add(self, cells: int):
        self.ram_used += cells
        if self.ram_used > self.ram_cells:
            raise RunError("ram limit exceeded (%d cells)" % self.ram_cells, self.line)

    def run(self, src: str, path: str | None = None) -> str:
        if path:
            self.base = Path(path).resolve().parent
        src = self._includes(_strip_comments(src), self.base)
        try:
            from toplethos_latex import preprocess
            src = preprocess(src)
        except Exception:
            pass
        src = self._apply_vm_params(src)
        self.src = src
        self._exec_block(src, 0)
        return "".join(self.out)

    def _apply_vm_params(self, src: str) -> str:
        def grab(name, default_attr):
            m = re.search(r"set_%s\s+(\d+)\s*;" % name, src)
            if m:
                setattr(self, default_attr, int(m.group(1)))
                return re.sub(r"set_%s\s+\d+\s*;" % name, "", src)
            return src

        src = grab("max_run", "max_run")
        src = grab("step_depth", "max_run")
        src = grab("ram", "ram_cells")
        src = grab("storage", "storage_bytes")
        src = grab("threads", "thread_cap")
        src = re.sub(r"set_(?!max_run|step_depth|ram|storage|threads)\w+[^\n;]*;", "", src)
        return src

    def _includes(self, src: str, base: Path) -> str:
        def one(m):
            rel = m.group(1)
            p = (base / rel).resolve()
            key = str(p)
            if key in self.include_seen or key in self.include_stack:
                return ""
            if not p.exists():
                raise RunError("include not found: %s" % rel)
            self.include_seen.add(key)
            self.include_stack.append(key)
            text = self._includes(_strip_comments(p.read_text(encoding="utf-8")), p.parent)
            self.include_stack.pop()
            return text

        return re.sub(r'(?:include|import)\s+"([^"]+)"\s*;', one, src)

    def _exec_block(self, src: str, origin: int):
        i = 0
        n = len(src)
        while i < n:
            while i < n and src[i].isspace():
                i += 1
            if i >= n:
                break
            self.line = _line_at(self.src, origin + i) if self.src else _line_at(src, i)
            self._tick()
            if src.startswith("fn ", i):
                i = self._take_fn(src, i)
                continue
            if src.startswith("struct ", i):
                i = self._take_struct(src, i)
                continue
            if src.startswith("if[", i):
                i = self._exec_if(src, i)
                continue
            if src.startswith("do n[", i) or src.startswith("do ", i):
                i = self._exec_do(src, i)
                continue
            end = src.find(";", i)
            if end < 0:
                stmt, nxt = src[i:].strip(), n
            else:
                stmt, nxt = src[i:end].strip(), end + 1
            i = nxt
            if stmt:
                try:
                    self._stmt(stmt)
                except RunError:
                    raise
                except Exception as e:
                    raise RunError(str(e), self.line) from e

    def _take_struct(self, src, i):
        m = re.match(r"struct\s+([A-Za-z_]\w*)", src[i:])
        b = src.find("{", i)
        e, nxt = self._matching_brace(src, b)
        fields = []
        body = src[b + 1 : e]
        for part in body.split(";"):
            part = part.strip()
            if not part:
                continue
            mm = re.match(r"(?:i32|f32|bool|string)?\s*([A-Za-z_]\w*)", part)
            if mm:
                fields.append(mm.group(1))
        if m:
            self.structs[m.group(1)] = fields
        return nxt if nxt > i else e + 1

    def _take_fn(self, src, i):
        head = src[i : src.find("{", i)]
        m = re.search(r"fn\s+([A-Za-z_]\w*)", head)
        args = re.search(r"in\[([^\]]*)\]", head)
        par = re.search(r"@parallel\s*(?:\(\s*(\d+)\s*\)|\[\s*(\d+)\s*\])", head) or re.search(
            r"@parallel\b", head
        )
        req = re.search(r"@require\[([^\]]*)\]", head)
        b = src.find("{", i)
        if b < 0:
            return self._skip_fn(src, i)
        e, nxt = self._matching_brace(src, b)
        if not m:
            return self._skip_fn(src, i)
        name = m.group(1)
        params = [p.strip() for p in args.group(1).split(",") if p.strip()] if args else []
        body = src[b + 1 : e]
        expr = "0"
        for line in body.split(";"):
            line = line.strip()
            if line.startswith("return "):
                expr = line[7:].strip()
        pn = 1
        if par and par.lastindex:
            pn = int(par.group(par.lastindex))
        elif par:
            pn = min(self.thread_cap, 4)
        if par:
            pn = max(1, min(pn, self.thread_cap))
        handlers, nxt = self._take_handlers(src, nxt)
        while nxt < len(src) and src[nxt] in " \t\r":
            nxt += 1
        if nxt < len(src) and src[nxt] == ";":
            nxt += 1
        self.env["__fn_" + name] = {
            "params": params,
            "expr": expr,
            "body": body,
            "pn": pn,
            "onfail": handlers.get("onfail", ""),
            "onwarn": handlers.get("onwarn", ""),
            "require": req.group(1).strip() if req else "",
        }
        return nxt

    def _take_handlers(self, src, i):
        handlers = {}
        while True:
            m = re.match(r"\s*(onfail|onwarn)\b", src[i:])
            if not m:
                break
            j = i + m.end()
            while j < len(src) and src[j].isspace():
                j += 1
            if j >= len(src) or src[j] != "{":
                break
            e, nxt = self._matching_brace(src, j)
            handlers[m.group(1)] = src[j + 1 : e]
            i = nxt
        return handlers, i

    def _skip_fn(self, src, i):
        b = src.find("{", i)
        if b < 0:
            return len(src)
        _, nxt = self._matching_brace(src, b)
        while nxt < len(src) and src[nxt] not in ";\n":
            nxt += 1
        if nxt < len(src) and src[nxt] == ";":
            nxt += 1
        return nxt

    def _exec_if(self, src, i):
        lb, rb = src.find("[", i), src.find("]", src.find("[", i))
        cond = self._eval(src[lb + 1 : rb])
        body_l = src.find("{", rb)
        body_r, nxt = self._matching_brace(src, body_l)
        body = src[body_l + 1 : body_r]
        rest = src[body_r + 1 :].lstrip()
        if rest.startswith("else"):
            el = src.find("{", body_r + 1)
            er, nxt = self._matching_brace(src, el)
            self._exec_block(body if cond else src[el + 1 : er], 0)
            return nxt
        if cond:
            self._exec_block(body, 0)
        return nxt

    def _exec_do(self, src, i):
        lb, rb = src.find("[", i), src.find("]", src.find("[", i))
        count = int(self._eval(src[lb + 1 : rb]))
        body_l = src.find("{", rb)
        body_r, nxt = self._matching_brace(src, body_l)
        body = src[body_l + 1 : body_r]
        workers = min(self.parallel_n, self.thread_cap)
        if workers > 1 and count > 1:
            def one(_i):
                r = Runner(self.base)
                r.env = dict(self.env)
                r.max_run = self.max_run
                r.ram_cells = self.ram_cells
                r.storage_bytes = self.storage_bytes
                r.thread_cap = 1
                r.parallel_n = 1
                r._exec_block(body, 0)
                return r.out, r.steps
            with ThreadPoolExecutor(max_workers=workers) as ex:
                futs = [ex.submit(one, i) for i in range(count)]
                for f in as_completed(futs):
                    out, st = f.result()
                    self.out.extend(out)
                    self._tick(st)
        else:
            for _ in range(max(0, count)):
                self._exec_block(body, 0)
        return nxt

    def _matching_brace(self, src, l):
        depth, j = 0, l
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    return j, j + 1
            j += 1
        return len(src) - 1, len(src)

    def _stmt(self, stmt: str):
        if stmt.replace(" ", "") == "STOP[THIS]":
            self.stop_this = True
            raise RunError("STOP[THIS]", self.line)
        if stmt.startswith("print(") and stmt.endswith(")"):
            self.out.append(str(self._eval(stmt[6:-1].strip())) + "\n")
            return
        if stmt.startswith("print[") and stmt.endswith("]"):
            self.out.append(str(self._eval(stmt[6:-1].strip())) + "\n")
            return
        if stmt.startswith("return"):
            rest = stmt[6:].strip()
            self.retval = self._eval(rest) if rest else 0
            return
        # a[i,j] = v
        m = re.match(r"([A-Za-z_]\w*)\[(.+)\]\s*=\s*(.*)$", stmt)
        if m and isinstance(self.env.get(m.group(1)), Arr):
            idx = [self._eval(x.strip()) for x in m.group(2).split(",")]
            self.env[m.group(1)].set(idx, self._eval(m.group(3)))
            return
        # C / Toplethos array decl: i32 name[2][3] @memory_hard = {{1,2,3},{4,5,6}}
        m = re.match(
            r"(?:i32|f32|bool|int|float)?\s*([A-Za-z_]\w*)((?:\s*\[[^\]]*\])+)\s*"
            r"((?:@\w+\s*)*)(?:=\s*(.*))?$",
            stmt,
        )
        if m and m.group(2):
            name, dims_s, props, init = m.group(1), m.group(2), m.group(3) or "", m.group(4)
            dims = []
            for d in re.findall(r"\[([^\]]*)\]", dims_s):
                dims.append(int(self._eval(d)) if d.strip() else 0)
            hard = "memory_hard" in props
            data = None
            if init:
                data = self._parse_c_init(init, dims)
                if 0 in dims:
                    # infer
                    pass
            arr = _hard_load(name, dims) if hard else Arr(dims, data)
            if hard:
                arr.hard = name
                if data is not None:
                    arr.data = data
                    _hard_save(name, arr)
                    used = sum(p.stat().st_size for p in HARD_DIR.glob("*.json")) if HARD_DIR.exists() else 0
                    if used > self.storage_bytes:
                        raise RunError("storage limit exceeded (%d bytes)" % self.storage_bytes, self.line)
            elif data is not None:
                arr.data = data
            self._ram_add(len(arr.data))
            self._declare(name, props, arr)
            return
        m = re.match(r"arr_new\s*\[(.+)\]\s*=\s*([A-Za-z_]\w*)$", stmt)
        if m:
            raise RunError("assignment is name = expr", self.line)
        m = re.match(r"arr_new\s*\((.+)\)\s*=\s*([A-Za-z_]\w*)$", stmt)
        if m:
            raise RunError("assignment is name = expr", self.line)
        m = re.match(
            r"(i32|f32|bool|int|string)\s+([A-Za-z_]\w*)\s*((?:@\w+\s*)*)(?:=\s*(.*))?$",
            stmt,
        )
        if m:
            name, props, init = m.group(2), m.group(3) or "", m.group(4)
            self._declare(name, props, self._eval(init) if init else 0)
            return
        m = re.match(r"([A-Za-z_]\w*)\s*=\s*(.*)$", stmt)
        if m and m.group(2) != "":
            name = m.group(1)
            if name not in self.env:
                raise RunError("declare %s before assign" % name, self.line)
            if not self.bind.get(name, {}).get("mut"):
                raise RunError("%s is immutable" % name, self.line)
            self.env[name] = self._eval(m.group(2))
            return
        if "=" in stmt:
            raise RunError("assignment is name = expr", self.line)
        self._eval(stmt)

    def _declare(self, name, props, value):
        names = re.findall(r"@([A-Za-z_]\w*)", props or "")
        if "mut" in names and "val" in names:
            raise RunError("@mut and @val conflict", self.line)
        if "config" in names and "mut" in names:
            raise RunError("@mut on config", self.line)
        self.env[name] = value
        self.bind[name] = {"mut": "mut" in names}

    def _parse_c_init(self, s, dims):
        s = s.strip()
        # nested braces → nested lists flatten in row-major
        def parse(x, pos):
            while pos < len(x) and x[pos].isspace():
                pos += 1
            if pos < len(x) and x[pos] == "{":
                pos += 1
                items = []
                while pos < len(x):
                    while pos < len(x) and x[pos] in " \t\n,":
                        pos += 1
                    if pos < len(x) and x[pos] == "}":
                        return items, pos + 1
                    v, pos = parse(x, pos)
                    items.append(v)
                return items, pos
            end = pos
            while end < len(x) and x[end] not in ",}":
                end += 1
            tok = x[pos:end].strip()
            return self._eval(tok) if tok else 0, end

        tree, _ = parse(s, 0)

        def flatten(t):
            if isinstance(t, list):
                out = []
                for x in t:
                    out.extend(flatten(x) if isinstance(x, list) else [x])
                return out
            return [t]

        return flatten(tree)

    def _eval(self, expr: str):
        expr = expr.strip()
        if not expr:
            return 0
        expr = re.sub(
            r"\b(sin_amp|sin|cos|tan|native|py|ccall|arr_new)\s*\[([^\[\]]*)\]",
            lambda m: m.group(1) + "(" + m.group(2) + ")",
            expr,
        )
        if expr.startswith("native_seq(") or expr.startswith("nseq("):
            inner = expr[expr.find("(") + 1 :]
            if inner.endswith(")"):
                inner = inner[:-1]
            return self._nseq(inner)
        if expr[0] == '"' and expr[-1] == '"':
            return expr[1:-1]
        # indexing
        m = re.match(r"([A-Za-z_]\w*)\[(.+)\]$", expr)
        if m and isinstance(self.env.get(m.group(1)), Arr):
            idx = [self._eval(x.strip()) for x in m.group(2).split(",")]
            return self.env[m.group(1)].get(idx)
        mcall = re.match(r"([A-Za-z_]\w*)\s+in\[(.*?)\](?:\s+out\[[^\]]*\])?\s*$", expr)
        if mcall and ("__fn_" + mcall.group(1)) in self.env:
            raw_args = [a.strip() for a in mcall.group(2).split(",") if a.strip()]
            return self._call_user(mcall.group(1), raw_args)
        m = re.match(r"([A-Za-z_]\w*)\((.*)\)\s*$", expr)
        if m and ("__fn_" + m.group(1)) in self.env:
            raise RunError("() is grouping; call is %s in[]" % m.group(1), self.line)
        expr2 = re.sub(r"\bsin_amp\s*\(", " _sa(", expr)
        expr2 = re.sub(r"\bsin\s*\(", " _s(", expr2)
        expr2 = re.sub(r"\bcos\s*\(", " _c(", expr2)
        expr2 = re.sub(r"\btan\s*\(", " _t(", expr2)
        expr2 = re.sub(r"\barr_new\s*\(", " _an(", expr2)
        expr2 = re.sub(r"\bnative\s*\(", " _nat(", expr2)
        expr2 = re.sub(r"\bpy\s*\(", " _py(", expr2)
        expr2 = re.sub(r"\bccall\s*\(", " _nat(", expr2)

        def _idx_repl(m):
            name, inner = m.group(1), m.group(2)
            if isinstance(self.env.get(name), Arr):
                return "_idx(\"%s\", %s)" % (name, inner)
            return m.group(0)

        expr2 = re.sub(r"([A-Za-z_]\w*)\[([^\]]+)\]", _idx_repl, expr2)

        def _idx(name, *idx):
            return self.env[name].get(idx)

        safe = {
            "_s": lut_sin_deg,
            "_c": lut_cos_deg,
            "_t": lambda d: (lut_sin_deg(d) * 32767) // (lut_cos_deg(d) or 1),
            "_sa": lut_sin_amp,
            "_an": lambda *d: Arr(list(d)),
            "_nat": _native_call,
            "_py": _py_call,
            "_idx": _idx,
        }
        safe.update(self.env)
        try:
            return eval(expr2, {"__builtins__": {}}, safe)
        except RunError:
            raise
        except Exception as e:
            raise RunError("%s: %s" % (expr, e), self.line) from e

    def _nseq(self, inner: str):
        parts = _split_top(inner)
        if not parts:
            return 0
        steps = []
        packed = True
        for part in parts:
            m = re.match(
                r'(?:native|ccall)\s*\(\s*"([^"]+)"\s*(?:,(.*))?\)$',
                part.strip(),
                re.S,
            )
            if m:
                packed = False
                nid = NSEQ_NIDS.get(m.group(1))
                if nid is None:
                    raise RunError("unknown nseq nid %s" % m.group(1), self.line)
                args = []
                if m.group(2):
                    args = [
                        int(self._eval(x.strip()))
                        for x in _split_top(m.group(2))
                        if x.strip()
                    ]
                steps.append((nid, args))
                continue
            m = re.match(r"(?:delay|delay_ms|sleep)\s*\((.*)\)$", part.strip())
            if m:
                packed = False
                steps.append((9, [int(self._eval(m.group(1)))]))
                continue
            if packed and len(parts) == 1:
                return self._nseq_packed(self._eval(part))
            raise RunError("nseq step: %s" % part, self.line)
        last = 0
        for nid, args in steps:
            last = self._nseq_host(nid, args)
        return last

    def _nseq_packed(self, val):
        if isinstance(val, Arr):
            data = [int(x or 0) for x in val.data]
        elif isinstance(val, (list, tuple)):
            data = [int(x or 0) for x in val]
        else:
            raise RunError("native_seq wants packed i32 array", self.line)
        last = 0
        for i in range(0, len(data) - 6, 7):
            nid = data[i]
            na = max(0, min(5, data[i + 1]))
            args = data[i + 2 : i + 2 + na]
            last = self._nseq_host(nid, args)
        return last

    def _nseq_host(self, nid, args):
        if nid == 9:
            ms = int(args[0]) if args else 0
            if ms > 0:
                time.sleep(min(ms, 5000) / 1000.0)
            return 0
        return 0

    def _call_user(self, name, raw_args):
        meta = self.env["__fn_" + name]
        params = meta["params"]
        body = meta["body"]
        onfail = meta["onfail"]
        require = meta["require"]
        local = dict(self.env)
        local_bind = dict(self.bind)
        for n, a in zip(params, raw_args):
            nm = n.split()[-1]
            local[nm] = self._eval(a)
            local_bind[nm] = {"mut": False}
        saved, self.env = self.env, local
        saved_b, self.bind = self.bind, local_bind
        prev_p = self.parallel_n
        self.parallel_n = meta["pn"]
        self.retval = None
        try:
            if require and not self._eval(require):
                raise RunError("require failed", self.line)
            self._exec_block(body, 0)
            return self.retval if self.retval is not None else 0
        except RunError:
            if not onfail:
                raise
            self.stop_this = False
            try:
                self._exec_block(onfail, 0)
            except RunError:
                if self.stop_this:
                    self.stop_this = False
                    raise
                raise
            if self.stop_this:
                self.stop_this = False
                raise RunError("STOP[THIS]", self.line)
            return self.retval if self.retval is not None else 0
        finally:
            self.parallel_n = prev_p
            self.env = saved
            self.bind = saved_b

    def _fn_parts(self, name):
        v = self.env["__fn_" + name]
        if len(v) == 2:
            return v[0], v[1], "", 1
        if len(v) == 3:
            return v[0], v[1], v[2], 1
        return v


def run_source(src: str, path: str | None = None) -> str:
    return Runner(Path(path).parent if path else None).run(src, path)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write("usage: toplethos_run.py file.vul\n")
        return 2
    path = argv[0]
    src = open(path, encoding="utf-8").read()
    try:
        sys.stdout.write(run_source(src, path))
    except RunError as e:
        sys.stderr.write(str(e) + "\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
