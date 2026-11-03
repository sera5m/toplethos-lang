"""Execute the checked program. Same results as the compiler."""

import threading
import time

from front import Failure

MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
MIN32 = -2147483648
MIN64 = -9223372036854775808


def wrap(n, bits):
    if bits == 32:
        return (n + 2**31) % 2**32 - 2**31
    return (n + 2**63) % 2**64 - 2**63


def cell(ty):
    return {"ty": ty, "present": False, "v": None}


class Interp:
    def __init__(self, prog):
        self.prog = prog
        self.out = []
        self.statics = {}
        self.tls = threading.local()
        self.gate = None

    def _local(self):
        t = self.tls
        if not hasattr(t, "on"):
            t.frames = []
            t.returned = False
            t.fn_stack = []
            t.here_box = None
            t.on = True
        return t

    @property
    def frames(self):
        return self._local().frames

    @property
    def returned(self):
        return self._local().returned

    @returned.setter
    def returned(self, v):
        self._local().returned = v

    @property
    def fn_stack(self):
        return self._local().fn_stack

    @property
    def here_box(self):
        return self._local().here_box

    @here_box.setter
    def here_box(self, v):
        self._local().here_box = v

    def run(self):
        self.call(self.prog["fns"]["main"], [])
        return "".join(self.out)

    def load_bmp(self, e):
        import struct
        path = e["path"]
        n = e["n"]
        slot = None
        for frame in reversed(self.frames):
            if e["id"] in frame:
                slot = frame[e["id"]]
                break
        if slot is None or not slot["present"]:
            raise Failure("absent")
        cells = slot["v"]
        try:
            data = open(path, "rb").read()
        except OSError:
            raise Failure(path)
        if len(data) < 54 or data[0:2] != b"BM":
            raise Failure("bitmap")
        off = struct.unpack_from("<I", data, 10)[0]
        w = struct.unpack_from("<i", data, 18)[0]
        h = struct.unpack_from("<i", data, 22)[0]
        bpp = struct.unpack_from("<H", data, 28)[0]
        comp = struct.unpack_from("<I", data, 30)[0]
        top = h < 0
        if top:
            h = -h
        if bpp != 24 or comp != 0 or w < 1 or h < 1 or w * h != n:
            raise Failure("bitmap")
        stride = (w * 3 + 3) & ~3
        if off + stride * h > len(data):
            raise Failure("bitmap")
        for y in range(h):
            desty = y if top else h - 1 - y
            row = off + y * stride
            for x in range(w):
                b, g, r = data[row + x * 3 : row + x * 3 + 3]
                cell = cells[desty * w + x]
                cell["v"] = (r << 16) | (g << 8) | b
                cell["present"] = True

    def load_bytes(self, e):
        n = e["n"]
        slot = None
        for frame in reversed(self.frames):
            if e["id"] in frame:
                slot = frame[e["id"]]
                break
        if slot is None or not slot["present"]:
            raise Failure("absent")
        cells = slot["v"]
        try:
            data = open(e["path"], "rb").read()
        except OSError:
            raise Failure(e["path"])
        if len(data) > n:
            raise Failure("file longer than the array")
        for i, b in enumerate(data):
            cell = cells[i]
            cell["v"] = b
            cell["present"] = True
        return len(data)

    def call(self, fn, args):
        if fn.get("inlined"):
            return self.native(fn, args)
        frame = {}
        for param, arg in zip(fn["params"], args):
            slot = cell(param["ty"])
            slot["present"] = True
            slot["v"] = arg
            frame[param["id"]] = slot
        if fn["method"]:
            box = cell(("obj", fn["method"]))
            box["present"] = True
            box["v"] = self.here_box
            frame[fn["here"]] = box
        self.frames.append(frame)
        saved = self.returned
        self.fn_stack.append(fn)
        try:
            if fn.get("require") is not None and not self.eval(fn["require"]):
                raise Failure("require")
            return self.stmts(fn["body"])
        finally:
            self.fn_stack.pop()
            self.returned = saved
            self.frames.pop()

    def div_standin(self, row):
        mode = self.fn_stack[-1].get("divzero") if self.fn_stack else None
        if mode == "0":
            return 0.0 if row == ("f64",) else 0
        if mode == "max":
            if row == ("f64",):
                return 1.7976931348623157e308
            if row == ("i32",):
                return 2147483647
            return 9223372036854775807
        return None

    def xor_clear(self, dest, rhs):
        if dest.get("k") != "name" or rhs.get("k") != "name":
            return
        d = self.prog["ids"].get(dest["id"], {})
        s = self.prog["ids"].get(rhs["id"], {})
        if not (d.get("xor") and s.get("xor")) or dest["id"] == rhs["id"]:
            return
        slot = self.cell_of(rhs)
        slot["present"] = False
        slot["v"] = 0

    def cell_mutex(self, iid, ty):
        if ty[0] == "ptr":
            slot = self.cell_of({"k": "name", "id": iid})
            target = slot["v"]
            if not isinstance(target, dict):
                raise Failure("absent")
        else:
            target = self.cell_of({"k": "name", "id": iid})
        if "mu" not in target:
            target["mu"] = threading.Lock()
        return target["mu"]

    def tp_fork(self, name, n, extra):
        if self.gate is not None:
            raise Failure("fork before the previous join")
        fn = self.prog["fns"][name]
        extra_cell = None
        if extra is not None:
            extra_cell = self.cell_of({"k": "name", "id": extra})
        g = {
            "n": n,
            "ready": 0,
            "done": 0,
            "go": False,
            "error": None,
            "outs": [0.0] * n,
            "threads": [],
            "cv": threading.Condition(),
        }
        self.gate = g

        def work(lane):
            with g["cv"]:
                g["ready"] += 1
                g["cv"].notify_all()
                while not g["go"]:
                    g["cv"].wait()
            try:
                args = [lane]
                if extra_cell is not None:
                    args.append(extra_cell)
                val = self.call(fn, args)
                g["outs"][lane] = 0.0 if val is None else float(val)
            except Exception as exc:
                g["error"] = exc
            with g["cv"]:
                g["done"] += 1
                g["cv"].notify_all()

        for lane in range(n):
            t = threading.Thread(target=work, args=(lane,))
            g["threads"].append(t)
            t.start()
        with g["cv"]:
            while g["ready"] < n:
                g["cv"].wait()

    def tp_start(self):
        g = self.gate
        if g is None or g["go"]:
            raise Failure("start before fork")
        with g["cv"]:
            g["go"] = True
            g["cv"].notify_all()
            while g["done"] < g["n"]:
                g["cv"].wait()
        if g["error"] is not None:
            raise g["error"]
        return sum(g["outs"])

    def tp_finish(self):
        g = self.gate
        if g is None or not g["go"] or g["done"] < g["n"]:
            raise Failure("finish before start")
        for t in g["threads"]:
            t.join()
        self.gate = None

    def block(self, body):
        self.returned = False
        try:
            r = self.stmts(body)
        except _Leave:
            return None, "leave"
        except _Continue:
            return None, "continue"
        if self.returned:
            return r, "return"
        return None, "next"

    def stmts(self, body):
        self.returned = False
        for st in body:
            op = st[0]
            if op == "return":
                if self.fn_stack and self.fn_stack[-1].get("ensure") is not None:
                    if not self.eval(self.fn_stack[-1]["ensure"]):
                        raise Failure("ensure")
                self.returned = True
                if st[1] is None:
                    return None
                return self.eval(st[1])
            if op == "time":
                t0 = time.perf_counter()
                r, how = self.block(st[3])
                if how == "return":
                    self.returned = True
                    return r
                if how == "leave":
                    raise _Leave()
                if how == "continue":
                    raise _Continue()
                val = self.eval(st[2])
                self.out.append(f"{st[1]} {time.perf_counter() - t0:.6f} {float(val):.9f}\n")
                continue
            if op == "fork":
                self.tp_fork(st[1], st[2], st[3])
                continue
            if op == "finish":
                self.tp_finish()
                continue
            if op in ("lock", "unlock"):
                mu = self.cell_mutex(st[1], st[2])
                (mu.release if op == "unlock" else mu.acquire)()
                continue
            if op == "leave":
                raise _Leave()
            if op == "continue":
                raise _Continue()
            if op == "decl":
                _, iid, ty, expr, _ = st
                info = self.prog["ids"].get(iid, {})
                if info.get("time") in ("static", "config"):
                    if iid not in self.statics:
                        slot = cell(ty)
                        if ty[0] == "obj":
                            slot["present"] = True
                            slot["v"] = self.fresh_obj(ty[1])
                        elif expr is not None:
                            slot["v"] = self.eval(expr)
                            slot["present"] = True
                            self.xor_clear({"k": "name", "id": iid}, expr)
                        self.statics[iid] = slot
                    self.frames[-1][iid] = self.statics[iid]
                    continue
                slot = cell(ty)
                if ty[0] == "obj":
                    slot["present"] = True
                    slot["v"] = self.fresh_obj(ty[1])
                elif expr is not None:
                    slot["v"] = self.eval(expr)
                    slot["present"] = True
                    self.xor_clear({"k": "name", "id": iid}, expr)
                self.frames[-1][iid] = slot
                continue
            if op == "adecl":
                _, iid, ty, items, _ = st
                slots = []
                zero = 0.0 if ty[1] == ("f64",) else False if ty[1] == ("bool",) else 0
                for k in range(ty[2]):
                    s = cell(ty[1])
                    if items is not None:
                        s["present"] = True
                        s["v"] = self.eval(items[k]) if k < len(items) else zero
                    slots.append(s)
                box = cell(ty)
                box["present"] = True
                box["v"] = slots
                self.frames[-1][iid] = box
                continue
            if op == "store":
                slot = self.cell_of(st[1])
                slot["v"] = self.eval(st[2])
                slot["present"] = True
                self.xor_clear(st[1], st[2])
                continue
            if op == "expr":
                self.eval(st[1])
                continue
            if op == "if":
                if self.eval(st[1]):
                    r, how = self.block(st[2])
                elif st[3]:
                    r, how = self.block(st[3])
                else:
                    continue
                if how == "return":
                    self.returned = True
                    return r
                if how == "leave":
                    raise _Leave()
                if how == "continue":
                    raise _Continue()
                continue
            if op == "loop":
                while True:
                    r, how = self.block(st[1])
                    if how == "return":
                        self.returned = True
                        return r
                    if how == "leave":
                        break
                continue
            if op == "do":
                _, count, iid, body, onfin, _ = st
                n = self.eval(count)
                if not isinstance(n, int) or n < 0:
                    raise Failure("do n[k] with k < 0")
                slot = cell(("i32",))
                slot["present"] = True
                self.frames[-1][iid] = slot
                for i in range(n):
                    slot["v"] = i
                    r, how = self.block(body)
                    if how == "return":
                        self.returned = True
                        return r
                    if how == "leave":
                        break
                if onfin:
                    r, how = self.block(onfin)
                    if how == "return":
                        self.returned = True
                        return r
                    if how == "leave":
                        raise _Leave()
                continue
            raise Failure("bad statement")
        return None

    def fresh_obj(self, name):
        fields = {}
        for fname, fty in self.prog["objects"][name]["fields"]:
            fields[fname] = cell(fty)
        return fields

    def cell_of(self, expr):
        k = expr["k"]
        if k == "name":
            for frame in reversed(self.frames):
                if expr["id"] in frame:
                    slot = frame[expr["id"]]
                    if slot is None:
                        raise Failure("absent")
                    return slot
            raise Failure("absent")
        if k == "field":
            obj = self.obj_of(expr["a"])
            return obj[expr["field"]]
        if k == "index":
            arr = self.eval(expr["a"])
            i = self.eval(expr["i"])
            if not isinstance(i, int) or i < 0 or i >= len(arr):
                raise Failure("index")
            return arr[i]
        if k == "deref":
            slot = self.eval(expr["a"])
            if slot is None:
                raise Failure("absent")
            return slot
        raise Failure("not a cell")

    def obj_of(self, expr):
        if expr["k"] == "name":
            slot = self.cell_of(expr)
            if not slot["present"] or slot["v"] is None:
                raise Failure("absent")
            return slot["v"]
        raise Failure("absent")

    def eval(self, e):
        k = e["k"]
        if k == "int":
            return e["n"] if e["ty"] == ("i32",) else e["n"]
        if k == "f64":
            return float(e["n"])
        if k == "bool":
            return bool(e["v"])
        if k == "name":
            slot = self.cell_of(e)
            if not slot["present"]:
                raise Failure("absent")
            v = slot["v"]
            if self.prog["ids"].get(e["id"], {}).get("ephemeral"):
                slot["present"] = False
                slot["v"] = 0
            return v
        if k == "field":
            slot = self.cell_of(e)
            if not slot["present"]:
                raise Failure("absent")
            return slot["v"]
        if k == "index":
            slot = self.cell_of(e)
            if not slot["present"]:
                raise Failure("absent")
            return slot["v"]
        if k == "deref":
            slot = self.cell_of(e)
            if not slot["present"]:
                raise Failure("absent")
            return slot["v"]
        if k == "addr":
            return self.cell_of(e["a"])
        if k == "as":
            return self.cast(self.eval(e["a"]), e["a"]["ty"], e["ty"])
        if k == "size":
            arr = self.eval(e["a"])
            return len(arr)
        if k == "unary":
            if e["op"] == "~":
                v = self.eval(e["a"])
                bits = 64 if e["ty"] == ("i64",) else 32
                from arith import bitnot
                return bitnot(v, bits)
            v = self.eval(e["a"])
            if e["op"] == "!":
                return not v
            return self.neg(v, e["ty"])
        if k == "upd":
            return self.upd(e)
        if k == "bin":
            return self.binop(e)
        if k == "start":
            return self.tp_start()
        if k == "run":
            self.tp_fork(e["name"], e["n"], e.get("extra"))
            val = self.tp_start()
            self.tp_finish()
            return val
        if k == "call":
            if e["name"] == "print":
                self.out.append(self.show(self.eval(e["args"][0]), e["args"][0]["ty"]) + "\n")
                return None
            args = [self.eval(a) for a in e["args"]]
            return self.call(self.prog["fns"][e["name"]], args)
        if k == "loadbmp":
            self.load_bmp(e)
            return None
        if k == "loadbytes":
            return self.load_bytes(e)
        if k == "method":
            args = [self.eval(a) for a in e["args"]]
            box = self.obj_of(e["a"])
            fn = self.prog["objects"][e["obj"]]["methods"][e["name"]]
            self.here_box = box
            # HERE name lookup uses the frame slot pointing at the object
            saved = self.here_box
            frame_obj = cell(("obj", e["obj"]))
            frame_obj["present"] = True
            frame_obj["v"] = box
            # call() overwrites here from self.here_box
            return self.call(fn, args)
        raise Failure("bad expression")

    def binop(self, e):
        op = e["op"]
        if op == "&&":
            return bool(self.eval(e["a"]) and self.eval(e["b"]))
        if op == "||":
            return bool(self.eval(e["a"]) or self.eval(e["b"]))
        a = self.eval(e["a"])
        b = self.eval(e["b"])
        row = e.get("row", e["ty"])
        a, b = self.promote(a, e["a"]["ty"], b, e["b"]["ty"], row)
        if op in ("==", "!=", "<", ">", "<=", ">="):
            return {"==": a == b, "!=": a != b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[op]
        if row == ("f64",):
            if op in ("**", "//"):
                from arith import fop
                return fop(op, a, b, self.div_standin(row))
            if op == "/" and b == 0.0:
                stood = self.div_standin(row)
                if stood is None:
                    raise Failure("division by zero")
                return stood
            if op == "+":
                return a + b
            if op == "-":
                return a - b
            if op == "*":
                return a * b
            return a / b
        bits = 32 if row == ("i32",) else 64
        if op in ("/", "%", "//", "**", "&", "|", "^", "<<", ">>"):
            from arith import iop
            return iop(op, a, b, bits, self.div_standin(row))
        if op == "/":
            if b == 0:
                stood = self.div_standin(row)
                if stood is None:
                    raise Failure("division by zero")
                return stood
            if a == (MIN32 if bits == 32 else MIN64) and b == -1:
                raise Failure("division overflow")
            sign = -1 if (a < 0) ^ (b < 0) else 1
            return sign * (abs(a) // abs(b))
        if op == "*":
            return wrap(a * b, bits)
        if op == "+":
            return wrap(a + b, bits)
        if op == "-":
            return wrap(a - b, bits)
        raise Failure("bad operator")

    def upd(self, e):
        from arith import wrap
        slot = self.cell_of(e["a"])
        if not slot["present"]:
            raise Failure("absent")
        old = slot["v"]
        bits = 64 if e["ty"] == ("i64",) else 32
        new = wrap(old + (1 if e["op"] == "++" else -1), bits)
        slot["v"] = new
        slot["present"] = True
        return new if e["pre"] else old

    def native(self, fn, args):
        import ctypes
        lib = self._so()
        sym = getattr(lib, fn["name"])
        cargs = []
        argtypes = []
        for p, a in zip(fn["params"], args):
            if p["ty"] == ("f64",):
                argtypes.append(ctypes.c_double)
                cargs.append(float(a))
            elif p["ty"] == ("i64",):
                argtypes.append(ctypes.c_int64)
                cargs.append(int(a))
            else:
                argtypes.append(ctypes.c_int32)
                cargs.append(int(bool(a) if p["ty"] == ("bool",) else a))
        sym.argtypes = argtypes
        out = fn["out"]
        if out == ("f64",):
            sym.restype = ctypes.c_double
        elif out == ("i64",):
            sym.restype = ctypes.c_int64
        elif out is None:
            sym.restype = None
        else:
            sym.restype = ctypes.c_int32
        val = sym(*cargs)
        if out == ("bool",):
            return bool(val)
        return val

    def _so(self):
        if getattr(self, "_lib", None) is not None:
            return self._lib
        import ctypes
        import subprocess
        import tempfile
        from pathlib import Path
        import copy
        from emit import assemble
        prog = copy.deepcopy(self.prog)
        text = assemble(prog)
        td = tempfile.mkdtemp(prefix="tpinline")
        asm = str(Path(td) / "u.s")
        so = str(Path(td) / "u.so")
        Path(asm).write_text(text)
        rt = Path(__file__).resolve().parent / "desktop.c"
        cmd = ["g++", "-shared", "-fPIC", "-pthread", "-lm", "-o", so, asm, str(rt)]
        g = subprocess.run(cmd, text=True, capture_output=True)
        if g.returncode != 0:
            raise Failure(g.stderr.strip() or "inlined compile")
        self._lib = ctypes.CDLL(so)
        return self._lib

    def promote(self, a, ta, b, tb, row):
        if row == ("f64",):
            return float(a), float(b)
        return a, b

    def cast(self, v, src, dst):
        if src == dst:
            return v
        if dst == ("f64",):
            return float(v)
        if dst == ("i64",) and src == ("i32",):
            return int(v)
        raise Failure("as with no row")

    def neg(self, v, ty):
        if ty == ("f64",):
            return -float(v)
        if ty == ("i32",):
            if v == MIN32:
                raise Failure("negation overflow")
            return wrap(-v, 32)
        if v == MIN64:
            raise Failure("negation overflow")
        return wrap(-v, 64)

    def show(self, v, ty):
        if ty == ("bool",):
            return "true" if v else "false"
        if ty == ("f64",):
            return f"{v:.9f}"
        if ty[0] == "ptr":
            return "ptr"
        return str(v)


class _Leave(Exception):
    pass


class _Continue(Exception):
    pass


def execute(prog):
    return Interp(prog).run()
