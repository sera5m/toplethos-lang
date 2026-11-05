"""Lower the checked program to the same results as the interpreter."""

import struct


def _imm32(n):
    return -2147483648 <= n <= 2147483647


def _range_bounds(e):
    if not isinstance(e, dict) or e.get("k") != "bin" or e.get("op") != "&&":
        return None
    left, right = _one_bound(e.get("a")), _one_bound(e.get("b"))
    if not left or not right or left[0] != right[0]:
        return None
    lo = hi = None
    for kind, n in ((left[1], left[2]), (right[1], right[2])):
        if kind == "lo":
            if lo is not None:
                return None
            lo = n
        else:
            if hi is not None:
                return None
            hi = n
    if lo is None or hi is None:
        return None
    name = left[3]
    if name.get("ty") == ("i64",):
        wide = True
    elif name.get("ty") == ("i32",):
        wide = False
    else:
        return None
    return name, lo, hi, wide


def _int_const(e):
    if not isinstance(e, dict):
        return None
    if e.get("k") == "int":
        return e["n"]
    if e.get("k") == "unary" and e.get("op") == "-" and isinstance(e.get("a"), dict) and e["a"].get("k") == "int":
        return -e["a"]["n"]
    return None


def _one_bound(e):
    if not isinstance(e, dict) or e.get("k") != "bin" or e.get("op") not in (">=", "<="):
        return None
    a, b = e.get("a"), e.get("b")
    nb, na = _int_const(b), _int_const(a)
    if isinstance(a, dict) and a.get("k") == "name" and nb is not None:
        return a["id"], "lo" if e["op"] == ">=" else "hi", nb, a
    if isinstance(b, dict) and b.get("k") == "name" and na is not None:
        return b["id"], "lo" if e["op"] == "<=" else "hi", na, b
    return None


class Emit:
    def __init__(self, prog):
        self.regmap = {}
        self.prog = prog
        self.o = []
        self.lits = []
        self.dubs = []
        self.nlab = 0
        self.slot = {}
        self.loops = []
        self.fails = []
        self.static_ids = []
        self.static_size = {}
        self.cur = None
        self.rbx_hold = 0
        self.vreg = {}
        self.extra_pushes = []
        self.stack_pad = 8

    def w(self, s):
        self.o.append(s)

    def lab(self):
        self.nlab += 1
        return f".L{self.nlab}"

    def lit(self, s):
        for lab, old in self.lits:
            if old == s:
                return lab
        lab = f".S{len(self.lits)}"
        self.lits.append((lab, s))
        return lab

    def dub(self, v):
        lab = f".D{len(self.dubs)}"
        self.dubs.append((lab, float(v)))
        return lab

    def _enum64(self, ty):
        return isinstance(ty, tuple) and ty[0] == "enum" and self.prog["enums"][ty[1]]["bits"] == 64

    def enum_cast(self, src, dst):
        from front import enum_limits
        if src == dst:
            return
        src64 = src == ("i64",) or self._enum64(src)
        dst64 = dst == ("i64",) or self._enum64(dst)
        if dst[0] == "enum":
            bits = self.prog["enums"][dst[1]]["bits"]
            if not (src[0] == "enum" and src[1] == dst[1]):
                lo, hi = enum_limits(bits)
                if src == ("i32",) and bits >= 32:
                    pass
                elif src == ("i64",) and bits >= 64:
                    pass
                elif src[0] == "enum" and self.prog["enums"][src[1]]["bits"] <= bits:
                    pass
                else:
                    reg = "%rax" if src64 else "%eax"
                    bad = self.lab()
                    ok = self.lab()
                    self.w(f"    cmp ${lo}, {reg}")
                    self.w(f"    jl {bad}")
                    self.w(f"    cmp ${hi}, {reg}")
                    self.w(f"    jg {bad}")
                    self.w(f"    jmp {ok}")
                    self.w(f"{bad}:")
                    self.fail("enum value does not fit")
                    self.w(f"{ok}:")
            if dst64 and not src64:
                self.w("    movslq %eax, %rax")
            return
        if dst == ("i64",) and not src64:
            self.w("    movslq %eax, %rax")

    def fail(self, msg):
        if msg not in self.fails:
            self.fails.append(msg)
        self.w(f"    jmp .F{self.fails.index(msg)}")

    def cell_size(self, ty):
        if ty[0] == "arr":
            return 16 * ty[2]
        if ty[0] == "obj":
            return 16 * len(self.prog["objects"][ty[1]]["fields"])
        return 16

    def layout(self, fn):
        self.slot = {}
        self.kind = {}
        cursor = 64

        def add(iid, ty):
            nonlocal cursor
            if iid in self.slot or iid == fn.get("here"):
                return
            info = self.prog["ids"].get(iid, {})
            self.kind[iid] = ty
            wide = 64 if info.get("mutex") else self.cell_size(ty)
            if info.get("time") in ("static", "config"):
                if iid not in self.static_ids:
                    self.static_ids.append(iid)
                    self.static_size[iid] = wide
                return
            cursor += wide
            self.slot[iid] = -cursor

        for p in fn["params"]:
            add(p["id"], p["ty"])
        self.walk_decls(fn["body"], add)
        self.frame = (cursor + 15) & ~15

    def walk_decls(self, body, add):
        for st in body:
            op = st[0]
            if op == "decl":
                add(st[1], st[2])
            elif op == "adecl":
                add(st[1], st[2])
            elif op == "if":
                self.walk_decls(st[2], add)
                self.walk_decls(st[3], add)
            elif op == "loop":
                self.walk_decls(st[1], add)
            elif op == "do":
                add(st[2], ("i32",))
                self.walk_decls(st[3], add)
                self.walk_decls(st[4], add)
            elif op == "time":
                self.walk_decls(st[3], add)

    def sym(self, iid):
        return "st_" + iid.replace("#", "_")

    def prologue(self, fn, public):
        self.w(f"    .globl {public}")
        self.w(f"{public}:")
        self.w("    push %rbp")
        self.w("    mov %rsp, %rbp")
        self.w("    push %rbx")
        for reg in self.extra_pushes:
            self.w(f"    push %{reg}")
        if fn["method"]:
            self.w("    mov %rdi, %rbx")
        self.w(f"    sub ${self.frame + self.stack_pad}, %rsp")
        for iid, off in self.slot.items():
            info = self.prog["ids"].get(iid, {})
            if info.get("mutex"):
                self.w(f"    movb $0, {off + 8}(%rbp)")
                self.w(f"    lea {off + 16}(%rbp), %rdi")
                self.w("    xor %eax, %eax")
                self.w("    mov $40, %ecx")
                self.w("    rep stosb")
                continue
            cells = self.cell_size(self.kind[iid]) // 16
            for k in range(cells):
                self.w(f"    movb $0, {off + k * 16 + 8}(%rbp)")
        ints = ["%rsi", "%rdx", "%rcx", "%r8", "%r9"] if fn["method"] else ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"]
        flts = ["%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5"]
        ni = nf = 0
        for p in fn["params"]:
            off = self.slot[p["id"]]
            if p["ty"] == ("f64",):
                self.w(f"    movsd {flts[nf]}, {off}(%rbp)")
                nf += 1
            else:
                self.w(f"    mov {ints[ni]}, {off}(%rbp)")
                ni += 1
            self.w(f"    movb $1, {off + 8}(%rbp)")
        if fn.get("require"):
            self.guard(fn["require"], "require")

    def guard(self, expr, msg):
        ok = self.lab()
        self.gen(expr)
        self.w("    test %eax, %eax")
        self.w(f"    jnz {ok}")
        self.fail(msg)
        self.w(f"{ok}:")

    def pass_extra(self, iid):
        if iid is None:
            self.w("    xor %edx, %edx")
            return
        info = self.prog["ids"][iid]
        if info.get("time") in ("static", "config"):
            self.w(f"    lea {self.sym(iid)}(%rip), %rdx")
        else:
            self.w(f"    lea {self.slot[iid]}(%rbp), %rdx")

    def lock_cell(self, iid, ty, unlock):
        fn = "pthread_mutex_unlock@PLT" if unlock else "pthread_mutex_lock@PLT"
        if ty[0] == "ptr":
            self.w(f"    mov {self.slot[iid]}(%rbp), %rdi")
            self.w("    add $16, %rdi")
        else:
            info = self.prog["ids"][iid]
            if info.get("time") in ("static", "config"):
                self.w(f"    lea {self.sym(iid)}+16(%rip), %rdi")
            else:
                self.w(f"    lea {self.slot[iid] + 16}(%rbp), %rdi")
        self.w(f"    call {fn}")

    def epilogue(self, name):
        self.w(f".Lret_{name}:")
        self.w(f"    add ${self.frame + self.stack_pad}, %rsp")
        for reg in reversed(self.extra_pushes):
            self.w(f"    pop %{reg}")
        self.w("    pop %rbx")
        self.w("    pop %rbp")
        self.w("    ret")

    def gen(self, e):
        k = e["k"]
        if k == "int":
            if e["ty"] == ("i64",):
                self.w(f"    movabs ${e['n']}, %rax")
            else:
                self.w(f"    mov ${e['n']}, %eax")
            return
        if k == "enumitem":
            if self._enum64(e["ty"]):
                self.w(f"    movabs ${e['n']}, %rax")
            else:
                self.w(f"    mov ${e['n']}, %eax")
            return
        if k == "f64":
            self.w(f"    movsd {self.dub(e['n'])}(%rip), %xmm0")
            return
        if k == "bool":
            self.w(f"    mov ${1 if e['v'] else 0}, %eax")
            return
        if k in ("name", "field", "index", "deref"):
            self.load_cell(e)
            return
        if k == "statecmp":
            self.statecmp(e)
            return
        if k == "addr":
            self.addr(e["a"])
            return
        if k == "as":
            self.gen(e["a"])
            src, dst = e["a"]["ty"], e["ty"]
            if src[0] == "enum" or dst[0] == "enum":
                self.enum_cast(src, dst)
                return
            if src != dst and dst == ("f64",):
                self.w("    cvtsi2sd %rax, %xmm0" if src == ("i64",) else "    cvtsi2sd %eax, %xmm0")
            elif src == ("i32",) and dst == ("i64",):
                self.w("    movslq %eax, %rax")
            return
        if k == "size":
            self.w(f"    mov ${e['a']['ty'][2]}, %eax")
            return
        if k == "unary":
            self.gen(e["a"])
            self.unary(e)
            return
        if k == "upd":
            self.upd(e)
            return
        if k == "bin":
            self.binop(e)
            return
        if k == "call":
            self.call(e)
            return
        if k == "loadbmp":
            self.loadbmp(e)
            return
        if k == "savebmp":
            self.savebmp(e)
            return
        if k == "loadbytes":
            self.loadbytes(e)
            return
        if k == "gputwo":
            self.gputwo(e)
            return
        if k == "gpubench":
            self.gpubench(e)
            return
        if k == "start":
            self.w("    call tp_start@PLT")
            return
        if k == "run":
            self.w(f"    mov ${e['n']}, %edi")
            self.w(f"    lea {e['name']}(%rip), %rsi")
            self.pass_extra(e.get("extra"))
            self.w("    call tp_run@PLT")
            return
        if k == "method":
            self.method(e)
            return
        raise RuntimeError(k)

    def load_cell(self, e):
        if e["k"] == "name" and e.get("fast"):
            self.load_fast(e)
            return
        self.addr(e)
        info = self.prog["ids"].get(e.get("id"), {}) if e["k"] == "name" else {}
        ephemeral = bool(info.get("ephemeral"))
        if ephemeral:
            self.w("    sub $16, %rsp")
            self.w("    mov %rax, (%rsp)")
        self.w("    cmpb $1, 8(%rax)")
        self.w("    je 1f")
        self.fail("absent")
        self.w("1:")
        ty = e["ty"]
        if ty == ("f64",):
            self.w("    movsd (%rax), %xmm0")
        elif ty == ("i64",) or ty[0] in ("ptr", "arr", "obj") or self._enum64(ty):
            self.w("    mov (%rax), %rax")
        else:
            self.w("    mov (%rax), %eax")
        if ephemeral:
            self.w("    mov (%rsp), %rcx")
            self.w("    movq $0, (%rcx)")
            self.w("    movb $2, 8(%rcx)")
            self.w("    add $16, %rsp")

    def load_fast(self, e):
        reg = self.regmap.get(e["id"]) if getattr(self, "regmap", None) else None
        if reg:
            kind, r = reg
            self.w(f"    movapd %{r}, %xmm0" if kind == "xmm" else f"    mov %{r}, %eax")
            return
        home = self.vreg.get(e["id"])
        if home:
            if e["ty"] == ("i64",) or self._enum64(e["ty"]):
                self.w(f"    mov %{home}, %rax")
            else:
                self.w(f"    mov %{self._r32(home)}, %eax")
            return
        info = self.prog["ids"][e["id"]]
        ty = e["ty"]
        if info.get("time") in ("static", "config"):
            sym = self.sym(e["id"])
            if ty == ("f64",):
                self.w(f"    movsd {sym}(%rip), %xmm0")
            else:
                self.w(f"    mov {sym}(%rip), %rax")
            return
        off = self.slot[e["id"]]
        if ty == ("f64",):
            self.w(f"    movsd {off}(%rbp), %xmm0")
        elif ty == ("i64",) or ty[0] == "ptr" or self._enum64(ty):
            self.w(f"    mov {off}(%rbp), %rax")
        else:
            self.w(f"    mov {off}(%rbp), %eax")

    def place(self, dest, ty):
        reg = self.regmap.get(dest["id"]) if getattr(self, "regmap", None) else None
        if reg:
            kind, r = reg
            self.w(f"    movapd %xmm0, %{r}" if kind == "xmm" else f"    mov %eax, %{r}")
            return
        home = self.vreg.get(dest["id"])
        if home:
            if ty == ("i64",) or self._enum64(ty):
                self.w(f"    mov %rax, %{home}")
            else:
                self.w(f"    mov %eax, %{self._r32(home)}")
            return
        info = self.prog["ids"][dest["id"]]
        if info.get("time") in ("static", "config"):
            sym = self.sym(dest["id"])
            if ty == ("f64",):
                self.w(f"    movsd %xmm0, {sym}(%rip)")
            else:
                self.w(f"    mov %rax, {sym}(%rip)")
            self.w(f"    movb $1, {sym}+8(%rip)")
            return
        off = self.slot[dest["id"]]
        if ty == ("f64",):
            self.w(f"    movsd %xmm0, {off}(%rbp)")
        else:
            self.w(f"    mov %rax, {off}(%rbp)")
        self.w(f"    movb $1, {off + 8}(%rbp)")

    def addr(self, e):
        k = e["k"]
        if k == "name":
            info = self.prog["ids"][e["id"]]
            if info["name"] == "HERE":
                self.w("    mov %rbx, %rax")
                return
            if info.get("time") in ("static", "config"):
                self.w(f"    lea {self.sym(e['id'])}(%rip), %rax")
                return
            off = self.slot[e["id"]]
            self.w(f"    lea {off}(%rbp), %rax")
            return
        if k == "field":
            self.addr(e["a"])
            names = [f[0] for f in self.prog["objects"][e["a"]["ty"][1]]["fields"]]
            off = names.index(e["field"]) * 16
            if off:
                self.w(f"    add ${off}, %rax")
            return
        if k == "index":
            self.gen(e["i"])
            if e["a"]["k"] == "name":
                # The base is a lea. It does not clobber the index.
                self.w("    mov %eax, %ecx")
                self.addr(e["a"])
            else:
                self.w("    sub $16, %rsp")
                self.w("    mov %rax, (%rsp)")
                self.addr(e["a"])
                self.w("    mov (%rsp), %rcx")
                self.w("    add $16, %rsp")
            n = e["a"]["ty"][2]
            self.w(f"    cmp ${n}, %ecx")
            self.w("    jb 1f")
            self.fail("index")
            self.w("1:")
            self.w("    shl $4, %rcx")
            self.w("    add %rcx, %rax")
            return
        if k == "deref":
            self.gen(e["a"])
            return
        raise RuntimeError("addr " + k)

    def store(self, dest, rhs):
        self.gen(rhs)
        if dest["k"] == "name":
            self.place(dest, rhs["ty"])
            self.xor_move(dest, rhs)
            return
        if rhs["ty"] == ("f64",) or self.cur.get("method") or self.rbx_hold:
            self.w("    sub $16, %rsp")
            if rhs["ty"] == ("f64",):
                self.w("    movsd %xmm0, (%rsp)")
            else:
                self.w("    mov %rax, (%rsp)")
            self.addr(dest)
            if rhs["ty"] == ("f64",):
                self.w("    movsd (%rsp), %xmm0")
                self.w("    movsd %xmm0, (%rax)")
            else:
                self.w("    mov (%rsp), %rcx")
                self.w("    mov %rcx, (%rax)")
            self.w("    movb $1, 8(%rax)")
            self.w("    add $16, %rsp")
            self.xor_move(dest, rhs)
            return
        # rbx is callee-saved and not HERE here. Nested address math sees rbx_hold and stays off it.
        self.w("    mov %rax, %rbx")
        self.rbx_hold += 1
        self.addr(dest)
        self.rbx_hold -= 1
        self.w("    mov %rbx, (%rax)")
        self.w("    movb $1, 8(%rax)")
        self.xor_move(dest, rhs)

    def xor_move(self, dest, rhs):
        if dest.get("k") != "name" or rhs.get("k") != "name":
            return
        d = self.prog["ids"].get(dest["id"], {})
        s = self.prog["ids"].get(rhs["id"], {})
        if not (d.get("xor") and s.get("xor")) or dest["id"] == rhs["id"]:
            return
        self.addr(rhs)
        self.w("    movq $0, (%rax)")
        self.w("    movb $0, 8(%rax)")

    def unary(self, e):
        if e["op"] == "~":
            self.w("    not %rax" if e["ty"] == ("i64",) else "    not %eax")
            return
        if e["op"] == "!":
            self.w("    xor $1, %eax")
            return
        ty = e["ty"]
        if ty == ("f64",):
            self.w(f"    movsd {self.dub(-0.0)}(%rip), %xmm1")
            self.w("    xorpd %xmm1, %xmm0")
            return
        if ty == ("i32",):
            self.w("    cmp $0x80000000, %eax")
            self.w("    jne 1f")
            self.fail("negation overflow")
            self.w("1:")
            self.w("    neg %eax")
            return
        self.w("    movabs $0x8000000000000000, %rcx")
        self.w("    cmp %rcx, %rax")
        self.w("    jne 1f")
        self.fail("negation_overflow")
        self.w("1:")
        self.w("    neg %rax")

    def binop(self, e):
        op = e["op"]
        if op in ("$=", "!$="):
            self.w("    xor %eax, %eax" if op == "$=" else "    mov $1, %eax")
            return
        if op in ("&&", "||"):
            done = self.lab()
            skip = self.lab()
            self.gen(e["a"])
            self.w("    test %eax, %eax")
            if op == "||":
                self.w(f"    jnz {skip}")
                self.gen(e["b"])
                self.w("    test %eax, %eax")
                self.w(f"    jnz {skip}")
                self.w("    xor %eax, %eax")
                self.w(f"    jmp {done}")
                self.w(f"{skip}:")
                self.w("    mov $1, %eax")
            else:
                self.w(f"    jz {skip}")
                self.gen(e["b"])
                self.w("    test %eax, %eax")
                self.w(f"    jz {skip}")
                self.w("    mov $1, %eax")
                self.w(f"    jmp {done}")
                self.w(f"{skip}:")
                self.w("    xor %eax, %eax")
            self.w(f"{done}:")
            return
        self.gen(e["a"])
        row = e.get("row", e["ty"])
        if row != ("f64",):
            self._int_binop_right(e, op, row)
            return
        self.w("    sub $16, %rsp")
        left_f = e["a"]["ty"] == ("f64",)
        if left_f:
            self.w("    movsd %xmm0, (%rsp)")
        else:
            self.w("    mov %rax, (%rsp)")
        self.gen(e["b"])
        row = e.get("row", e["ty"])
        if row == ("f64",):
            if e["b"]["ty"] != ("f64",):
                self.w("    cvtsi2sd %rax, %xmm0" if e["b"]["ty"] == ("i64",) else "    cvtsi2sd %eax, %xmm0")
            self.w("    movsd (%rsp), %xmm1")
            if not left_f:
                # left was int, sitting in memory as bits. Reload and convert.
                self.w("    mov (%rsp), %rax")
                self.w("    cvtsi2sd %rax, %xmm1" if e["a"]["ty"] == ("i64",) else "    cvtsi2sd %eax, %xmm1")
            self.w("    add $16, %rsp")
            if op in ("==", "!=", "<", ">", "<=", ">="):
                self.fcmp(op)
                return
            if op in ("**", "//"):
                self.fhelper(op)
                return
            if op == "/":
                zero = self.lab()
                done = self.lab()
                self.w("    xorpd %xmm2, %xmm2")
                self.w("    ucomisd %xmm2, %xmm0")
                self.w(f"    jne {zero}")
                mode = self.cur.get("divzero")
                if mode == "0":
                    self.w("    xorpd %xmm0, %xmm0")
                elif mode == "max":
                    self.w(f"    movsd {self.dub(1.7976931348623157e+308)}(%rip), %xmm0")
                else:
                    self.fail("division by zero")
                self.w(f"    jmp {done}")
                self.w(f"{zero}:")
                self.w("    divsd %xmm0, %xmm1")
                self.w("    movapd %xmm1, %xmm0")
                self.w(f"{done}:")
                return
            asm = {"+": "addsd", "-": "subsd", "*": "mulsd", "/": "divsd"}[op]
            self.w(f"    {asm} %xmm0, %xmm1")
            self.w("    movapd %xmm1, %xmm0")
            return

    def _int_leaf(self, e):
        ty = e.get("ty")
        if ty not in (("i32",), ("i64",), ("bool",)):
            return False
        if e.get("k") == "int":
            return True
        if e.get("k") == "name" and e.get("fast"):
            if self.regmap and e["id"] in self.regmap:
                return False
            return True
        return False

    def _load_rcx(self, e):
        # Does not clobber rax. The left operand is already there.
        wide = e.get("ty") == ("i64",)
        if e["k"] == "int":
            n = e["n"]
            if wide and not (-2147483648 <= n <= 2147483647):
                self.w(f"    movabs ${n}, %rcx")
            elif wide:
                self.w(f"    mov ${n}, %rcx")
            else:
                self.w(f"    mov ${n}, %ecx")
            return
        home = self.vreg.get(e["id"]) if e.get("k") == "name" else None
        if home:
            if wide:
                self.w(f"    mov %{home}, %rcx")
            else:
                self.w(f"    mov %{self._r32(home)}, %ecx")
            return
        info = self.prog["ids"][e["id"]]
        reg = "%rcx" if wide else "%ecx"
        if info.get("time") in ("static", "config"):
            self.w(f"    mov {self.sym(e['id'])}(%rip), {reg}")
        else:
            self.w(f"    mov {self.slot[e['id']]}(%rbp), {reg}")

    def _hold_left(self):
        if self.cur.get("method") or self.rbx_hold:
            self.w("    sub $16, %rsp")
            self.w("    mov %rax, (%rsp)")
            return "stack"
        self.rbx_hold += 1
        self.w("    mov %rax, %rbx")
        return "rbx"

    def _unhold_left(self, kind):
        self.w("    mov %rax, %rcx")
        if kind == "rbx":
            self.rbx_hold -= 1
            self.w("    mov %rbx, %rax")
        else:
            self.w("    mov (%rsp), %rax")
            self.w("    add $16, %rsp")

    def _int_binop_right(self, e, op, row):
        if self._int_leaf(e["b"]):
            self._load_rcx(e["b"])
        else:
            kind = self._hold_left()
            self.gen(e["b"])
            self._unhold_left(kind)
        self._int_finish(e, op, 64 if row == ("i64",) else 32)

    def _int_finish(self, e, op, bits):
        if op in ("==", "!=", "<", ">", "<=", ">="):
            self.w("    cmp %rcx, %rax" if bits == 64 else "    cmp %ecx, %eax")
            setcc = {"==": "e", "!=": "ne", "<": "l", ">": "g", "<=": "le", ">=": "ge"}[op]
            self.w(f"    set{setcc} %al")
            self.w("    movzx %al, %eax")
            return
        if op == "/":
            k = _pow2_shift(e["b"], bits)
            if k is not None:
                self.idiv_pow2(bits, k)
                return
            self.idiv(bits)
            return
        if op == "%":
            self.idiv(bits, rem=True)
            return
        if op == "//":
            self.ifloor(bits)
            return
        if op == "**":
            self.ihelper("tp_ipow", bits)
            return
        if op in ("&", "|", "^"):
            asm = {"&": "and", "|": "or", "^": "xor"}[op]
            self.w(f"    {asm} %rcx, %rax" if bits == 64 else f"    {asm} %ecx, %eax")
            return
        if op in ("<<", ">>"):
            self.ishift(bits, op)
            return
        if bits == 32:
            self.w({"+" : "add %ecx, %eax", "-": "sub %ecx, %eax", "*": "imul %ecx, %eax"}[op])
        else:
            self.w({"+" : "add %rcx, %rax", "-": "sub %rcx, %rax", "*": "imul %rcx, %rax"}[op])

    def fcmp(self, op):
        # xmm1 is left, xmm0 is right.
        self.w("    ucomisd %xmm0, %xmm1")
        end = self.lab()
        nan = self.lab()
        self.w(f"    jp {nan}")
        flag = {"==": "e", "!=": "ne", "<": "b", ">": "a", "<=": "be", ">=": "ae"}[op]
        self.w(f"    set{flag} %al")
        self.w("    movzx %al, %eax")
        self.w(f"    jmp {end}")
        self.w(f"{nan}:")
        self.w("    mov $1, %eax" if op == "!=" else "    xor %eax, %eax")
        self.w(f"{end}:")

    def idiv_pow2(self, bits, k):
        # Toward zero, so a negative dividend is adjusted before the arithmetic shift.
        if k == 0:
            return
        if bits == 32:
            self.w("    mov %eax, %ecx")
            self.w("    sar $31, %ecx")
            self.w(f"    shr ${32 - k}, %ecx")
            self.w("    add %ecx, %eax")
            self.w(f"    sar ${k}, %eax")
            return
        self.w("    mov %rax, %rcx")
        self.w("    sar $63, %rcx")
        self.w(f"    shr ${64 - k}, %rcx")
        self.w("    add %rcx, %rax")
        self.w(f"    sar ${k}, %rax")

    def idiv(self, bits, rem=False):
        reg = "%ecx" if bits == 32 else "%rcx"
        zero = self.lab()
        done = self.lab()
        self.w(f"    test {reg}, {reg}")
        self.w(f"    jne {zero}")
        mode = self.cur.get("divzero")
        if mode == "0":
            self.w("    xor %eax, %eax")
        elif mode == "max":
            if bits == 32:
                self.w("    mov $2147483647, %eax")
            else:
                self.w("    movabs $9223372036854775807, %rax")
        else:
            self.fail("division by zero")
        self.w(f"    jmp {done}")
        self.w(f"{zero}:")
        if bits == 32:
            skip = self.lab()
            self.w("    cmp $-1, %ecx")
            self.w(f"    jne {skip}")
            self.w("    cmp $0x80000000, %eax")
            self.w(f"    jne {skip}")
            self.fail("division overflow")
            self.w(f"{skip}:")
            self.w("    cdq")
            self.w("    idiv %ecx")
        else:
            skip = self.lab()
            self.w("    cmp $-1, %rcx")
            self.w(f"    jne {skip}")
            self.w("    movabs $0x8000000000000000, %rdx")
            self.w("    cmp %rdx, %rax")
            self.w(f"    jne {skip}")
            self.fail("division overflow")
            self.w(f"{skip}:")
            self.w("    cqto")
            self.w("    idiv %rcx")
        if rem:
            self.w("    mov %rdx, %rax" if bits == 64 else "    mov %edx, %eax")
        self.w(f"{done}:")

    def ifloor(self, bits):
        mode = {"0": 1, "max": 2}.get(self.cur.get("divzero"), 0)
        self.w("    mov %rcx, %r10")
        if bits == 32:
            self.w("    movslq %eax, %rdi")
            self.w("    movslq %r10d, %rsi")
        else:
            self.w("    mov %rax, %rdi")
            self.w("    mov %r10, %rsi")
        self.w(f"    mov ${bits}, %edx")
        self.w(f"    mov ${mode}, %ecx")
        self.w("    call tp_floor@PLT")

    def ihelper(self, name, bits):
        if bits == 32:
            self.w("    movslq %eax, %rdi")
            self.w("    movslq %ecx, %rsi")
        else:
            self.w("    mov %rax, %rdi")
            self.w("    mov %rcx, %rsi")
        self.w(f"    mov ${bits}, %edx")
        self.w(f"    call {name}@PLT")

    def fhelper(self, op):
        mode = {"0": 1, "max": 2}.get(self.cur.get("divzero"), 0)
        self.w("    movapd %xmm0, %xmm2")
        self.w("    movapd %xmm1, %xmm0")
        self.w("    movapd %xmm2, %xmm1")
        if op == "**":
            self.w("    call tp_fpow@PLT")
            return
        self.w(f"    mov ${mode}, %edi")
        self.w("    call tp_ffloor@PLT")

    def ishift(self, bits, op):
        bad, ok = self.lab(), self.lab()
        width = 32 if bits == 32 else 64
        self.w(f"    cmp ${width}, %ecx")
        self.w(f"    jae {bad}")
        if bits == 32:
            self.w("    shl %cl, %eax" if op == "<<" else "    sar %cl, %eax")
        else:
            self.w("    shl %cl, %rax" if op == "<<" else "    sar %cl, %rax")
        self.w(f"    jmp {ok}")
        self.w(f"{bad}:")
        self.fail("shift")
        self.w(f"{ok}:")

    def upd(self, e):
        dest = e["a"]
        wide = dest["ty"] == ("i64",)
        self.load_cell(dest)
        if dest["k"] == "name":
            self.w("    mov %rax, %rcx" if wide else "    mov %eax, %ecx")
            if e["op"] == "++":
                self.w("    add $1, %rax" if wide else "    add $1, %eax")
            else:
                self.w("    sub $1, %rax" if wide else "    sub $1, %eax")
            self.place(dest, dest["ty"])
            if not e["pre"]:
                self.w("    mov %rcx, %rax" if wide else "    mov %ecx, %eax")
            return
        # The address is computed again, and that reuses rcx. Keep the old value on the stack.
        self.w("    sub $16, %rsp")
        self.w("    mov %rax, (%rsp)")
        if e["op"] == "++":
            self.w("    add $1, %rax" if wide else "    add $1, %eax")
        else:
            self.w("    sub $1, %rax" if wide else "    sub $1, %eax")
        self.w("    mov %rax, %rbx")
        self.rbx_hold += 1
        self.addr(dest)
        self.rbx_hold -= 1
        self.w("    mov %rbx, (%rax)")
        self.w("    movb $1, 8(%rax)")
        if e["pre"]:
            self.w("    mov %rbx, %rax")
        else:
            self.w("    mov (%rsp), %rax")
        self.w("    add $16, %rsp")

    def loadbmp(self, e):
        lab = self.lit(e["path"])
        info = self.prog["ids"][e["id"]]
        self.w(f"    lea {lab}(%rip), %rdi")
        if info.get("time") in ("static", "config"):
            self.w(f"    lea {self.sym(e['id'])}(%rip), %rsi")
        else:
            self.w(f"    lea {self.slot[e['id']]}(%rbp), %rsi")
        self.w(f"    mov ${e['n']}, %edx")
        self.w("    call tp_loadbmp@PLT")

    def savebmp(self, e):
        self.gen(e["w"])
        self.w("    sub $16, %rsp")
        self.w("    mov %rax, (%rsp)")
        self.gen(e["h"])
        self.w("    sub $16, %rsp")
        self.w("    mov %rax, (%rsp)")
        lab = self.lit(e["path"])
        info = self.prog["ids"][e["id"]]
        self.w(f"    lea {lab}(%rip), %rdi")
        if info.get("time") in ("static", "config"):
            self.w(f"    lea {self.sym(e['id'])}(%rip), %rsi")
        else:
            self.w(f"    lea {self.slot[e['id']]}(%rbp), %rsi")
        self.w(f"    mov ${e['n']}, %edx")
        self.w("    mov 16(%rsp), %rcx")
        self.w("    mov (%rsp), %r8")
        self.w("    add $32, %rsp")
        self.w("    call tp_savebmp@PLT")

    def loadbytes(self, e):
        lab = self.lit(e["path"])
        info = self.prog["ids"][e["id"]]
        self.w(f"    lea {lab}(%rip), %rdi")
        if info.get("time") in ("static", "config"):
            self.w(f"    lea {self.sym(e['id'])}(%rip), %rsi")
        else:
            self.w(f"    lea {self.slot[e['id']]}(%rbp), %rsi")
        self.w(f"    mov ${e['n']}, %edx")
        self.w("    call tp_loadbytes@PLT")

    def gputwo(self, e):
        def addr(iid, reg):
            info = self.prog["ids"][iid]
            if info.get("time") in ("static", "config"):
                self.w(f"    lea {self.sym(iid)}(%rip), {reg}")
            else:
                self.w(f"    lea {self.slot[iid]}(%rbp), {reg}")
        addr(e["xs"], "%rdi")
        addr(e["fs"], "%rsi")
        addr(e["gs"], "%rdx")
        self.w(f"    mov ${e['n']}, %ecx")
        self.w("    call tp_gputwo@PLT")

    def gpubench(self, e):
        self.gen(e["mode"])
        self.w("    mov %eax, %edi")
        self.w("    call tp_gpubench@PLT")

    def call(self, e):
        if e["name"] in ("min", "max", "clamp", "abs", "swap"):
            self.builtin(e)
            return
        if e["name"] == "print":
            self.gen(e["args"][0])
            self.print_val(e["args"][0]["ty"])
            return
        self.push_args(e["args"])
        self.pop_args(e["args"], method=False)
        self.w(f"    call {e['name']}")

    def statecmp(self, e):
        info = self.prog["ids"][e["id"]]
        if info.get("time") in ("static", "config"):
            self.w(f"    movzbl {self.sym(e['id'])}+8(%rip), %eax")
        else:
            self.w(f"    movzbl {self.slot[e['id']] + 8}(%rbp), %eax")
        word = {"absent": 0, "present": 1, "empty": 2}[e["word"]]
        self.w(f"    cmp ${word}, %eax")
        self.w("    sete %al" if e["op"] == "==" else "    setne %al")
        self.w("    movzx %al, %eax")

    def builtin(self, e):
        name = e["name"]
        if name == "swap":
            self.gen(e["args"][0])
            self.w("    sub $16, %rsp")
            self.w("    mov %rax, (%rsp)")
            self.gen(e["args"][1])
            self.w("    mov %rax, %rcx")
            self.w("    mov (%rsp), %rax")
            self.w("    mov (%rax), %rdx")
            self.w("    mov (%rcx), %rdi")
            self.w("    mov %rdi, (%rax)")
            self.w("    mov %rdx, (%rcx)")
            self.w("    movzbl 8(%rax), %edx")
            self.w("    movzbl 8(%rcx), %edi")
            self.w("    movb %dil, 8(%rax)")
            self.w("    movb %dl, 8(%rcx)")
            self.w("    add $16, %rsp")
            return
        if name == "abs":
            self.abs_op(e)
            return
        if name == "clamp":
            self.minmax(e["args"][0], e["args"][1], e["ty"], "max")
            self.hold_result(e["ty"])
            self.gen(e["args"][2])
            self.unhold_result(e["ty"])
            self.cmp_pick(e["ty"], "min")
            return
        self.minmax(e["args"][0], e["args"][1], e["ty"], name)

    def hold_result(self, ty):
        self.w("    sub $16, %rsp")
        if ty == ("f64",):
            self.w("    movsd %xmm0, (%rsp)")
        else:
            self.w("    mov %rax, (%rsp)")

    def unhold_result(self, ty):
        # current result is the right hand. Saved result becomes the left.
        if ty == ("f64",):
            self.w("    movsd %xmm0, %xmm1")
            self.w("    movsd (%rsp), %xmm0")
        else:
            wide = ty == ("i64",)
            self.w("    mov %rax, %rcx" if wide else "    mov %eax, %ecx")
            self.w("    mov (%rsp), %rax" if wide else "    mov (%rsp), %eax")
        self.w("    add $16, %rsp")

    def minmax(self, a, b, ty, which):
        self.gen(a)
        self.hold_result(ty)
        self.gen(b)
        self.unhold_result(ty)
        if ty != ("f64",):
            wide = ty == ("i64",)
            self.w("    xchg %rax, %rcx" if wide else "    xchg %eax, %ecx")
        self.cmp_pick(ty, which)

    def cmp_pick(self, ty, which):
        # left is rcx or xmm0. right is rax or xmm1. Result is left if it wins, else right.
        if ty == ("f64",):
            less = self.lab()
            end = self.lab()
            self.w("    ucomisd %xmm1, %xmm0")
            self.w(f"    jb {less}")
            if which == "min":
                self.w("    movsd %xmm1, %xmm0")
            self.w(f"    jmp {end}")
            self.w(f"{less}:")
            if which == "max":
                self.w("    movsd %xmm1, %xmm0")
            self.w(f"{end}:")
            return
        wide = ty == ("i64",)
        left = "%rcx" if wide else "%ecx"
        right = "%rax" if wide else "%eax"
        self.w(f"    cmp {right}, {left}")
        op = "cmovl" if which == "min" else "cmovg"
        self.w(f"    {op} {left}, {right}")

    def abs_op(self, e):
        self.gen(e["args"][0])
        ty = e["ty"]
        end = self.lab()
        if ty == ("f64",):
            self.w("    movmskpd %xmm0, %eax")
            self.w("    test $1, %eax")
            self.w(f"    jz {end}")
            self.w(f"    movsd {self.dub(-0.0)}(%rip), %xmm1")
            self.w("    xorpd %xmm1, %xmm0")
            self.w(f"{end}:")
            return
        if ty == ("i32",):
            ok = self.lab()
            self.w("    cmp $0x80000000, %eax")
            self.w(f"    jne {ok}")
            self.fail("abs")
            self.w(f"{ok}:")
            self.w("    test %eax, %eax")
            self.w(f"    jns {end}")
            self.w("    neg %eax")
            self.w(f"{end}:")
            return
        ok = self.lab()
        self.w("    movabs $0x8000000000000000, %rcx")
        self.w("    cmp %rcx, %rax")
        self.w(f"    jne {ok}")
        self.fail("abs")
        self.w(f"{ok}:")
        self.w("    test %rax, %rax")
        self.w(f"    jns {end}")
        self.w("    neg %rax")
        self.w(f"{end}:")

    def method(self, e):
        self.addr(e["a"])
        self.w("    sub $16, %rsp")
        self.w("    mov %rax, (%rsp)")
        self.push_args(e["args"])
        self.pop_args(e["args"], method=True)
        self.w("    mov (%rsp), %rdi")
        self.w("    add $16, %rsp")
        self.w(f"    call {e['obj']}_{e['name']}")

    def push_args(self, args):
        for a in args:
            self.gen(a)
            self.w("    sub $16, %rsp")
            if a["ty"] == ("f64",):
                self.w("    movsd %xmm0, (%rsp)")
            else:
                self.w("    mov %rax, (%rsp)")

    def pop_args(self, args, method):
        ints = ["%rsi", "%rdx", "%rcx", "%r8", "%r9"] if method else ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"]
        flts = ["%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5"]
        plan = []
        ni = nf = 0
        for a in args:
            if a["ty"] == ("f64",):
                plan.append(("f", flts[nf]))
                nf += 1
            else:
                plan.append(("i", ints[ni]))
                ni += 1
        for kind, reg in reversed(plan):
            if kind == "f":
                self.w(f"    movsd (%rsp), {reg}")
            else:
                self.w(f"    mov (%rsp), {reg}")
            self.w("    add $16, %rsp")

    def print_val(self, ty):
        if isinstance(ty, tuple) and ty[0] == "enum":
            ty = ("i64",) if self._enum64(ty) else ("i32",)
        if ty == ("bool",):
            yes = self.lab()
            end = self.lab()
            self.w("    test %eax, %eax")
            self.w(f"    jnz {yes}")
            self.w(f"    lea {self.lit('false')}(%rip), %rdi")
            self.w(f"    jmp {end}")
            self.w(f"{yes}:")
            self.w(f"    lea {self.lit('true')}(%rip), %rdi")
            self.w(f"{end}:")
            self.w("    call puts@PLT")
            return
        if ty == ("f64",):
            lab = self.lit("%.9f")
            # printf wants a newline too. puts adds newline, printf does not.
            lab = self.lit("%.9f\n")
            self.w(f"    lea {lab}(%rip), %rdi")
            self.w("    mov $1, %eax")
            self.w("    call printf@PLT")
            return
        lab = self.lit("%ld\n")
        if ty == ("i32",):
            self.w("    movslq %eax, %rsi")
        else:
            self.w("    mov %rax, %rsi")
        self.w(f"    lea {lab}(%rip), %rdi")
        self.w("    xor %eax, %eax")
        self.w("    call printf@PLT")

    def stmts(self, body):
        for st in body:
            op = st[0]
            if op == "time":
                self.w("    call tp_mark@PLT")
                self.stmts(st[3])
                self.w("    call tp_lap@PLT")
                self.w("    sub $16, %rsp")
                self.w("    movsd %xmm0, (%rsp)")
                self.load_cell(st[2])
                self.w("    movapd %xmm0, %xmm1")
                self.w("    movsd (%rsp), %xmm0")
                self.w("    add $16, %rsp")
                fmt = self.lit("%s %.6f %.9f\n")
                label = self.lit(st[1])
                self.w(f"    lea {fmt}(%rip), %rdi")
                self.w(f"    lea {label}(%rip), %rsi")
                self.w("    mov $2, %eax")
                self.w("    call printf@PLT")
            elif op == "fork":
                self.w(f"    mov ${st[2]}, %edi")
                self.w(f"    lea {st[1]}(%rip), %rsi")
                self.pass_extra(st[3])
                self.w("    call tp_fork@PLT")
            elif op == "finish":
                self.w("    call tp_finish@PLT")
            elif op == "lock":
                self.lock_cell(st[1], st[2], False)
            elif op == "unlock":
                self.lock_cell(st[1], st[2], True)
            elif op == "hold":
                self.lock_cell(st[1], st[2], False)
                self.stmts(st[3])
                self.lock_cell(st[1], st[2], True)
            elif op == "fchain":
                self.fchain(st)
            elif op == "decl":
                if st[3] is not None:
                    info = self.prog["ids"].get(st[1], {})
                    if info.get("time") in ("static", "config"):
                        skip = self.lab()
                        self.w(f"    lea {self.sym(st[1])}(%rip), %rax")
                        self.w("    cmpb $0, 8(%rax)")
                        self.w(f"    jne {skip}")
                        self.store({"k": "name", "id": st[1], "ty": st[2]}, st[3])
                        self.w(f"{skip}:")
                    else:
                        self.store({"k": "name", "id": st[1], "ty": st[2]}, st[3])
            elif op == "adecl":
                items = st[3]
                if items is None:
                    continue
                base = self.slot[st[1]]
                n = st[2][2]
                et = st[2][1]
                zero = {"k": "f64", "n": 0.0, "ty": ("f64",)} if et == ("f64",) else {"k": "bool", "v": False, "ty": ("bool",)} if et == ("bool",) else {"k": "int", "n": 0, "ty": et}
                for k in range(n):
                    item = items[k] if k < len(items) else zero
                    self.store_elem(base, k, item)
            elif op == "store":
                self.store(st[1], st[2])
            elif op == "expr":
                self.gen(st[1])
            elif op == "return":
                if self.cur.get("ensure"):
                    self.guard(self.cur["ensure"], "ensure")
                if st[1] is not None:
                    self.gen(st[1])
                self.w("    jmp .Lret_PLACE")
            elif op == "if":
                if self.range_if(st):
                    continue
                no = self.lab()
                end = self.lab()
                self.gen(st[1])
                self.w("    test %eax, %eax")
                self.w(f"    jz {no}")
                self.stmts(st[2])
                self.w(f"    jmp {end}")
                self.w(f"{no}:")
                self.stmts(st[3])
                self.w(f"{end}:")
            elif op == "loop":
                top, end = self.lab(), self.lab()
                lname = st[3]["name"] if len(st) > 3 and isinstance(st[3], dict) else None
                self.loops.append((top, end, lname))
                self.w(f"{top}:")
                self.stmts(st[1])
                self.w(f"    jmp {top}")
                self.w(f"{end}:")
                self.loops.pop()
            elif op == "do":
                if len(st) > 6 and isinstance(st[6], dict) and st[6].get("reg") and self.regloop(st):
                    continue
                top, cont, end = self.lab(), self.lab(), self.lab()
                const = st[1].get("k") == "int"
                if not const:
                    self.gen(st[1])
                    self.w("    test %eax, %eax")
                    self.w("    jns 1f")
                    self.fail("do n[k] with k < 0")
                    self.w("1:")
                    self.w("    sub $16, %rsp")
                    self.w("    mov %rax, (%rsp)")
                off = self.slot[st[2]]
                self.w(f"    movl $0, {off}(%rbp)")
                self.w(f"    movb $1, {off + 8}(%rbp)")
                self.w(f"{top}:")
                self.w(f"    mov {off}(%rbp), %eax")
                if const:
                    self.w(f"    cmpl ${st[1]['n']}, %eax")
                else:
                    self.w("    cmp (%rsp), %eax")
                self.w(f"    jge {end}")
                self.loops.append((cont, end, st[6].get("name") if len(st) > 6 and isinstance(st[6], dict) else None))
                self.stmts(st[3])
                self.loops.pop()
                self.w(f"{cont}:")
                self.w(f"    incl {off}(%rbp)")
                self.w(f"    jmp {top}")
                self.w(f"{end}:")
                if not const:
                    self.w("    add $16, %rsp")
                self.stmts(st[4])
            elif op == "leave":
                want = st[2] if len(st) > 2 else None
                if want:
                    found = False
                    for _cont, end, lname in reversed(self.loops):
                        if lname == want:
                            self.w(f"    jmp {end}")
                            found = True
                            break
                    if not found:
                        self.fail("leave[name] with no matching @name")
                else:
                    self.w(f"    jmp {self.loops[-1][1]}")
            elif op == "continue":
                self.w(f"    jmp {self.loops[-1][0]}")

    def range_if(self, st):
        # (x >= lo && x <= hi), constants, used only as this branch.
        # The long form is two cmp/setcc/movzx chains and a test.
        # The short form is one subtract and an unsigned compare.
        hit = _range_bounds(st[1])
        if not hit:
            return False
        name, lo, hi, wide = hit
        if not (_imm32(lo) and _imm32(hi) and lo <= hi and _imm32(hi - lo)):
            return False
        no, end = self.lab(), self.lab()
        self.gen(name)
        reg = "%rax" if wide else "%eax"
        self.w(f"    sub ${lo}, {reg}")
        self.w(f"    cmp ${hi - lo}, {reg}")
        self.w(f"    ja {no}")
        self.stmts(st[2])
        self.w(f"    jmp {end}")
        self.w(f"{no}:")
        self.stmts(st[3])
        self.w(f"{end}:")
        return True

    def tight_horner(self, st):
        # do n[const] { x = as[f64, i] * scale; y = c + x*y ...; s = s + y }.
        # Coefficients stay in xmm. No (%rsp) spill. mul then add, not vfmadd.
        body = st[3]
        if len(body) != 3:
            return False
        sx, chain, ss = body
        if sx[0] != "store" or chain[0] != "fchain" or ss[0] != "store":
            return False
        rhs = sx[2]
        if rhs.get("k") != "bin" or rhs.get("op") != "*":
            return False
        conv, scale = rhs["a"], rhs["b"]
        if scale.get("k") != "f64":
            conv, scale = scale, conv
        if scale.get("k") != "f64" or conv.get("k") != "as":
            return False
        if conv.get("a", {}).get("k") != "name" or conv["a"].get("id") != st[2]:
            return False
        if chain[2].get("k") != "f64" or chain[3].get("id") != sx[1].get("id"):
            return False
        if any(c.get("k") != "f64" for c in chain[4]):
            return False
        coeffs = [chain[2]] + list(chain[4])
        if len(coeffs) < 2 or len(coeffs) > 8:
            return False
        sr = ss[2]
        if sr.get("k") != "bin" or sr.get("op") != "+":
            return False
        arms = {sr["a"].get("id"), sr["b"].get("id")}
        if ss[1].get("id") not in arms or chain[1].get("id") not in arms:
            return False
        count = st[1]["n"]
        iid, xid, yid, sid = st[2], sx[1]["id"], chain[1]["id"], ss[1]["id"]
        # xmm9 downward is c_inner .. c_outer. xmm10 is x, xmm11 is the scale.
        # xmm0 is y, xmm1 is s. Same numbering as the C++ direct loop.
        creg = [f"xmm{9 - n}" for n in range(len(coeffs))]
        top, end = self.lab(), self.lab()
        self.w("    xor %eax, %eax")
        self.w(f"    cmpl ${count}, %eax")
        self.w(f"    jge {end}")
        self.w(f"    movsd {self.slot[sid]}(%rbp), %xmm1")
        self.w(f"    movsd {self.dub(scale['n'])}(%rip), %xmm11")
        for n, c in enumerate(coeffs):
            self.w(f"    movsd {self.dub(c['n'])}(%rip), %{creg[n]}")
        self.w("    .p2align 4")
        self.w(f"{top}:")
        self.w("    pxor %xmm10, %xmm10")
        self.w("    cvtsi2sdl %eax, %xmm10")
        self.w("    addl $1, %eax")
        self.w("    mulsd %xmm11, %xmm10")
        self.w("    movapd %xmm10, %xmm0")
        self.w(f"    mulsd %{creg[0]}, %xmm0")
        for n in range(1, len(coeffs) - 1):
            self.w(f"    addsd %{creg[n]}, %xmm0")
            self.w("    mulsd %xmm10, %xmm0")
        self.w(f"    addsd %{creg[-1]}, %xmm0")
        self.w("    addsd %xmm0, %xmm1")
        self.w(f"    cmpl ${count}, %eax")
        self.w(f"    jne {top}")
        self.w(f"{end}:")
        ioff = self.slot[iid]
        self.w(f"    mov %eax, {ioff}(%rbp)")
        self.w(f"    movb $1, {ioff + 8}(%rbp)")
        # x, y, s are stored once. A zero trip count leaves x and y as they were.
        ran = self.lab()
        self.w("    test %eax, %eax")
        self.w(f"    jz {ran}")
        self.w(f"    movsd %xmm10, {self.slot[xid]}(%rbp)")
        self.w(f"    movb $1, {self.slot[xid] + 8}(%rbp)")
        self.w(f"    movsd %xmm0, {self.slot[yid]}(%rbp)")
        self.w(f"    movb $1, {self.slot[yid] + 8}(%rbp)")
        self.w(f"    movsd %xmm1, {self.slot[sid]}(%rbp)")
        self.w(f"    movb $1, {self.slot[sid] + 8}(%rbp)")
        self.w(f"{ran}:")
        self.stmts(st[4])
        return True

    def regloop(self, st):
        if self.tight_horner(st):
            return True
        # i and the scalar locals stay in registers. Interlocks never get here.
        count, iid, body = st[1]["n"], st[2], st[3]
        f64s, i32s = [], []
        def rec(e):
            if not isinstance(e, dict):
                return
            if e.get("k") == "name" and e["id"] != iid:
                ty = e.get("ty")
                if isinstance(ty, tuple) and ty[0] == "enum" and self._enum64(ty):
                    return
                (f64s if ty == ("f64",) else i32s).append(e["id"])
            for v in e.values():
                if isinstance(v, dict):
                    rec(v)
        for inner in body:
            for part in inner[1:]:
                rec(part)
        f64s = list(dict.fromkeys(f64s))
        i32s = list(dict.fromkeys(i32s))
        xmm = [f"xmm{n}" for n in range(4, 16)]
        gpr = ["r8d", "r9d", "r10d", "r11d"]
        if len(f64s) > len(xmm) or len(i32s) > len(gpr):
            return False
        regmap = {iid: ("gpr", "ecx")}
        for n, i in enumerate(f64s):
            regmap[i] = ("xmm", xmm[n])
        for n, i in enumerate(i32s):
            regmap[i] = ("gpr", gpr[n])
        top, end = self.lab(), self.lab()
        for i, (kind, r) in ((i, regmap[i]) for i in f64s + i32s):
            off = self.slot[i]
            if kind == "xmm":
                self.w(f"    movsd {off}(%rbp), %{r}")
            else:
                self.w(f"    mov {off}(%rbp), %{r}")
        self.w("    xor %ecx, %ecx")
        self.w(f"{top}:")
        self.w(f"    cmpl ${count}, %ecx")
        self.w(f"    jge {end}")
        self.regmap = regmap
        self.stmts(body)
        self.regmap = {}
        self.w("    incl %ecx")
        self.w(f"    jmp {top}")
        self.w(f"{end}:")
        off = self.slot[iid]
        self.w(f"    mov %ecx, {off}(%rbp)")
        self.w(f"    movb $1, {off + 8}(%rbp)")
        for i in f64s + i32s:
            kind, r = regmap[i]
            off = self.slot[i]
            if kind == "xmm":
                self.w(f"    movsd %{r}, {off}(%rbp)")
            else:
                self.w(f"    mov %{r}, {off}(%rbp)")
            self.w(f"    movb $1, {off + 8}(%rbp)")
        self.stmts(st[4])
        return True

    def fchain(self, st):
        # y = c + x * y, repeated. mul then add, two roundings. Not vfmadd.
        dest, init, factor, coeffs = st[1], st[2], st[3], st[4]
        self.gen(init)
        self.w("    sub $16, %rsp")
        self.w("    movsd %xmm0, (%rsp)")
        self.gen(factor)
        self.w("    movapd %xmm0, %xmm1")
        self.w("    movsd (%rsp), %xmm0")
        self.w("    add $16, %rsp")
        risky = init.get("k") != "f64" or any(a.get("k") != "f64" for a in coeffs)
        if risky:
            self.place(dest, ("f64",))
        for addend in coeffs:
            if addend.get("k") == "f64":
                lab = self.dub(addend["n"])
                self.w(f"    movsd {lab}(%rip), %xmm2")
                self.w("    mulsd %xmm1, %xmm0")
                self.w("    addsd %xmm0, %xmm2")
                self.w("    movapd %xmm2, %xmm0")
            else:
                self.w("    movapd %xmm0, %xmm3")
                self.gen(addend)
                self.w("    mulsd %xmm1, %xmm3")
                self.w("    addsd %xmm3, %xmm0")
            if risky:
                self.place(dest, ("f64",))
        if not risky:
            self.place(dest, ("f64",))

    def store_elem(self, base, k, item):
        self.gen(item)
        off = base + k * 16
        if item["ty"] == ("f64",):
            self.w(f"    movsd %xmm0, {off}(%rbp)")
        else:
            self.w(f"    mov %rax, {off}(%rbp)")
        self.w(f"    movb $1, {off + 8}(%rbp)")

    def _r32(self, reg):
        return {"rsi": "esi", "rdi": "edi", "rbx": "ebx"}.get(reg, reg + "d")

    def _assign_vregs(self, fn):
        # Caller-saved first so a short live range does not make us push.
        pool = ["r8", "r9", "r10", "r11", "rsi", "rdi", "r12", "r13", "r14", "r15"]
        self.vreg = {}
        self.extra_pushes = []
        self.stack_pad = 8
        if self._has_regloop(fn["body"]):
            return
        params = {p["id"] for p in fn["params"]}
        do_idx = set()
        defs, uses, slow, calls = {}, {}, set(), []
        loops = []

        def reads(e, pos):
            if isinstance(e, list):
                for x in e:
                    reads(x, pos)
                return
            if not isinstance(e, dict):
                return
            if e.get("k") == "addr" and isinstance(e.get("a"), dict) and e["a"].get("k") == "name":
                slow.add(e["a"]["id"])
            if e.get("k") == "name" and e.get("id"):
                uses.setdefault(e["id"], []).append(pos)
                if not e.get("fast"):
                    slow.add(e["id"])
            if e.get("k") in ("call", "method", "loadbmp", "loadbytes", "gputwo", "gpubench", "start", "run"):
                calls.append(pos)
            for v in e.values():
                if isinstance(v, (dict, list)):
                    reads(v, pos)

        def walk(stmts):
            for st in stmts:
                pos = walk.n = walk.n + 1
                op = st[0]
                if op == "decl":
                    if st[3] is not None:
                        reads(st[3], pos)
                        defs.setdefault(st[1], []).append(pos)
                elif op == "store" and st[1].get("k") == "name":
                    reads(st[2], pos)
                    defs.setdefault(st[1]["id"], []).append(pos)
                elif op == "store":
                    reads(st[1], pos)
                    reads(st[2], pos)
                elif op == "expr":
                    reads(st[1], pos)
                elif op == "return" and st[1] is not None:
                    reads(st[1], pos)
                elif op == "if":
                    reads(st[1], pos)
                    walk(st[2])
                    walk(st[3])
                elif op == "loop":
                    before = walk.n
                    walk(st[1])
                    if walk.n >= before + 1:
                        loops.append((before + 1, walk.n))
                elif op == "do":
                    do_idx.add(st[2])
                    reads(st[1], pos)
                    before = walk.n
                    walk(st[3])
                    if walk.n >= before + 1:
                        loops.append((before + 1, walk.n))
                    walk(st[4])
                elif op == "time":
                    calls.append(pos)
                    reads(st[2], pos)
                    walk(st[3])
                elif op in ("fork", "finish"):
                    calls.append(pos)
                elif op == "adecl" and st[3]:
                    reads(st[3], pos)
        walk.n = 0
        walk(fn["body"])

        intervals = []
        for iid, ds in defs.items():
            if iid in params or iid in do_idx or iid in slow or iid not in uses:
                continue
            info = self.prog["ids"].get(iid, {})
            if info.get("ty") not in (("i32",), ("i64",), ("bool",)):
                continue
            if info.get("time") or info.get("mutex") or info.get("xor") or info.get("ephemeral") or info.get("freeze"):
                continue
            if iid not in self.slot:
                continue
            start, end = min(ds), max(uses[iid])
            if any(u < start for u in uses[iid]):
                continue
            for left, right in loops:
                uses_in = [u for u in uses[iid] if left <= u <= right]
                if not uses_in:
                    continue
                defs_in = [d for d in ds if left <= d <= right]
                # A use before this iteration's store is live across the back edge.
                if not defs_in or any(u < min(defs_in) for u in uses_in):
                    if end < right:
                        end = right
            if any(start <= c <= end for c in calls):
                continue
            intervals.append((start, end, iid))
        intervals.sort()
        active = []
        chosen = []
        for start, end, iid in intervals:
            active = [a for a in active if a[0] >= start]
            used = {a[1] for a in active}
            free = next((r for r in pool if r not in used), None)
            if free is None:
                continue
            active.append((end, free))
            self.vreg[iid] = free
            chosen.append(free)
        self.extra_pushes = [r for r in ("r12", "r13", "r14", "r15") if r in chosen]
        if len(self.extra_pushes) % 2:
            self.stack_pad = 0

    def _has_regloop(self, stmts):
        for st in stmts:
            op = st[0]
            if op == "do" and len(st) > 6 and isinstance(st[6], dict) and st[6].get("reg"):
                return True
            if op == "if" and (self._has_regloop(st[2]) or self._has_regloop(st[3])):
                return True
            if op == "loop" and self._has_regloop(st[1]):
                return True
            if op == "do" and (self._has_regloop(st[3]) or self._has_regloop(st[4])):
                return True
            if op == "time" and self._has_regloop(st[3]):
                return True
        return False

    def emit_fn(self, fn, public):
        self.o = []
        self.cur = fn
        self.rbx_hold = 0
        self.layout(fn)
        self._assign_vregs(fn)
        self.prologue(fn, public)
        self.stmts(fn["body"])
        text = "\n".join(self.o).replace("jmp .Lret_PLACE", f"jmp .Lret_{public}")
        self.o = []
        self.epilogue(public)
        return text + "\n" + "\n".join(self.o)

    def render(self):
        parts = []
        for fn in self.prog["fns"].values():
            parts.append(self.emit_fn(fn, fn["name"]))
        for oname, obj in self.prog["objects"].items():
            for mname, fn in obj["methods"].items():
                parts.append(self.emit_fn(fn, f"{oname}_{mname}"))
        ro = ["    .section .rodata"]
        for lab, v in self.dubs:
            bits = struct.unpack("<Q", struct.pack("<d", v))[0]
            ro.append(f"    .align 8")
            ro.append(f"{lab}: .quad {bits}")
        for lab, s in self.lits:
            data = ",".join(str(b) for b in (s + "\0").encode())
            ro.append(f"{lab}: .byte {data}")
        for msg in self.fails:
            self.lit("failure: " + msg + "\n")
        ro = ["    .section .rodata"]
        for lab, v in self.dubs:
            bits = struct.unpack("<Q", struct.pack("<d", v))[0]
            ro.append("    .align 8")
            ro.append(f"{lab}: .quad {bits}")
        seen = {}
        for lab, s in self.lits:
            if s in seen:
                continue
            seen[s] = lab
            data = ",".join(str(b) for b in (s + "\0").encode())
            ro.append(f"{lab}: .byte {data}")
        tail = []
        emitted = set()
        for i, msg in enumerate(self.fails):
            if i in emitted:
                continue
            emitted.add(i)
            lab = seen["failure: " + msg + "\n"]
            tail.append(f".F{i}:")
            tail.append(f"    lea {lab}(%rip), %rdi")
            tail.append("    mov stderr@GOTPCREL(%rip), %rsi")
            tail.append("    mov (%rsi), %rsi")
            tail.append("    call fputs@PLT")
            tail.append("    mov $1, %edi")
            tail.append("    call exit@PLT")
        bss = ["    .bss"]
        for iid in self.static_ids:
            bss.append("    .align 16")
            bss.append(f"{self.sym(iid)}: .zero {self.static_size[iid]}")
        return "\n".join(
            ["# pipeline: phase1 fold, phase3 fchain, amd64"]
            + ro
            + ["    .text"]
            + parts
            + tail
            + (bss if self.static_ids else [])
            + ['    .section .note.GNU-stack,"",@progbits', ""]
        )


def _pow2_shift(expr, bits):
    if not isinstance(expr, dict) or expr.get("k") != "int":
        return None
    n = expr.get("n")
    if not isinstance(n, int) or n <= 1 or n & (n - 1):
        return None
    k = n.bit_length() - 1
    if k <= 0 or k >= bits:
        return None
    return k


def assemble(prog):
    from passes import optimize
    optimize(prog)
    notes = []
    for name in sorted(prog.get("symbols") or {}):
        row = prog["symbols"][name]
        fns = " ".join(row["functions"]) or "-"
        notes.append(
            f"# symbol {name} cost {row['cost']} time {row['time']} resources {row['resources']} functions {fns}"
        )
    body = Emit(prog).render()
    if not notes:
        return body
    return "\n".join(notes) + "\n" + body
