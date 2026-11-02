"""Lower the checked program to the same results as the interpreter."""

import struct


class Emit:
    def __init__(self, prog):
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
        if fn["method"]:
            self.w("    mov %rdi, %rbx")
        self.w(f"    sub ${self.frame + 8}, %rsp")
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
        self.w(f"    add ${self.frame + 8}, %rsp")
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
        if k == "f64":
            self.w(f"    movsd {self.dub(e['n'])}(%rip), %xmm0")
            return
        if k == "bool":
            self.w(f"    mov ${1 if e['v'] else 0}, %eax")
            return
        if k in ("name", "field", "index", "deref"):
            self.load_cell(e)
            return
        if k == "addr":
            self.addr(e["a"])
            return
        if k == "as":
            self.gen(e["a"])
            src, dst = e["a"]["ty"], e["ty"]
            if src != dst and dst == ("f64",):
                self.w("    cvtsi2sd %rax, %xmm0" if src == ("i64",) else "    cvtsi2sd %eax, %xmm0")
            return
        if k == "size":
            self.w(f"    mov ${e['a']['ty'][2]}, %eax")
            return
        if k == "unary":
            self.gen(e["a"])
            self.unary(e)
            return
        if k == "bin":
            self.binop(e)
            return
        if k == "call":
            self.call(e)
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
        self.w("    sub $16, %rsp")
        self.w("    mov %rax, (%rsp)")
        self.w("    cmpb $0, 8(%rax)")
        self.w("    jne 1f")
        self.fail("absent")
        self.w("1:")
        ty = e["ty"]
        if ty == ("f64",):
            self.w("    movsd (%rax), %xmm0")
        elif ty[0] in ("i64", "ptr", "arr", "obj"):
            self.w("    mov (%rax), %rax")
        else:
            self.w("    mov (%rax), %eax")
        info = self.prog["ids"].get(e.get("id"), {}) if e["k"] == "name" else {}
        if info.get("ephemeral"):
            self.w("    mov (%rsp), %rcx")
            self.w("    movq $0, (%rcx)")
            self.w("    movb $0, 8(%rcx)")
        self.w("    add $16, %rsp")

    def load_fast(self, e):
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
        elif ty == ("i64",) or ty[0] == "ptr":
            self.w(f"    mov {off}(%rbp), %rax")
        else:
            self.w(f"    mov {off}(%rbp), %eax")

    def place(self, dest, ty):
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
        # integer. left in memory, right in rax.
        self.w("    mov %rax, %rcx")
        self.w("    mov (%rsp), %rax")
        self.w("    add $16, %rsp")
        bits = 64 if row == ("i64",) else 32
        if op in ("==", "!=", "<", ">", "<=", ">="):
            self.w("    cmp %rcx, %rax" if bits == 64 else "    cmp %ecx, %eax")
            setcc = {"==": "e", "!=": "ne", "<": "l", ">": "g", "<=": "le", ">=": "ge"}[op]
            self.w(f"    set{setcc} %al")
            self.w("    movzx %al, %eax")
            return
        if op == "/":
            self.idiv(bits)
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

    def idiv(self, bits):
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
            self.w("    cmp $-1, %ecx")
            self.w("    jne 2f")
            self.w("    cmp $0x80000000, %eax")
            self.w("    jne 2f")
            self.fail("division overflow")
            self.w("2:")
            self.w("    cdq")
            self.w("    idiv %ecx")
        else:
            self.w("    cqto")
            self.w("    idiv %rcx")
        self.w(f"{done}:")

    def call(self, e):
        if e["name"] == "print":
            self.gen(e["args"][0])
            self.print_val(e["args"][0]["ty"])
            return
        self.push_args(e["args"])
        self.pop_args(e["args"], method=False)
        self.w(f"    call {e['name']}")

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
                base = self.slot[st[1]]
                items = st[3]
                n = st[2][2]
                et = st[2][1]
                zero = {"k": "f64", "n": 0.0, "ty": ("f64",)} if et == ("f64",) else {"k": "bool", "v": False, "ty": ("bool",)} if et == ("bool",) else {"k": "int", "n": 0, "ty": et}
                if items is None:
                    continue
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
                self.loops.append((top, end))
                self.w(f"{top}:")
                self.stmts(st[1])
                self.w(f"    jmp {top}")
                self.w(f"{end}:")
                self.loops.pop()
            elif op == "do":
                top, cont, end = self.lab(), self.lab(), self.lab()
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
                self.w("    cmp (%rsp), %eax")
                self.w(f"    jge {end}")
                self.loops.append((cont, end))
                self.stmts(st[3])
                self.loops.pop()
                self.w(f"{cont}:")
                self.w(f"    incl {off}(%rbp)")
                self.w(f"    jmp {top}")
                self.w(f"{end}:")
                self.w("    add $16, %rsp")
                self.stmts(st[4])
            elif op == "leave":
                self.w(f"    jmp {self.loops[-1][1]}")
            elif op == "continue":
                self.w(f"    jmp {self.loops[-1][0]}")

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

    def emit_fn(self, fn, public):
        self.o = []
        self.cur = fn
        self.layout(fn)
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
            tail.append("    mov stderr(%rip), %rsi")
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


def assemble(prog):
    from passes import optimize
    optimize(prog)
    return Emit(prog).render()
