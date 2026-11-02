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
from pathlib import Path

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


@dataclass
class StrLit:
    s: str


@dataclass
class Index:
    base: object
    idx: object


@dataclass
class SizeOf:
    expr: object


@dataclass
class Call:
    name: str
    args: list


@dataclass
class Cmp:
    op: str
    a: object
    b: object


@dataclass
class Logic:
    op: str
    a: object
    b: object


@dataclass
class Not:
    a: object


@dataclass
class DeclStr:
    name: str
    expr: object
    line: int


@dataclass
class DeclMap:
    name: str
    line: int


@dataclass
class If:
    cond: object
    body: list
    els: list
    line: int


@dataclass
class Loop:
    body: list
    line: int


@dataclass
class Leave:
    line: int


@dataclass
class CallStmt:
    name: str
    args: list
    line: int


class Parser:
    def __init__(self, src):
        self.s = src
        self.i = 0
        self.line = 1
        self.col = 1
        self.known = {}

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

    def string_lit(self):
        self.get()
        chars = []
        while True:
            c = self.get()
            if c == '"':
                return "".join(chars)
            if c == "":
                self.fail("unclosed string")
            if c == "\\":
                n = self.get()
                esc = {"n": "\n", "t": "\t", "\\": "\\", '"': '"'}
                if n not in esc:
                    self.fail("bad escape")
                chars.append(esc[n])
            else:
                chars.append(c)

    def char_lit(self):
        self.get()
        c = self.get()
        if c == "":
            self.fail("unclosed char")
        if c == "\\":
            n = self.get()
            esc = {"n": "\n", "t": "\t", "\\": "\\", "'": "'"}
            if n not in esc:
                self.fail("bad escape")
            c = esc[n]
        if self.peek() != "'":
            self.fail("unclosed char")
        self.get()
        return Const(ord(c))

    def match_op(self, tok):
        self.skip()
        if not self.s.startswith(tok, self.i):
            return False
        if tok in ("<", ">") and self.s.startswith(tok + "=", self.i):
            return False
        if tok == "!" and self.s.startswith("!=", self.i):
            return False
        if tok == "&" or tok == "|":
            return False
        for _ in range(len(tok)):
            self.get()
        return True

    def arglist(self):
        self.expect("[")
        args = []
        self.skip()
        if self.peek() != "]":
            while True:
                args.append(self.expr())
                self.skip()
                if self.peek() == ",":
                    self.get()
                    continue
                break
        self.expect("]")
        return args

    def postfix(self, e):
        self.skip()
        if self.peek() == "[":
            self.get()
            idx = self.expr()
            self.expect("]")
            return Index(e, idx)
        return e

    def factor(self):
        self.skip()
        if self.peek() == "(":
            self.get()
            e = self.expr()
            self.expect(")")
            return self.postfix(e)
        if self.peek() == "!" and not self.s.startswith("!=", self.i):
            self.get()
            return Not(self.factor())
        if self.peek() == '"':
            return StrLit(self.string_lit())
        if self.peek() == "'":
            return self.char_lit()
        if self.peek() == "-":
            self.get()
            f = self.factor()
            if isinstance(f, Const):
                return Const(i32(-f.v))
            return Bin("-", Const(0), f)
        if self.peek().isdigit():
            return Const(self.number())
        if self.match_kw("size"):
            if not self.match_kw("in"):
                self.fail("expected size in[value]")
            self.expect("[")
            inner = self.expr()
            self.expect("]")
            return SizeOf(inner)
        name = self.ident()
        if self.match_kw("in"):
            return Call(name, self.arglist())
        if name not in self.known:
            self.fail(f"'{name}' is used before it is declared")
        return self.postfix(Name(name))

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

    def add_expr(self):
        e = self.term()
        while True:
            self.skip()
            if self.peek() in "+-":
                op = self.get()
                e = Bin(op, e, self.term())
            else:
                return e

    def cmp_expr(self):
        e = self.add_expr()
        for tok in ("<=", ">=", "==", "!=", "<", ">"):
            if self.match_op(tok):
                return Cmp(tok, e, self.add_expr())
        return e

    def and_expr(self):
        e = self.cmp_expr()
        while self.match_op("&&"):
            e = Logic("&&", e, self.cmp_expr())
        return e

    def expr(self):
        e = self.and_expr()
        while self.match_op("||"):
            e = Logic("||", e, self.and_expr())
        return e

    def block(self, allow_decl=False):
        self.expect("{")
        body = []
        while True:
            self.skip()
            if self.peek() == "}":
                self.get()
                return body
            if not self.peek():
                self.fail("unclosed '{'")
            st = self.statement(allow_decl=allow_decl)
            if st is not None:
                body.append(st)

    def directive(self):
        # set_step_depth and the other set_* lines are interpreter budgets.
        # This pipeline has no step counter, so the number is discarded.
        # It is not an i32. 5000000000 is a legal budget and an illegal i32.
        self.ident()
        self.skip()
        if self.peek() == "-" or self.peek().isdigit():
            if self.peek() == "-":
                self.get()
            if not self.peek().isdigit():
                self.fail("expected a number")
            while self.peek().isdigit():
                self.get()
        self.expect(";")
        return None

    def bind(self, kind, line, allow_decl):
        if not allow_decl:
            self.fail(f"declare {kind} at the top level, not inside do")
        name = self.ident()
        if name in self.known:
            self.fail(f"'{name}' is already declared")
        if kind == "map":
            self.expect(";")
            self.known[name] = kind
            return DeclMap(name, line)
        self.expect("=")
        expr = self.expr()
        self.expect(";")
        self.known[name] = kind
        if kind == "string":
            return DeclStr(name, expr, line)
        return Decl(name, expr, line)

    def statement(self, allow_decl):
        line = self.line
        self.skip()
        if self.s.startswith("set_", self.i):
            return self.directive()
        if self.match_kw("i32"):
            return self.bind("i32", line, allow_decl)
        if self.match_kw("string"):
            return self.bind("string", line, allow_decl)
        if self.match_kw("map"):
            return self.bind("map", line, allow_decl)
        if self.match_kw("print"):
            self.expect("(")
            expr = self.expr()
            self.expect(")")
            self.expect(";")
            return Print(expr, line)
        if self.match_kw("if"):
            cond = self.expr()
            body = self.block(allow_decl=True)
            els = []
            self.skip()
            if self.match_kw("else"):
                els = self.block(allow_decl=True)
            return If(cond, body, els, line)
        if self.match_kw("loop"):
            return Loop(self.block(allow_decl=True), line)
        if self.match_kw("leave"):
            self.expect(";")
            return Leave(line)
        if self.match_kw("do"):
            if not self.match_kw("n"):
                self.fail("expected 'do n[count]'")
            self.expect("[")
            count = self.expr()
            self.expect("]")
            body = self.block(allow_decl=False)
            return Do(count, body, line)
        name = self.ident()
        if self.match_kw("in"):
            args = self.arglist()
            self.expect(";")
            return CallStmt(name, args, line)
        if name not in self.known:
            self.fail(f"'{name}' is used before it is declared")
        if self.known[name] != "i32":
            self.fail(f"'{name}' is not an i32")
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
        elif isinstance(s, If):
            lines.append(f"{pad}if {show_e(s.cond)}")
            lines.extend(show(s.body, ind + 1))
            if s.els:
                lines.append(f"{pad}else")
                lines.extend(show(s.els, ind + 1))
        elif isinstance(s, Loop):
            lines.append(f"{pad}loop")
            lines.extend(show(s.body, ind + 1))
        elif isinstance(s, Leave):
            lines.append(f"{pad}leave")
        elif isinstance(s, DeclStr):
            lines.append(f"{pad}string {s.name} = {show_e(s.expr)}")
        elif isinstance(s, DeclMap):
            lines.append(f"{pad}map {s.name}")
        elif isinstance(s, CallStmt):
            lines.append(f"{pad}{s.name} in[{', '.join(show_e(a) for a in s.args)}]")
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
    if isinstance(e, StrLit):
        return '"' + e.s + '"'
    if isinstance(e, Index):
        return f"{show_e(e.base)}[{show_e(e.idx)}]"
    if isinstance(e, SizeOf):
        return f"size in[{show_e(e.expr)}]"
    if isinstance(e, Call):
        return f"{e.name} in[{', '.join(show_e(a) for a in e.args)}]"
    if isinstance(e, Cmp):
        return f"({show_e(e.a)} {e.op} {show_e(e.b)})"
    if isinstance(e, Logic):
        return f"({show_e(e.a)} {e.op} {show_e(e.b)})"
    if isinstance(e, Not):
        return f"!{show_e(e.a)}"
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
        return "\n".join(note + ro + bss + head + body + extra + tail) + "\n    .section .note.GNU-stack,\"\",@progbits\n"


def slots_of(stmts, acc):
    for s in stmts:
        if isinstance(s, Decl):
            if s.name not in acc:
                acc.append(s.name)
        elif isinstance(s, Do):
            slots_of(s.body, acc)
        elif isinstance(s, If):
            slots_of(s.body, acc)
            slots_of(s.els, acc)
        elif isinstance(s, Loop):
            slots_of(s.body, acc)


def expr_mem(e):
    if isinstance(e, (StrLit, Index, SizeOf, Call, Cmp, Logic, Not)):
        return True
    if isinstance(e, Bin):
        return expr_mem(e.a) or expr_mem(e.b)
    return False


def uses_mem(stmts):
    for s in stmts:
        if isinstance(s, (If, Loop, Leave, CallStmt, DeclStr, DeclMap)):
            return True
        if isinstance(s, (Decl, Store, Print, Do, Inc)) and expr_mem(getattr(s, "expr", None) or getattr(s, "count", None) or 0):
            return True
        if isinstance(s, Do) and uses_mem(s.body):
            return True
    return False


def collect_slots(stmts, order, kinds):
    for s in stmts:
        if isinstance(s, Decl):
            if s.name not in kinds:
                kinds[s.name] = "i32"
                order.append(s.name)
        elif isinstance(s, DeclStr):
            if s.name not in kinds:
                kinds[s.name] = "str"
                order.append(s.name)
        elif isinstance(s, DeclMap):
            if s.name not in kinds:
                kinds[s.name] = "map"
                order.append(s.name)
        elif isinstance(s, If):
            collect_slots(s.body, order, kinds)
            collect_slots(s.els, order, kinds)
        elif isinstance(s, (Loop, Do)):
            collect_slots(s.body, order, kinds)


class MemAsm:
    """Stack slots. Used when the program has strings, maps, or loops.
    The i32 register path stays untouched for the increment benchmark."""

    def __init__(self, order, kinds):
        self.kinds = kinds
        self.off = {}
        cursor = 32
        for name in order:
            if kinds[name] == "str":
                cursor += 16
            else:
                cursor += 8
            self.off[name] = -cursor
        self.frame = (cursor + 15) & ~15
        self.o = []
        self.nlab = 0
        self.lits = []
        self.loops = []
        self.needs_bounds = False
        self.needs_rt = False
        self.needs_printf = False

    def lab(self):
        self.nlab += 1
        return f".L{self.nlab}"

    def w(self, s):
        self.o.append(s)

    def lit(self, s):
        lab = f".LS{len(self.lits)}"
        self.lits.append((lab, s))
        return lab

    def gen_i32(self, e):
        if isinstance(e, Const):
            self.w(f"    mov ${i32(e.v)}, %eax")
            return
        if isinstance(e, Name):
            if self.kinds.get(e.n) != "i32":
                raise CompileError(f"'{e.n}' is not an i32")
            self.w(f"    mov {self.off[e.n]}(%rbp), %eax")
            return
        if isinstance(e, Bin):
            self.gen_i32(e.a)
            self.w("    mov %eax, -8(%rbp)")
            self.gen_i32(e.b)
            self.w("    mov %eax, %ecx")
            self.w("    mov -8(%rbp), %eax")
            if e.op == "+":
                self.w("    add %ecx, %eax")
            elif e.op == "-":
                self.w("    sub %ecx, %eax")
            elif e.op == "*":
                self.w("    imul %ecx, %eax")
            elif e.op in ("/", "%"):
                nz = self.lab()
                self.w("    test %ecx, %ecx")
                self.w(f"    jnz {nz}")
                self.w("    jmp .Ldiv0")
                self.w(nz + ":")
                self.w("    cdq")
                self.w("    idiv %ecx")
                if e.op == "%":
                    self.w("    mov %edx, %eax")
            else:
                raise CompileError(f"bad op {e.op}")
            return
        if isinstance(e, Index):
            self.gen_i32(e.idx)
            self.w("    mov %eax, -16(%rbp)")
            self.gen_str(e.base)
            self.w("    movslq -16(%rbp), %rcx")
            self.w("    test %rcx, %rcx")
            self.w("    jl .Lbounds")
            self.w("    cmp %rdx, %rcx")
            self.w("    jae .Lbounds")
            self.w("    movzbl (%rax, %rcx), %eax")
            self.needs_bounds = True
            return
        if isinstance(e, SizeOf):
            self.gen_str(e.expr)
            self.w("    mov %edx, %eax")
            return
        if isinstance(e, Cmp):
            self.gen_i32(e.a)
            self.w("    mov %eax, -8(%rbp)")
            self.gen_i32(e.b)
            self.w("    mov %eax, %ecx")
            self.w("    mov -8(%rbp), %eax")
            self.w("    cmp %ecx, %eax")
            setcc = {"<": "setl", ">": "setg", "<=": "setle", ">=": "setge", "==": "sete", "!=": "setne"}
            self.w(f"    {setcc[e.op]} %al")
            self.w("    movzbl %al, %eax")
            return
        if isinstance(e, Not):
            self.gen_i32(e.a)
            self.w("    test %eax, %eax")
            self.w("    setz %al")
            self.w("    movzbl %al, %eax")
            return
        if isinstance(e, Logic):
            no = self.lab()
            end = self.lab()
            self.gen_i32(e.a)
            self.w("    test %eax, %eax")
            if e.op == "&&":
                self.w(f"    jz {no}")
                self.gen_i32(e.b)
                self.w("    test %eax, %eax")
                self.w(f"    jz {no}")
                self.w("    mov $1, %eax")
                self.w(f"    jmp {end}")
                self.w(no + ":")
                self.w("    xor %eax, %eax")
                self.w(end + ":")
            else:
                self.w(f"    jnz {no}")
                self.gen_i32(e.b)
                self.w("    test %eax, %eax")
                self.w(f"    jnz {no}")
                self.w("    xor %eax, %eax")
                self.w(f"    jmp {end}")
                self.w(no + ":")
                self.w("    mov $1, %eax")
                self.w(end + ":")
            return
        raise CompileError("expected an i32")

    def gen_str(self, e):
        if isinstance(e, StrLit):
            lab = self.lit(e.s)
            self.w(f"    lea {lab}(%rip), %rax")
            self.w(f"    mov ${len(e.s.encode())}, %rdx")
            return
        if isinstance(e, Name):
            if self.kinds.get(e.n) != "str":
                raise CompileError(f"'{e.n}' is not a string")
            off = self.off[e.n]
            self.w(f"    mov {off}(%rbp), %rax")
            self.w(f"    mov {off + 8}(%rbp), %rdx")
            return
        if isinstance(e, Call) and e.name == "readfile":
            if len(e.args) != 1:
                raise CompileError("readfile in[path]")
            self.gen_str(e.args[0])
            self.w("    mov %rax, %rdi")
            self.w("    call tp_readfile@PLT")
            self.needs_rt = True
            return
        raise CompileError("expected a string")

    def gen_stmts(self, stmts):
        for s in stmts:
            if isinstance(s, Decl) or isinstance(s, Store):
                self.gen_i32(s.expr)
                self.w(f"    mov %eax, {self.off[s.name]}(%rbp)")
            elif isinstance(s, DeclStr):
                self.gen_str(s.expr)
                off = self.off[s.name]
                self.w(f"    mov %rax, {off}(%rbp)")
                self.w(f"    mov %rdx, {off + 8}(%rbp)")
            elif isinstance(s, DeclMap):
                self.w("    call tp_map_new@PLT")
                self.w(f"    mov %rax, {self.off[s.name]}(%rbp)")
                self.needs_rt = True
            elif isinstance(s, Print):
                if isinstance(s.expr, StrLit):
                    lab = self.lit(s.expr.s)
                    self.w(f"    lea {lab}(%rip), %rdi")
                    self.w("    call puts@PLT")
                else:
                    self.needs_printf = True
                    self.gen_i32(s.expr)
                    self.w("    mov %eax, %esi")
                    self.w("    lea fmt(%rip), %rdi")
                    self.w("    xor %eax, %eax")
                    self.w("    call printf@PLT")
            elif isinstance(s, If):
                no = self.lab()
                end = self.lab()
                self.gen_i32(s.cond)
                self.w("    test %eax, %eax")
                self.w(f"    jz {no}")
                self.gen_stmts(s.body)
                self.w(f"    jmp {end}")
                self.w(no + ":")
                self.gen_stmts(s.els)
                self.w(end + ":")
            elif isinstance(s, Loop):
                top, end = self.lab(), self.lab()
                self.loops.append(end)
                self.w(top + ":")
                self.gen_stmts(s.body)
                self.w(f"    jmp {top}")
                self.w(end + ":")
                self.loops.pop()
            elif isinstance(s, Leave):
                if not self.loops:
                    raise CompileError(f"{s.line}: leave outside loop")
                self.w(f"    jmp {self.loops[-1]}")
            elif isinstance(s, CallStmt):
                self.gen_call(s)
            else:
                raise CompileError(f"cannot lower {type(s).__name__} in this program")

    def gen_call(self, s):
        self.needs_rt = True
        if s.name == "map_print":
            if len(s.args) != 1 or not isinstance(s.args[0], Name):
                raise CompileError("map_print in[map]")
            self.w(f"    mov {self.off[s.args[0].n]}(%rbp), %rdi")
            self.w("    call tp_map_print@PLT")
            return
        if s.name == "map_add":
            if len(s.args) != 4:
                raise CompileError("map_add in[map, text, start, end]")
            self.gen_i32(s.args[2])
            self.w("    mov %eax, -16(%rbp)")
            self.gen_i32(s.args[3])
            self.w("    mov %eax, -24(%rbp)")
            self.gen_str(s.args[1])
            self.w("    mov %rax, -32(%rbp)")
            if not isinstance(s.args[0], Name) or self.kinds.get(s.args[0].n) != "map":
                raise CompileError("map_add needs a map")
            self.w(f"    mov {self.off[s.args[0].n]}(%rbp), %rdi")
            self.w("    mov -32(%rbp), %rsi")
            self.w("    movslq -16(%rbp), %rdx")
            self.w("    movslq -24(%rbp), %rcx")
            self.w("    call tp_map_add@PLT")
            return
        raise CompileError(f"unknown call {s.name}")

    def render(self, stmts):
        self.gen_stmts(stmts)
        body = self.o
        ro = ["    .section .rodata"]
        if self.needs_printf:
            ro.append('fmt: .string "%d\\n"')
        if self.needs_bounds:
            ro.append('bmsg: .ascii "bounds\\n"')
        for lab, s in self.lits:
            data = ",".join(str(b) for b in s.encode()) + ",0"
            ro.append(f"{lab}: .byte {data}")
        head = [
            "    .text",
            "    .globl main",
            "main:",
            "    push %rbp",
            "    mov %rsp, %rbp",
            f"    sub ${self.frame}, %rsp",
        ]
        if self.frame:
            head += [
                f"    lea -{self.frame}(%rbp), %rdi",
                f"    mov ${self.frame}, %ecx",
                "    xor %eax, %eax",
                "    rep stosb",
            ]
        tail = ["    xor %eax, %eax", "    leave", "    ret"]
        extra = []
        if self.needs_bounds:
            extra += [
                "    jmp .Lret",
                ".Lbounds:",
                "    mov $1, %eax",
                "    mov $1, %edi",
                "    lea bmsg(%rip), %rsi",
                "    mov $7, %edx",
                "    syscall",
                "    mov $60, %eax",
                "    mov $1, %edi",
                "    syscall",
                ".Lret:",
            ]
        note = ["# pipeline: parse, fold_constants, contract_inc_loops, amd64, g++"]
        return "\n".join(note + ro + head + body + extra + tail) + "\n    .section .note.GNU-stack,\"\",@progbits\n", self.needs_rt


def compile_source(src):
    import re
    if re.search(r"(?m)^\s*fn\s+main\b", src):
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lang"))
        from emit import assemble
        from front import CompileError as FrontError
        from front import parse as front_parse
        try:
            prog = front_parse(src)
        except FrontError as e:
            raise CompileError(str(e))
        return ["USE.md core"], assemble(prog), "desktop"
    head = "\n" + src
    if "\nfn " in head or "\nobject " in head or "\nf64 " in head:
        import feat
        return feat.compile_feat(src)
    prog = Parser(src).program()
    folded = fold_stmts(prog)
    contracted = contract_inc_loops(folded)
    if uses_mem(contracted):
        order, kinds = [], {}
        collect_slots(contracted, order, kinds)
        asm = MemAsm(order, kinds)
        text, needs_rt = asm.render(contracted)
        return contracted, text, needs_rt
    names = []
    slots_of(contracted, names)
    if len(names) > len(REGS):
        raise CompileError(
            f"this slice keeps {len(REGS)} i32 locals in registers, this program has {len(names)}"
        )
    slot = {name: REGS[i][0] for i, name in enumerate(names)}
    asm = Asm(slot)
    text = asm.render(contracted)
    return contracted, text, False



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
        ir, text, needs_rt = compile_source(src)
    except (ParseError, CompileError) as e:
        print(f"tpc: {args.source}: {e}", file=sys.stderr)
        return 1
    if args.dump:
        if isinstance(ir, list) and ir and isinstance(ir[0], str):
            print("\n".join(ir), file=sys.stderr)
        else:
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
    if needs_rt == "battery":
        cmd = ["g++", "-O2", "-pthread", "-o", args.o, asm_path, str(Path(__file__).with_name("batteryrt.c"))]
    elif needs_rt == "desktop":
        root = Path(__file__).resolve().parents[1]
        cmd = ["g++", "-pthread", "-o", args.o, asm_path, str(root / "lang" / "desktop.c")]
    elif needs_rt:
        cmd = ["g++", "-O2", "-o", args.o, asm_path, str(Path(__file__).with_name("wordrt.c"))]
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
