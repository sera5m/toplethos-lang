"""Lower the battery slice: f64, functions, and objects with methods.

Not the whole language. direct / call / object in battery.tpth_s.
"""

import struct


class CompileError(Exception):
    pass


class Parser:
    def __init__(self, src):
        self.s = src
        self.i = 0
        self.line = 1
        self.col = 1
        self.scopes = [{}]
        self.in_method = False
        self.fns = []
        self.objects = {}

    def fail(self, msg):
        raise CompileError(f"{self.line}:{self.col} {msg}")

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

    def starts(self, tok):
        if not self.s.startswith(tok, self.i):
            return False
        nxt = self.i + len(tok)
        if tok[-1].isalnum() or tok[-1] == "_":
            if nxt < len(self.s) and (self.s[nxt].isalnum() or self.s[nxt] == "_"):
                return False
        return True

    def eat(self, tok):
        self.skip()
        if tok in ("<", ">") and self.starts(tok + "="):
            return False
        if tok == "!" and self.starts("!="):
            return False
        if not self.starts(tok):
            return False
        for _ in tok:
            self.get()
        return True

    def expect(self, tok):
        if not self.eat(tok):
            self.fail(f"expected {tok}")

    def ident(self):
        self.skip()
        if not (self.peek().isalpha() or self.peek() == "_"):
            self.fail("expected a name")
        start = self.i
        self.get()
        while self.peek().isalnum() or self.peek() == "_":
            self.get()
        return self.s[start:self.i]

    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        return None

    def bind(self, name, kind):
        if name in self.scopes[-1]:
            self.fail(f"'{name}' is already declared")
        self.scopes[-1][name] = kind

    def number(self):
        self.skip()
        start = self.i
        if not self.peek().isdigit():
            self.fail("expected a number")
        while self.peek().isdigit():
            self.get()
        if self.peek() == "." and self.i + 1 < len(self.s) and self.s[self.i + 1].isdigit():
            self.get()
            while self.peek().isdigit():
                self.get()
            return ("f64", float(self.s[start:self.i]))
        v = int(self.s[start:self.i])
        if v > 2147483647:
            self.fail("integer does not fit in i32")
        return ("i32", v)

    def primary(self):
        self.skip()
        if self.eat("("):
            e = self.cond()
            self.expect(")")
            return e
        if self.eat("as"):
            self.expect("[")
            ty = self.ident()
            self.expect(",")
            inner = self.expr()
            self.expect("]")
            if ty != "f64":
                self.fail("only as[f64, i32] in this slice")
            return ("as", ty, inner)
        if self.peek() == '"':
            return ("str", self.string())
        if self.peek().isdigit():
            return self.number()
        name = self.ident()
        self.skip()
        if name != "HERE" and self.lookup(name) is None and self.peek() != "." and not self.starts("in"):
            self.fail(f"'{name}' is used before it is declared")
        base = ("here",) if name == "HERE" else ("name", name)
        if self.peek() == ".":
            self.get()
            field = self.ident()
            self.skip()
            if self.eat("in"):
                return ("method", base, field, self.arglist())
            return ("field", base, field)
        if self.eat("in"):
            return ("call", name, self.arglist())
        return base

    def eat_ahead_in(self):
        # called after the name is consumed. Look for `in`.
        return False

    def string(self):
        self.get()
        chars = []
        while True:
            c = self.get()
            if c == '"':
                return "".join(chars)
            if c == "":
                self.fail("unclosed string")
            chars.append(c)

    def arglist(self):
        self.expect("[")
        args = []
        self.skip()
        if not self.starts("]"):
            while True:
                args.append(self.expr())
                self.skip()
                if self.eat(","):
                    continue
                break
        self.expect("]")
        return args

    def unary(self):
        return self.primary()

    def term(self):
        e = self.unary()
        while True:
            self.skip()
            op = None
            for cand in ("*", "/"):
                if self.starts(cand):
                    op = cand
                    break
            if not op:
                return e
            self.get()
            e = ("bin", op, e, self.unary())

    def expr(self):
        e = self.term()
        while True:
            self.skip()
            if self.peek() in "+-" and not (self.peek() == "-" and False):
                # a trailing name or number, not a comment
                if self.peek() == "-" or self.peek() == "+":
                    op = self.get()
                    e = ("bin", op, e, self.term())
                    continue
            return e

    def cond(self):
        if self.eat("!"):
            return ("not", self.cond())
        a = self.expr()
        for op in ("<=", ">=", "==", "!=", "<", ">"):
            if self.eat(op):
                return ("cmp", op, a, self.expr())
        return a

    def block(self):
        self.expect("{")
        body = []
        while True:
            self.skip()
            if self.eat("}"):
                return body
            if not self.peek():
                self.fail("unclosed '{'")
            body.append(self.statement())

    def ty(self):
        name = self.ident()
        if name not in ("f64", "i32"):
            self.fail(f"'{name}' is not a type in this slice")
        return name

    def params(self):
        self.expect("[")
        out = []
        self.skip()
        if not self.starts("]"):
            while True:
                ty = self.ty()
                name = self.ident()
                out.append((ty, name))
                self.skip()
                if self.eat(","):
                    continue
                break
        self.expect("]")
        return out

    def statement(self):
        self.skip()
        return self.statement_at(self.line)

    def statement_at(self, line):
        if self.eat("return"):
            if self.eat(";"):
                return ("return", None, line)
            e = self.expr()
            self.expect(";")
            return ("return", e, line)
        if self.eat("leave"):
            self.expect(";")
            return ("leave", line)
        if self.eat("if"):
            c = self.cond()
            body = self.block()
            els = []
            self.skip()
            if self.eat("else"):
                els = self.block()
            return ("if", c, body, els, line)
        if self.eat("loop"):
            return ("loop", self.block(), line)
        if self.starts("f64") or self.starts("i32"):
            ty = self.ty()
            name = self.ident()
            self.expect("=")
            e = self.expr()
            self.expect(";")
            self.bind(name, ty)
            return ("decl", ty, name, e, line)
        name = self.ident()
        self.skip()
        if self.peek() == ".":
            self.get()
            field = self.ident()
            self.expect("=")
            e = self.expr()
            self.expect(";")
            return ("fset", name, field, e, line)
        if self.eat("in"):
            args = self.arglist()
            self.expect(";")
            return ("callstmt", name, args, line)
        if self.eat(";"):
            kind = self.lookup(name)
            if kind is None and name not in self.objects and name not in [o[0] for o in self.objects_list()]:
                # object type name used as instance: `Poly p` was parsed as name then we needed the second ident.
                self.fail(f"'{name}' is not a statement")
            return ("nop",)
        # `Poly p;` — we consumed Poly and expected more. The second ident is still ahead if we didn't eat `;`.
        if self.lookup(name) is None and self.peek().isalpha():
            inst = self.ident()
            self.expect(";")
            self.bind(inst, ("obj", name))
            return ("odecl", name, inst, line)
        self.expect("=")
        e = self.expr()
        self.expect(";")
        if self.lookup(name) is None:
            self.fail(f"'{name}' is used before it is declared")
        return ("store", name, e, line)

    def objects_list(self):
        return self.objects

    def function(self, method_of=None):
        name = self.ident()
        self.expect("in")
        params = self.params()
        self.expect("out")
        self.expect("[")
        self.skip()
        if self.eat("]"):
            out = None
        else:
            out = self.ty()
            self.expect("]")
        self.scopes.append({})
        prev = self.in_method
        self.in_method = method_of is not None
        for ty, pname in params:
            self.bind(pname, ty)
        if method_of:
            self.bind("HERE", ("obj", method_of))
        body = self.block()
        self.scopes.pop()
        self.in_method = prev
        return (name, params, out, body)

    def object(self):
        name = self.ident()
        if name in self.objects:
            self.fail(f"object {name} already declared")
        self.expect("{")
        fields = []
        methods = []
        while True:
            self.skip()
            if self.eat("}"):
                break
            if not self.peek():
                self.fail("unclosed object")
            if self.eat("fn"):
                methods.append(self.function(method_of=name))
                continue
            ty = self.ty()
            fname = self.ident()
            self.expect(";")
            if ty != "f64":
                self.fail("object fields in this slice are f64")
            fields.append(fname)
        self.objects[name] = (fields, methods)
        return ("object", name)

    def item(self):
        self.skip()
        if self.eat("fn"):
            fn = self.function()
            self.fns.append(fn)
            return None
        if self.eat("object"):
            return self.object()
        return self.statement_at(self.line)

    def program(self):
        # statement() currently double-dispatches. Use item only.
        stmts = []
        while True:
            self.skip()
            if not self.peek():
                return stmts
            it = self.item()
            if it is not None and it[0] != "object":
                stmts.append(it)
        return stmts


class Asm:
    def __init__(self, objects, fns):
        self.objects = objects
        self.fns = {fn[0]: fn for fn in fns}
        self.o = []
        self.nlab = 0
        self.lits = []
        self.dubs = []
        self.loops = []
        self.locals = {}
        self.kind = {}
        self.cursor = 64
        self.fn_kind = {}
        self.self_off = None

    def w(self, s):
        self.o.append(s)

    def lab(self):
        self.nlab += 1
        return f".L{self.nlab}"

    def dub(self, v):
        lab = f".D{len(self.dubs)}"
        self.dubs.append((lab, float(v)))
        return lab

    def lit(self, s):
        lab = f".S{len(self.lits)}"
        self.lits.append((lab, s))
        return lab

    def slot(self, name, kind):
        if name in self.locals:
            return
        if kind == "f64" or kind == "i32":
            self.cursor += 8
        elif isinstance(kind, tuple) and kind[0] == "obj":
            fields, _ = self.objects[kind[1]]
            self.cursor += 8 * len(fields)
        else:
            raise CompileError(f"cannot store {name}")
        self.locals[name] = -self.cursor
        self.kind[name] = kind

    def kind_of(self, e, env):
        k = e[0]
        if k == "f64":
            return "f64"
        if k == "i32":
            return "i32"
        if k == "name":
            return env[e[1]]
        if k == "bin":
            return self.kind_of(e[2], env)
        if k == "as":
            return "f64"
        if k == "call":
            return self.fn_kind[e[1]]
        if k == "field":
            return "f64"
        if k == "method":
            return "f64"
        raise CompileError("bad expression kind")

    def gen_i32(self, e, env):
        k = e[0]
        if k == "i32":
            self.w(f"    mov ${e[1]}, %eax")
            return
        if k == "name":
            if env[e[1]] != "i32":
                raise CompileError(f"'{e[1]}' is not i32")
            self.w(f"    mov {self.locals[e[1]]}(%rbp), %eax")
            return
        if k == "bin":
            self.gen_i32(e[2], env)
            self.w("    mov %eax, -40(%rbp)")
            self.gen_i32(e[3], env)
            self.w("    mov %eax, %ecx")
            self.w("    mov -40(%rbp), %eax")
            if e[1] == "+":
                self.w("    add %ecx, %eax")
            elif e[1] == "-":
                self.w("    sub %ecx, %eax")
            else:
                raise CompileError("i32 only adds here")
            return
        raise CompileError("expected i32")

    def gen_f64(self, e, env):
        k = e[0]
        if k == "f64":
            lab = self.dub(e[1])
            self.w(f"    movsd {lab}(%rip), %xmm0")
            return
        if k == "name":
            if env[e[1]] != "f64":
                raise CompileError(f"'{e[1]}' is not f64")
            self.w(f"    movsd {self.locals[e[1]]}(%rbp), %xmm0")
            return
        if k == "as":
            self.gen_i32(e[2], env)
            self.w("    cvtsi2sd %eax, %xmm0")
            return
        if k == "bin":
            self.gen_f64(e[2], env)
            self.w("    sub $16, %rsp")
            self.w("    movsd %xmm0, (%rsp)")
            self.gen_f64(e[3], env)
            self.w("    movsd (%rsp), %xmm1")
            self.w("    add $16, %rsp")
            op = {"+": "addsd", "-": "subsd", "*": "mulsd", "/": "divsd"}[e[1]]
            self.w(f"    {op} %xmm0, %xmm1")
            self.w("    movapd %xmm1, %xmm0")
            return
        if k == "call":
            if len(e[2]) != 1:
                raise CompileError("this slice calls one f64")
            self.gen_f64(e[2][0], env)
            self.w("    movsd %xmm0, -56(%rbp)")
            self.w("    movsd -56(%rbp), %xmm0")
            self.w(f"    call {e[1]}")
            return
        if k == "field":
            base, field = e[1], e[2]
            self.addr_field(base, field, env)
            self.w("    movsd (%rax), %xmm0")
            return
        if k == "method":
            base, method, args = e[1], e[2], e[3]
            if len(args) != 1:
                raise CompileError("method takes one f64")
            self.gen_f64(args[0], env)
            self.w("    movsd %xmm0, -56(%rbp)")
            self.addr_obj(base, env)
            self.w("    mov %rax, %rdi")
            self.w("    movsd -56(%rbp), %xmm0")
            oname = env["HERE"][1] if base[0] == "here" else env[base[1]][1]
            self.w(f"    call {oname}_{method}")
            return
        raise CompileError("expected f64")

    def addr_obj(self, base, env):
        if base[0] == "here":
            self.w("    mov %rbx, %rax")
            return
        if base[0] != "name":
            raise CompileError("bad object")
        self.w(f"    lea {self.locals[base[1]]}(%rbp), %rax")

    def addr_field(self, base, field, env):
        if base[0] == "here":
            oname = env["HERE"][1]
            self.w("    mov %rbx, %rax")
        elif base[0] == "name":
            kind = env[base[1]]
            if not (isinstance(kind, tuple) and kind[0] == "obj"):
                raise CompileError(f"'{base[1]}' is not an object")
            oname = kind[1]
            self.w(f"    lea {self.locals[base[1]]}(%rbp), %rax")
        else:
            raise CompileError("bad field")
        fields = self.objects[oname][0]
        if field not in fields:
            raise CompileError(f"no field {field}")
        off = fields.index(field) * 8
        if off:
            self.w(f"    add ${off}, %rax")

    def gen_cond(self, e, env, false_lab):
        if e[0] == "not":
            yes = self.lab()
            self.gen_cond(e[1], env, yes)
            self.w(f"    jmp {false_lab}")
            self.w(yes + ":")
            return
        if e[0] == "cmp":
            self.gen_i32(e[2], env)
            self.w("    mov %eax, -40(%rbp)")
            self.gen_i32(e[3], env)
            self.w("    mov %eax, %ecx")
            self.w("    mov -40(%rbp), %eax")
            self.w("    cmp %ecx, %eax")
            jmp = {"<": "jge", ">": "jle", "<=": "jg", ">=": "jl", "==": "jne", "!=": "je"}[e[1]]
            self.w(f"    {jmp} {false_lab}")
            return
        self.gen_i32(e, env)
        self.w("    test %eax, %eax")
        self.w(f"    jz {false_lab}")

    def gen_stmts(self, stmts, env):
        for s in stmts:
            op = s[0]
            if op == "decl":
                _, ty, name, e, _ = s
                self.slot(name, ty)
                env[name] = ty
                if ty == "f64":
                    self.gen_f64(e, env)
                    self.w(f"    movsd %xmm0, {self.locals[name]}(%rbp)")
                else:
                    self.gen_i32(e, env)
                    self.w(f"    mov %eax, {self.locals[name]}(%rbp)")
            elif op == "store":
                _, name, e, _ = s
                if env[name] == "f64":
                    self.gen_f64(e, env)
                    self.w(f"    movsd %xmm0, {self.locals[name]}(%rbp)")
                else:
                    self.gen_i32(e, env)
                    self.w(f"    mov %eax, {self.locals[name]}(%rbp)")
            elif op == "odecl":
                _, oname, inst, _ = s
                self.slot(inst, ("obj", oname))
                env[inst] = ("obj", oname)
            elif op == "fset":
                _, name, field, e, _ = s
                self.gen_f64(e, env)
                self.w("    movsd %xmm0, -64(%rbp)")
                self.addr_field(("name", name), field, env)
                self.w("    movsd -64(%rbp), %xmm0")
                self.w("    movsd %xmm0, (%rax)")
            elif op == "if":
                _, c, body, els, _ = s
                no, end = self.lab(), self.lab()
                self.gen_cond(c, env, no)
                self.gen_stmts(body, env)
                self.w(f"    jmp {end}")
                self.w(no + ":")
                self.gen_stmts(els, env)
                self.w(end + ":")
            elif op == "loop":
                top, end = self.lab(), self.lab()
                self.loops.append(end)
                self.w(top + ":")
                self.gen_stmts(s[1], env)
                self.w(f"    jmp {top}")
                self.w(end + ":")
                self.loops.pop()
            elif op == "leave":
                if not self.loops:
                    raise CompileError("leave outside loop")
                self.w(f"    jmp {self.loops[-1]}")
            elif op == "return":
                if s[1] is None:
                    self.w("    xor %eax, %eax")
                else:
                    self.gen_f64(s[1], env)
                self.w("    jmp .Lret")
            elif op == "callstmt":
                _, name, args, _ = s
                if name == "tp_begin":
                    lab = self.lit(args[0][1])
                    self.w(f"    lea {lab}(%rip), %rdi")
                    self.w("    call tp_begin@PLT")
                elif name == "tp_end":
                    self.gen_f64(args[0], env)
                    self.w("    call tp_end@PLT")
                elif name == "tp_threads":
                    self.gen_i32(args[0], env)
                    self.w("    mov %eax, %edi")
                    self.w("    call tp_threads@PLT")
                else:
                    raise CompileError(f"unknown call {name}")
            else:
                raise CompileError(f"cannot lower {op}")

    def emit_fn(self, name, params, out, body, method_of):
        self.o = []
        self.locals = {}
        self.kind = {}
        self.cursor = 64
        env = {}
        for ty, pname in params:
            self.slot(pname, ty)
            env[pname] = ty
        if method_of:
            env["HERE"] = ("obj", method_of)
        # walk decls first so the frame is stable before codegen? gen_stmts slots on the fly.
        # Return jumps to .Lret, so the frame size must be known first.
        # Pre-scan decls.
        self.prescan(body, env)
        frame = (self.cursor + 15) & ~15
        saved_locals = dict(self.locals)
        self.locals = saved_locals
        self.o = []
        self.w(f"    .globl {name}")
        self.w(f"{name}:")
        self.w("    push %rbp")
        self.w("    mov %rsp, %rbp")
        self.w("    push %rbx")
        self.w(f"    sub ${frame + 8}, %rsp")
        if method_of:
            self.w("    mov %rdi, %rbx")
        if params:
            self.w(f"    movsd %xmm0, {self.locals[params[0][1]]}(%rbp)")
        self.gen_stmts(body, env)
        self.w(".Lret:")
        self.w(f"    add ${frame + 8}, %rsp")
        self.w("    pop %rbx")
        self.w("    pop %rbp")
        self.w("    ret")
        # .Lret is not unique if two functions. Use a unique label.
        text = "\n".join(self.o)
        # The shared .Lret name collides. Rename per function.
        text = text.replace(".Lret", f".Lret_{name}")
        return text

    def prescan(self, stmts, env):
        for s in stmts:
            if s[0] == "decl":
                self.slot(s[2], s[1])
                env[s[2]] = s[1]
            elif s[0] == "odecl":
                self.slot(s[2], ("obj", s[1]))
                env[s[2]] = ("obj", s[1])
            elif s[0] in ("if",):
                self.prescan(s[2], env)
                self.prescan(s[3], env)
            elif s[0] == "loop":
                self.prescan(s[1], env)

    def emit_main(self, stmts):
        self.o = []
        self.locals = {}
        self.kind = {}
        self.cursor = 64
        env = {}
        self.prescan(stmts, env)
        frame = (self.cursor + 15) & ~15
        self.o = []
        self.w("    .globl main")
        self.w("main:")
        self.w("    push %rbp")
        self.w("    mov %rsp, %rbp")
        self.w("    push %rbx")
        self.w(f"    sub ${frame + 8}, %rsp")
        self.gen_stmts(stmts, env)
        self.w("    xor %eax, %eax")
        self.w(f"    add ${frame + 8}, %rsp")
        self.w("    pop %rbx")
        self.w("    pop %rbp")
        self.w("    ret")
        return "\n".join(self.o)

    def render(self, stmts):
        parts = []
        for name, params, out, body in self.fns.values():
            self.fn_kind[name] = out
            parts.append(self.emit_fn(name, params, out, body, None))
        for oname, (fields, methods) in self.objects.items():
            for mname, params, out, body in methods:
                parts.append(self.emit_fn(f"{oname}_{mname}", params, out, body, oname))
        parts.append(self.emit_main(stmts))
        # dubs and lits were collected across emits, but each emit resets self.o
        # and gen uses labels. The lists persist. Good.
        ro = ["    .section .rodata", "    .align 8"]
        for lab, v in self.dubs:
            bits = struct.unpack("<Q", struct.pack("<d", v))[0]
            ro.append(f"{lab}: .quad {bits}")
        for lab, s in self.lits:
            data = ",".join(str(b) for b in s.encode()) + ",0"
            ro.append(f"{lab}: .byte {data}")
        text = "\n".join(
            ["# pipeline: battery slice, amd64, g++"] + ro + ["    .text"] + parts
        )
        return text + "\n    .section .note.GNU-stack,\"\",@progbits\n"


def compile_feat(src):
    p = Parser(src)
    # Fix statement() so it isn't used. program uses item.
    stmts = []
    while True:
        p.skip()
        if not p.peek():
            break
        it = p.item()
        if it is not None and it[0] != "object":
            stmts.append(it)
    asm = Asm(p.objects, p.fns)
    # fn_kind filled during render, but gen of main needs it first.
    for name, params, out, body in p.fns:
        asm.fn_kind[name] = "f64" if out == "f64" else out
    text = asm.render(stmts)
    dump = ["battery slice", f"functions: {len(p.fns)}", f"objects: {len(p.objects)}"]
    return dump, text, "battery"
