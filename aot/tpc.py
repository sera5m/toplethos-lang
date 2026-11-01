#!/usr/bin/env python3
"""Toplethos → passes → amd64 assembly → g++.

Slice: i32, + - * / %, assignment, print, do n. Not the whole language.
contract_inc_loops turns `do n[c] { x = x + k }` into one machine loop.
Everything else in the slice is still compiled. It is just a longer loop.
"""

import argparse
import subprocess
import sys
from dataclasses import dataclass

I32_MIN = -2147483648
I32_MAX = 2147483647


def i32(v):
    v &= 0xFFFFFFFF
    if v >= 0x80000000:
        v -= 0x100000000
    return v


def div_trunc(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def mod_trunc(a, b):
    return a - div_trunc(a, b) * b


class ParseError(Exception):
    pass


class CompileError(Exception):
    pass


@dataclass
class Const:
    v: int


@dataclass
class Name:
    n: str


@dataclass
class Bin:
    op: str
    a: object
    b: object


@dataclass
class Decl:
    name: str
    expr: object
    line: int


@dataclass
class Store:
    name: str
    expr: object
    line: int


@dataclass
class Print:
    expr: object
    line: int


@dataclass
class Do:
    count: object
    body: list
    line: int


@dataclass
class Inc:
    name: str
    k: int
    count: object
    line: int


class Parser:
    def __init__(self, src):
        self.s = src
        self.i = 0
        self.line = 1
        self.col = 1
        self.known = set()

    def fail(self, msg):
        raise ParseError(f"{self.line}:{self.col} {msg}")

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def get(self):
        c = self.peek()
        if not c:
            return ""
        self.i += 1
        if c == "\n":
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return c

    def skip(self):
        while True:
            c = self.peek()
            if not c:
                return
            if c in " \t\r\n":
                self.get()
                continue
            if c == "/" and self.s.startswith("//", self.i):
                while self.peek() not in ("", "\n"):
                    self.get()
                continue
            return

    def _ident_char(self, i, first):
        if i >= len(self.s):
            return False
        c = self.s[i]
        if first:
            return c.isalpha() or c == "_"
        return c.isalnum() or c == "_"

    def match_kw(self, kw):
        self.skip()
        if self.s.startswith(kw, self.i) and not self._ident_char(self.i + len(kw), False):
            for _ in kw:
                self.get()
            return True
        return False

    def expect(self, tok):
        self.skip()
        if not self.s.startswith(tok, self.i):
            self.fail(f"expected {tok}")
        for _ in tok:
            self.get()

    def ident(self):
        self.skip()
        if not self._ident_char(self.i, True):
            self.fail("expected a name")
        start = self.i
        self.get()
        while self._ident_char(self.i, False):
            self.get()
        return self.s[start:self.i]

    def number(self):
        start_line, start_col = self.line, self.col
        neg = False
        if self.peek() == "-":
            neg = True
            self.get()
        if not self.peek().isdigit():
            self.fail("expected a number")
        v = 0
        while self.peek().isdigit():
            v = v * 10 + ord(self.get()) - 48
        if neg:
            v = -v
        if v < I32_MIN or v > I32_MAX:
            raise ParseError(f"{start_line}:{start_col} integer does not fit in i32")
        return v

    def factor(self):
        self.skip()
        if self.peek() == "(":
            self.get()
            e = self.expr()
            self.expect(")")
            return e
        if self.peek() == "-":
            self.get()
            f = self.factor()
            if isinstance(f, Const):
                v = i32(-f.v)
                return Const(v)
            return Bin("-", Const(0), f)
        if self.peek().isdigit():
            return Const(self.number())
        name = self.ident()
        if name not in self.known:
            self.fail(f"'{name}' is used before it is declared")
        return Name(name)

    def term(self):
        e = self.factor()
        while True:
            self.skip()
            op = None
            for cand in ("*", "/", "%"):
                if self.peek() == cand:
                    op = cand
                    break
            if not op:
                return e
            self.get()
            e = Bin(op, e, self.factor())

    def expr(self):
        e = self.term()
        while True:
            self.skip()
            if self.peek() in "+-":
                op = self.get()
                e = Bin(op, e, self.term())
            else:
                return e

    def block(self):
        self.expect("{")
        body = []
        while True:
            self.skip()
            if self.peek() == "}":
                self.get()
                return body
            if not self.peek():
                self.fail("unclosed '{'")
            st = self.statement(allow_decl=False)
            if st is not None:
                body.append(st)

    def directive(self):
        self.ident()
        self.skip()
        if self.peek() == "-" or self.peek().isdigit():
            self.number()
        self.expect(";")
        return None

    def statement(self, allow_decl):
        line = self.line
        self.skip()
        if self.s.startswith("set_", self.i):
            return self.directive()
        if self.match_kw("i32"):
            if not allow_decl:
                self.fail("declare i32 at the top level, not inside do")
            name = self.ident()
            if name in self.known:
                self.fail(f"'{name}' is already declared")
            self.expect("=")
            expr = self.expr()
            self.expect(";")
            self.known.add(name)
            return Decl(name, expr, line)
        if self.match_kw("print"):
            self.expect("(")
            expr = self.expr()
            self.expect(")")
            self.expect(";")
            return Print(expr, line)
        if self.match_kw("do"):
            if not self.match_kw("n"):
                self.fail("expected 'do n[count]'")
            self.expect("[")
            count = self.expr()
            self.expect("]")
            body = self.block()
            return Do(count, body, line)
        name = self.ident()
        if name not in self.known:
            self.fail(f"'{name}' is used before it is declared")
        self.expect("=")
        expr = self.expr()
        self.expect(";")
        return Store(name, expr, line)

    def program(self):
        stmts = []
        while True:
            self.skip()
            if not self.peek():
                return stmts
            st = self.statement(allow_decl=True)
            if st is not None:
                stmts.append(st)


def fold(e, line):
    if isinstance(e, Bin):
        a = fold(e.a, line)
        b = fold(e.b, line)
        if isinstance(a, Const) and isinstance(b, Const):
            if e.op == "/" or e.op == "%":
                if b.v == 0:
                    raise CompileError(f"{line}: div0")
            if e.op == "+":
                return Const(i32(a.v + b.v))
            if e.op == "-":
                return Const(i32(a.v - b.v))
            if e.op == "*":
                return Const(i32(a.v * b.v))
            if e.op == "/":
                return Const(div_trunc(a.v, b.v))
            if e.op == "%":
                return Const(mod_trunc(a.v, b.v))
        return Bin(e.op, a, b)
    return e


def fold_stmts(stmts):
    out = []
    for s in stmts:
        if isinstance(s, Decl):
            out.append(Decl(s.name, fold(s.expr, s.line), s.line))
        elif isinstance(s, Store):
            out.append(Store(s.name, fold(s.expr, s.line), s.line))
        elif isinstance(s, Print):
            out.append(Print(fold(s.expr, s.line), s.line))
        elif isinstance(s, Do):
            out.append(Do(fold(s.count, s.line), fold_stmts(s.body), s.line))
        else:
            out.append(s)
    return out


def contract_inc_loops(stmts):
    """do n[c] { x = x + k } becomes one add-and-branch loop. k is a constant."""
    out = []
    for s in stmts:
        if isinstance(s, Do) and len(s.body) == 1 and isinstance(s.body[0], Store):
            st = s.body[0]
            e = st.expr
            if (
                isinstance(e, Bin)
                and e.op == "+"
                and isinstance(e.a, Name)
                and e.a.n == st.name
                and isinstance(e.b, Const)
            ):
                out.append(Inc(st.name, e.b.v, s.count, s.line))
                continue
        if isinstance(s, Do):
            out.append(Do(s.count, contract_inc_loops(s.body), s.line))
        else:
            out.append(s)
    return out


def show(stmts, ind=0):
    pad = "  " * ind
    lines = []
    for s in stmts:
        if isinstance(s, Decl):
            lines.append(f"{pad}i32 {s.name} = {show_e(s.expr)}")
        elif isinstance(s, Store):
            lines.append(f"{pad}{s.name} = {show_e(s.expr)}")
        elif isinstance(s, Print):
            lines.append(f"{pad}print {show_e(s.expr)}")
        elif isinstance(s, Do):
            lines.append(f"{pad}do n[{show_e(s.count)}]")
            lines.extend(show(s.body, ind + 1))
        elif isinstance(s, Inc):
            lines.append(f"{pad}inc {s.name} by {s.k} times {show_e(s.count)}")
        else:
            lines.append(f"{pad}{s}")
    return lines


def show_e(e):
    if isinstance(e, Const):
        return str(e.v)
    if isinstance(e, Name):
        return e.n
    if isinstance(e, Bin):
        return f"({show_e(e.a)} {e.op} {show_e(e.b)})"
    return "?"


REGS = [
    ("%ebx", "%rbx"),
    ("%r12d", "%r12"),
    ("%r13d", "%r13"),
    ("%r14d", "%r14"),
    ("%r15d", "%r15"),
]


class Asm:
    def __init__(self, slots):
        self.slot = slots
        self.o = []
        self.nlab = 0
        self.needs_printf = False
        self.needs_div0 = False
        self.needs_ctr = False
        self.depth = 0
        self.contracted = 0

    def lab(self):
        self.nlab += 1
        return f".L{self.nlab}"

    def w(self, s):
        self.o.append(s)

    def gen_expr(self, e):
        if isinstance(e, Const):
            self.w(f"    mov ${e.v}, %eax")
            return
        if isinstance(e, Name):
            self.w(f"    mov {self.slot[e.n]}, %eax")
            return
        if isinstance(e, Bin):
            self.gen_expr(e.a)
            self.w("    push %rax")
            self.gen_expr(e.b)
            self.w("    mov %eax, %ecx")
            self.w("    pop %rax")
            if e.op == "+":
                self.w("    add %ecx, %eax")
            elif e.op == "-":
                self.w("    sub %ecx, %eax")
            elif e.op == "*":
                self.w("    imul %ecx, %eax")
            elif e.op in ("/", "%"):
                nz = self.lab()
                self.w("    test %ecx, %ecx")
                self.w("    jnz " + nz)
                self.w("    jmp .Ldiv0")
                self.needs_div0 = True
                self.w(nz + ":")
                self.w("    cdq")
                self.w("    idiv %ecx")
                if e.op == "%":
                    self.w("    mov %edx, %eax")
            return
        raise CompileError("bad expression")

    def gen_store(self, s):
        e = s.expr
        reg = self.slot[s.name]
        if (
            isinstance(e, Bin)
            and e.op == "+"
            and isinstance(e.a, Name)
            and e.a.n == s.name
            and isinstance(e.b, Const)
        ):
            self.w(f"    add ${i32(e.b.v)}, {reg}")
            return
        if (
            isinstance(e, Bin)
            and e.op == "-"
            and isinstance(e.a, Name)
            and e.a.n == s.name
            and isinstance(e.b, Const)
        ):
            self.w(f"    sub ${i32(e.b.v)}, {reg}")
            return
        self.gen_expr(e)
        self.w(f"    mov %eax, {reg}")

    def gen_inc(self, s):
        self.contracted += 1
        top, skip = self.lab(), self.lab()
        self.gen_expr(s.count)
        self.w("    mov %eax, %ecx")
        self.w("    test %ecx, %ecx")
        self.w(f"    jle {skip}")
        self.w("    .p2align 5")
        self.w(f"{top}:")
        reg = self.slot[s.name]
        k = i32(s.k)
        if k != 0:
            if k >= 0:
                self.w(f"    add ${k}, {reg}")
            else:
                self.w(f"    sub ${-k}, {reg}")
        self.w("    sub $1, %ecx")
        self.w(f"    jne {top}")
        self.w(f"{skip}:")

    def gen_do(self, s):
        self.needs_ctr = True
        top, skip = self.lab(), self.lab()
        self.gen_expr(s.count)
        if self.depth:
            self.w("    mov ctr(%rip), %ecx")
            self.w("    push %rcx")
        self.w("    mov %eax, ctr(%rip)")
        self.w("    test %eax, %eax")
        self.w(f"    jle {skip}")
        self.w("    .p2align 5")
        self.w(f"{top}:")
        self.depth += 1
        self.gen_stmts(s.body)
        self.depth -= 1
        self.w("    subl $1, ctr(%rip)")
        self.w(f"    jnz {top}")
        self.w(f"{skip}:")
        if self.depth:
            self.w("    pop %rcx")
            self.w("    mov %ecx, ctr(%rip)")

    def gen_stmts(self, stmts):
        for s in stmts:
            if isinstance(s, Decl) or isinstance(s, Store):
                self.gen_store(s)
            elif isinstance(s, Print):
                self.needs_printf = True
                self.gen_expr(s.expr)
                self.w("    mov %eax, %esi")
                self.w("    lea fmt(%rip), %rdi")
                self.w("    xor %eax, %eax")
                self.w("    call printf@PLT")
            elif isinstance(s, Inc):
                self.gen_inc(s)
            elif isinstance(s, Do):
                self.gen_do(s)
            else:
                raise CompileError(f"cannot lower {type(s).__name__}")

    def render(self, stmts):
        self.gen_stmts(stmts)
        body = self.o
        used = [REGS[i][1] for i in range(len(self.slot))]
        head = [
            "    .text",
            "    .globl main",
            "main:",
        ]
        for reg in used:
            head.append(f"    push {reg}")
        # Process entry has rsp 8 mod 16. An even number of pushes leaves it there.
        pad = len(used) % 2 == 0
        if pad:
            head.append("    sub $8, %rsp")
        tail = []
        if pad:
            tail.append("    add $8, %rsp")
        for reg in reversed(used):
            tail.append(f"    pop {reg}")
        tail += ["    xor %eax, %eax", "    ret"]
        extra = []
        if self.needs_div0:
            extra += [
                ".Ldiv0:",
                "    mov $1, %eax",
                "    mov $1, %edi",
                "    lea d0(%rip), %rsi",
                "    mov $5, %edx",
                "    syscall",
                "    mov $60, %eax",
                "    mov $1, %edi",
                "    syscall",
            ]
        ro = []
        if self.needs_printf or self.needs_div0:
            ro.append("    .section .rodata")
        if self.needs_printf:
            ro.append('fmt: .string "%d\\n"')
        if self.needs_div0:
            ro.append('d0: .ascii "div0\\n"')
        bss = []
        if self.needs_ctr:
            bss += ["    .bss", "    .align 4", "ctr: .zero 4"]
        note = [
            f"# pipeline: parse, fold_constants, contract_inc_loops, amd64, g++",
            f"# contract_inc_loops fired {self.contracted} time(s)",
        ]
        return "\n".join(note + ro + bss + head + body + extra + tail) + "\n"


def slots_of(stmts, acc):
    for s in stmts:
        if isinstance(s, Decl):
            if s.name not in acc:
                acc.append(s.name)
        elif isinstance(s, Do):
            slots_of(s.body, acc)


def compile_source(src):
    prog = Parser(src).program()
    folded = fold_stmts(prog)
    contracted = contract_inc_loops(folded)
    names = []
    slots_of(contracted, names)
    if len(names) > len(REGS):
        raise CompileError(
            f"this slice keeps {len(REGS)} i32 locals in registers, this program has {len(names)}"
        )
    slot = {name: REGS[i][0] for i, name in enumerate(names)}
    asm = Asm(slot)
    text = asm.render(contracted)
    return contracted, text


def main(argv):
    ap = argparse.ArgumentParser(description="Compile a Toplethos slice to assembly and link it with g++.")
    ap.add_argument("source")
    ap.add_argument("-o", default="a.out", help="binary to write (assembly is written beside it as .s)")
    ap.add_argument("--asm-only", action="store_true", help="write the .s file and do not call g++")
    ap.add_argument("--dump", action="store_true", help="print the IR after the passes")
    ap.add_argument("--run", action="store_true", help="run the binary after g++ links it")
    args = ap.parse_args(argv)
    try:
        src = open(args.source, encoding="utf-8").read()
    except OSError as e:
        print(f"tpc: {e}", file=sys.stderr)
        return 1
    try:
        ir, text = compile_source(src)
    except (ParseError, CompileError) as e:
        print(f"tpc: {args.source}: {e}", file=sys.stderr)
        return 1
    if args.dump:
        print("\n".join(show(ir)), file=sys.stderr)
    asm_path = args.o + ".s"
    try:
        with open(asm_path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError as e:
        print(f"tpc: {e}", file=sys.stderr)
        return 1
    if args.asm_only:
        print(asm_path)
        return 0
    cmd = ["g++", "-o", args.o, asm_path]
    print(" ".join(cmd), file=sys.stderr)
    g = subprocess.run(cmd, text=True, capture_output=True)
    if g.returncode != 0:
        sys.stderr.write(g.stderr)
        return g.returncode or 1
    if not args.run:
        return 0
    r = subprocess.run([args.o])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
