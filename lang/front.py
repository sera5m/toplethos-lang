"""One front end for both hosts.

It accepts the CPU core of USE.md and rejects the rest with the same message
on either host. A program that passes has one meaning.
"""


class CompileError(Exception):
    def __str__(self):
        return "compile error: " + super().__str__()


class Failure(Exception):
    def __str__(self):
        return "failure: " + super().__str__()


ATOMS = {"i32", "i64", "f64", "bool"}


class Front:
    def __init__(self, src):
        self.s = src
        self.i = 0
        self.line = 1
        self.col = 1
        self.fns = {}
        self.objects = {}
        self.scopes = []
        self.nid = 0
        self.loop = 0
        self.do_depth = 0
        self.cur_fn = None
        self.ids = {}

    def fail(self, msg):
        raise CompileError(f"line {self.line}: {msg}")

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
            if self.s.startswith("//", self.i):
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
        if tok == "=" and (self.starts("==") or self.starts("=>")):
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
        a = self.i
        self.get()
        while self.peek().isalnum() or self.peek() == "_":
            self.get()
        return self.s[a : self.i]

    def push(self):
        self.scopes.append({})

    def pop(self):
        self.scopes.pop()

    def bind(self, name, info):
        if name in self.scopes[-1]:
            self.fail(f"'{name}' is already declared")
        self.scopes[-1][name] = info
        self.ids[info["id"]] = info

    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        return None

    def fresh(self, name):
        self.nid += 1
        return f"{name}#{self.nid}"

    def later(self, what):
        self.fail(f"'{what}' is in USE.md and is not implemented on either host")

    def quoted(self):
        self.skip()
        if self.peek() != '"':
            self.fail("expected a string")
        self.get()
        buf = []
        while self.peek() not in ('"', ""):
            buf.append(self.get())
        if self.peek() != '"':
            self.fail("unclosed string")
        self.get()
        return "".join(buf)

    def skip_bracket_body(self):
        depth = 1
        while depth:
            c = self.peek()
            if not c:
                self.fail("unclosed '['")
            if c == "[":
                depth += 1
            elif c == "]":
                depth -= 1
            self.get()

    def mark_arg(self, name):
        if name == "divzero":
            if self.eat("0"):
                return "0"
            if self.eat("max"):
                return "max"
            self.fail("@divzero with an argument other than 0 or max")
        if name == "childof":
            parent = self.ident()
            if parent not in self.objects:
                self.fail("a parent that is not an object")
            return parent
        if name == "ismutationof":
            self.expect("getfuncidbyname")
            self.expect("[")
            text = self.quoted()
            self.expect("]")
            return text
        e = self.expr(("bool",))
        if e["ty"] != ("bool",):
            self.fail(f"@{name} condition is not bool")
        return e

    def marks(self, where):
        out = {}
        needs = ("divzero", "ismutationof", "require", "ensure", "childof")
        live = {
            "public", "private", "protected", "mut", "val", "xor",
            "ephemeral", "static", "config", "divzero", "ismutationof",
            "require", "ensure", "childof",
        }
        while True:
            self.skip()
            if not self.eat("@"):
                break
            name = self.ident()
            if name == "NoFailTolerance":
                self.fail("@NoFailTolerance")
            if name in ("ptr", "ref"):
                self.fail(f"@{name} is not a mark")
            arg = True
            if self.eat("["):
                if name not in needs:
                    self.skip_bracket_body()
                    self.later(f"@{name}")
                arg = self.mark_arg(name)
                self.expect("]")
            elif name in needs:
                self.fail(f"@{name} needs an argument")
            if name in out:
                self.fail(f"a second @{name}")
            if name not in live:
                self.later(f"@{name}")
            if name == "childof" and where != "object":
                self.fail("@childof is for an object")
            if name in ("divzero", "ismutationof", "require", "ensure") and where != "fn":
                self.fail(f"@{name} is for a function")
            if name in ("val", "xor", "ephemeral", "static", "config") and where not in ("bind", "field"):
                self.fail(f"@{name} is for a binding")
            out[name] = arg
        times = [t for t in ("ephemeral", "static", "config") if t in out]
        if len(times) > 1:
            self.fail("two time marks")
        if "mut" in out and ("val" in out or "config" in out):
            self.fail("@mut written on @val or @config")
        return out

    def paint(self, info, ms):
        if "val" in ms or "config" in ms:
            info["freeze"] = True
        if "config" in ms:
            info["time"] = "config"
        elif "static" in ms:
            info["time"] = "static"
        elif "ephemeral" in ms:
            info["time"] = "ephemeral"
            info["ephemeral"] = True
        if "xor" in ms:
            info["xor"] = True
        if "mut" in ms:
            info["mut"] = True
        if "private" in ms:
            info["scope"] = "private"
        elif "protected" in ms:
            info["scope"] = "protected"
        elif "public" in ms:
            info["scope"] = "public"
        return info

    def find_method(self, objname, mname):
        while objname:
            obj = self.objects[objname]
            if mname in obj["methods"]:
                return objname, obj["methods"][mname]
            objname = obj.get("parent")
        return None, None

    def link_mutations(self):
        table = {}
        for name, fn in self.fns.items():
            table[name] = fn
        for oname, obj in self.objects.items():
            for mname, fn in obj["methods"].items():
                table[f"{oname}.{mname}"] = fn
        for key, fn in table.items():
            target = fn.get("mutation_of")
            if not target:
                continue
            if target not in table:
                self.fail("@ismutationof a missing id")
            seen = {key}
            cur = target
            while table[cur].get("mutation_of"):
                if cur in seen:
                    self.fail("a mutation cycle")
                seen.add(cur)
                nxt = table[cur]["mutation_of"]
                if nxt not in table:
                    self.fail("@ismutationof a missing id")
                cur = nxt

    def note_uses(self, e):
        if not isinstance(e, dict):
            return
        if e.get("k") == "name":
            info = self.ids[e["id"]]
            if info.get("ephemeral"):
                info["uses"] = info.get("uses", 0) + 1
                if info["uses"] > 1:
                    self.fail("a second use of @ephemeral")
        for key in ("a", "b", "i"):
            if isinstance(e.get(key), dict):
                self.note_uses(e[key])
        for arg in e.get("args") or []:
            self.note_uses(arg)

    def note_store(self, info, depth):
        if not info or not info.get("freeze"):
            return
        if depth or info.get("stores"):
            self.fail("a later store to @val or @config")
        info["stores"] = 1

    def store_info(self, expr):
        while expr.get("k") in ("field", "index", "deref"):
            expr = expr["a"]
        if expr.get("k") == "name":
            return self.ids[expr["id"]]
        return None

    def audit(self, fn):
        def walk(body, depth):
            for st in body:
                op = st[0]
                if op == "decl" and st[3] is not None:
                    self.note_store(self.ids[st[1]], depth)
                    self.note_uses(st[3])
                elif op == "store":
                    info = self.store_info(st[1])
                    self.note_store(info, depth)
                    if st[1].get("k") == "name" and self.ids[st[1]["id"]].get("ephemeral"):
                        self.ids[st[1]["id"]]["uses"] = self.ids[st[1]["id"]].get("uses", 0) + 1
                        if self.ids[st[1]["id"]]["uses"] > 1:
                            self.fail("a second use of @ephemeral")
                    self.note_uses(st[2])
                elif op == "expr":
                    self.note_uses(st[1])
                elif op == "return" and st[1] is not None:
                    self.note_uses(st[1])
                elif op == "if":
                    self.note_uses(st[1])
                    walk(st[2], depth)
                    walk(st[3], depth)
                elif op == "loop":
                    walk(st[1], depth + 1)
                elif op == "do":
                    self.note_uses(st[1])
                    walk(st[3], depth + 1)
                    walk(st[4], depth)
        walk(fn["body"], 0)
        if isinstance(fn.get("require"), dict):
            self.note_uses(fn["require"])
        if isinstance(fn.get("ensure"), dict):
            self.note_uses(fn["ensure"])

    def at_word(self, words):
        self.skip()
        return any(self.starts(w) for w in words)

    def parse(self):
        items = []
        while True:
            self.skip()
            if not self.peek():
                break
            if self.eat("fn"):
                items.append(self.function(None))
                continue
            if self.eat("object"):
                self.object()
                continue
            if self.at_word(("namespace", "flow", "pipe", "pool", "enum", "macro", "struct", "include")):
                w = self.ident()
                self.later(w)
            self.fail("expected fn or object")
        self.link_mutations()
        if "main" not in self.fns:
            raise CompileError("a program with no main")
        fn = self.fns["main"]
        if fn["params"] or fn["out"] != ("i32",):
            self.fail("main takes nothing and its result type is i32")
        return {"fns": self.fns, "objects": self.objects, "ids": self.ids}

    def type_name(self):
        self.skip()
        if self.starts("fn"):
            self.later("fn type")
        name = self.ident()
        if name in self.objects:
            ty = ("obj", name)
        elif name in ATOMS:
            ty = (name,)
        else:
            self.later(name) if name not in ("i8", "i16", "f32", "ui32", "ui64", "string", "char", "size") else self.later(name)
        while self.eat("*"):
            ty = ("ptr", ty)
        return ty

    def function(self, method_of):
        line = self.line
        name = self.ident()
        early = self.marks("fn")
        self.expect("in")
        self.expect("[")
        params = []
        self.push()
        self.skip()
        if not self.starts("]"):
            while True:
                ty = self.type_name()
                pname = self.ident()
                pid = self.fresh(pname)
                info = {"id": pid, "ty": ty, "ro": False, "name": pname}
                self.bind(pname, info)
                params.append(info)
                if len(params) > 6:
                    self.fail("more than 6 parameters")
                self.skip()
                if self.eat(","):
                    continue
                break
        self.expect("]")
        self.expect("out")
        self.expect("[")
        self.skip()
        if self.eat("]"):
            out = None
        else:
            out = self.type_name()
            self.expect("]")
        late = self.marks("fn")
        for key in early:
            if key in late:
                self.fail(f"a second @{key}")
        ms = dict(early)
        ms.update(late)
        if name in self.fns or name in self.objects:
            self.fail(f"a second member with the short name {name}")
        prev = self.cur_fn
        self.cur_fn = {"name": name, "out": out, "line": line, "method": method_of}
        if method_of:
            hid = self.fresh("HERE")
            self.bind("HERE", {"id": hid, "ty": ("obj", method_of), "ro": True, "name": "HERE"})
        body = self.block()
        self.pop()
        fn = {
            "name": name,
            "params": params,
            "out": out,
            "body": body,
            "line": line,
            "method": method_of,
            "here": hid if method_of else None,
            "divzero": ms.get("divzero"),
            "mutation_of": ms.get("ismutationof"),
            "require": ms["require"] if isinstance(ms.get("require"), dict) else None,
            "ensure": ms["ensure"] if isinstance(ms.get("ensure"), dict) else None,
            "scope": "private" if "private" in ms else "protected" if "protected" in ms else "public",
        }
        if ms.get("divzero") is True:
            self.fail("@divzero with an argument other than 0 or max")
        self.cur_fn = prev
        self.audit(fn)
        self.returned_all(fn)
        if method_of:
            return fn
        self.fns[name] = fn
        return fn

    def returned_all(self, fn):
        if fn["out"] is None:
            return
        if not self.always_returns(fn["body"]):
            raise CompileError(
                f'function "{fn["name"]}" on line {fn["line"]} has a path with no return'
            )

    def always_returns(self, body):
        for st in body:
            if st[0] == "return":
                return True
            if st[0] == "if" and st[3] and self.always_returns(st[2]) and self.always_returns(st[3]):
                return True
        return False

    def object(self):
        name = self.ident()
        ms = self.marks("object")
        parent = ms.get("childof")
        if parent:
            seen = set()
            cur = parent
            while cur:
                if cur == name or cur in seen:
                    self.fail("a cycle")
                seen.add(cur)
                cur = self.objects[cur].get("parent")
        if name in self.objects or name in self.fns:
            self.fail(f"a second member with the short name {name}")
        self.expect("{")
        mark = (self.i, self.line, self.col)
        fields = []
        owners = {}
        fmarks = {}
        if parent:
            p = self.objects[parent]
            fields = list(p["fields"])
            owners = dict(p.get("field_owner", {}))
            fmarks = dict(p.get("field_marks", {}))
        self.objects[name] = {
            "fields": fields, "methods": {}, "parent": parent,
            "field_owner": owners, "field_marks": fmarks,
        }
        depth = 1
        while depth:
            self.skip()
            if not self.peek():
                self.fail("unclosed object")
            if self.starts("{"):
                self.get()
                depth += 1
                continue
            if self.starts("}"):
                self.get()
                depth -= 1
                continue
            if depth == 1 and self.starts("fn"):
                while self.peek() and not self.starts("{"):
                    self.get()
                continue
            if depth == 1:
                ty = self.type_name()
                fname = self.ident()
                fm = self.marks("field")
                for t in ("ephemeral", "static", "config"):
                    if t in fm:
                        self.fail("a field cannot carry @ephemeral, @static, or @config")
                self.expect(";")
                if any(f[0] == fname for f in fields):
                    if parent and any(f[0] == fname for f in self.objects[parent]["fields"]):
                        self.fail("a child must not declare a field name the parent already has")
                    self.fail(f"a second member with the short name {fname}")
                fields.append((fname, ty))
                owners[fname] = name
                fmarks[fname] = fm
                continue
            self.get()
        self.i, self.line, self.col = mark
        methods = {}
        while True:
            self.skip()
            if self.eat("}"):
                break
            if not self.peek():
                self.fail("unclosed object")
            if self.eat("fn"):
                fn = self.function(name)
                if fn["name"] in methods or any(f[0] == fn["name"] for f in fields):
                    self.fail(f"a second member with the short name {fn['name']}")
                owner, _ = self.find_method(parent, fn["name"])
                if owner and fn.get("mutation_of") != f"{owner}.{fn['name']}":
                    self.fail("a child method reusing a short name without @ismutationof of that parent method")
                methods[fn["name"]] = fn
                continue
            self.type_name()
            self.ident()
            self.marks("field")
            self.expect(";")
        self.objects[name]["methods"] = methods

    def block(self):
        self.expect("{")
        self.push()
        body = []
        while True:
            self.skip()
            if self.eat("}"):
                self.pop()
                return body
            if not self.peek():
                self.fail("unclosed '{'")
            body.append(self.statement())

    def statement(self):
        self.skip()
        line = self.line
        if self.eat("return"):
            return self.ret(line)
        if self.eat("leave"):
            if self.loop == 0:
                self.fail("leave outside loop")
            self.expect(";")
            return ("leave", line)
        if self.eat("continue"):
            if self.loop == 0:
                self.fail("continue outside loop")
            self.expect(";")
            return ("continue", line)
        if self.eat("if"):
            return self.if_stmt(line)
        if self.eat("loop"):
            self.marks("loop")
            self.loop += 1
            body = self.block()
            self.loop -= 1
            return ("loop", body, line)
        if self.eat("do"):
            return self.do_stmt(line)
        if self.eat("match") or self.eat("defer") or self.eat("select"):
            self.later("match" if False else "flow keyword")
        if self.starts("i32") or self.starts("i64") or self.starts("f64") or self.starts("bool") or self.known_obj():
            return self.decl(line)
        return self.assign_or_call(line)

    def known_obj(self):
        self.skip()
        for name in self.objects:
            if self.starts(name):
                return True
        return False

    def ret(self, line):
        fn = self.cur_fn
        if self.eat(";"):
            if fn["out"] is not None:
                self.fail(f'function "{fn["name"]}" on line {fn["line"]} has a path with no return')
            return ("return", None, line)
        expr = self.fit(self.expr(fn["out"]), fn["out"])
        self.expect(";")
        if fn["out"] is None:
            raise CompileError(
                f'function "{fn["name"]}" on line {fn["line"]} has no designated type out[] in function definition and cannot return this statement'
            )
        if expr["ty"] != fn["out"]:
            self.fail(f'result type is not the designated out type')
        return ("return", expr, line)

    def if_stmt(self, line):
        if self.eat("["):
            self.fail("if takes a bool condition")
        cond = self.expr(("bool",))
        if cond["ty"] != ("bool",):
            self.fail("if with a condition that is not bool")
        body = self.block()
        els = []
        self.skip()
        if self.eat("else"):
            self.skip()
            if self.eat("if"):
                els = [self.if_stmt(self.line)]
            else:
                els = self.block()
        return ("if", cond, body, els, line)

    def do_stmt(self, line):
        self.expect("n")
        self.expect("[")
        count = self.expr(("i32",))
        if count["ty"] != ("i32",):
            self.fail("do n takes an i32")
        if count["k"] == "int" and count["n"] < 0:
            self.fail("do n[k] when k is a constant and k < 0")
        self.expect("]")
        self.marks("loop")
        self.loop += 1
        self.do_depth += 1
        self.expect("{")
        self.push()
        iid = self.fresh("i")
        self.bind("i", {"id": iid, "ty": ("i32",), "ro": True, "name": "i"})
        body = []
        while True:
            self.skip()
            if self.eat("}"):
                break
            if not self.peek():
                self.fail("unclosed '{'")
            body.append(self.statement())
        self.pop()
        self.do_depth -= 1
        self.loop -= 1
        onfin = []
        self.skip()
        if self.eat("onFinished"):
            onfin = self.block()
        return ("do", count, iid, body, onfin, line)

    def decl(self, line):
        ty = self.type_name()
        name = self.ident()
        if self.eat("["):
            return self.array_decl(ty, name, line)
        ms = self.marks("bind")
        if self.do_depth and name == "i":
            self.fail("writing i in a do")
        if self.eat("@"):
            self.later("attribute on a binding")
        expr = None
        if self.eat("="):
            expr = self.fit(self.expr(ty), ty)
            if expr["ty"] != ty:
                self.fail(f"cannot store {self.show(expr['ty'])} in {self.show(ty)}")
        self.expect(";")
        iid = self.fresh(name)
        info = {"id": iid, "ty": ty, "ro": False, "name": name}
        self.paint(info, ms)
        self.bind(name, info)
        return ("decl", iid, ty, expr, line)

    def array_decl(self, ty, name, line):
        if ty[0] == "ptr" or ty[0] == "obj":
            self.later("array of that type")
        self.skip()
        if not self.peek().isdigit():
            self.fail("an array size that is not a constant")
        n = 0
        while self.peek().isdigit():
            n = n * 10 + int(self.get())
        if n == 0:
            self.fail("i32[0]" if ty == ("i32",) else "an array size that is not a constant")
        self.expect("]")
        items = None
        if self.eat("="):
            self.expect("{")
            items = []
            self.skip()
            if not self.starts("}"):
                while True:
                    item = self.fit(self.expr(ty), ty)
                    if item["ty"] != ty:
                        self.fail("array element type")
                    items.append(item)
                    self.skip()
                    if self.eat(","):
                        continue
                    break
            self.expect("}")
            if len(items) > n:
                self.fail("a brace list longer than the array")
        self.expect(";")
        iid = self.fresh(name)
        aty = ("arr", ty, n)
        self.bind(name, {"id": iid, "ty": aty, "ro": False, "name": name})
        return ("adecl", iid, aty, items, line)

    def assign_or_call(self, line):
        expr = self.expr(None)
        if self.eat("="):
            if expr["k"] not in ("name", "field", "deref", "index"):
                self.fail("expr = name")
            self.write_target(expr)
            rhs = self.fit(self.expr(expr["ty"]), expr["ty"])
            if rhs["ty"] != expr["ty"]:
                self.fail(f"cannot store {self.show(rhs['ty'])} in {self.show(expr['ty'])}")
            self.expect(";")
            return ("store", expr, rhs, line)
        if expr["k"] == "call" or expr["k"] == "method":
            self.expect(";")
            return ("expr", expr, line)
        self.fail("a statement, other than a brace block, that does not end with ;")

    def write_target(self, expr):
        if expr["k"] != "name":
            return
        info = self.ids[expr["id"]]
        if info.get("ro"):
            if info["name"] == "i":
                self.fail("writing i in a do")
            self.fail(f"'{info['name']}' cannot be stored")

    def fit(self, expr, ty):
        if expr["k"] == "int" and ty in (("i32",), ("i64",)):
            if ty == ("i32",) and expr["n"] > 2147483647:
                self.fail("integer does not fit in i32")
            expr = dict(expr)
            expr["ty"] = ty
        return expr
        if expr["k"] == "name" and self.ids[expr["id"]].get("ro"):
            self.fail("writing i in a do")

    def show(self, ty):
        if ty[0] == "ptr":
            return self.show(ty[1]) + "*"
        if ty[0] == "obj":
            return ty[1]
        if ty[0] == "arr":
            return self.show(ty[1]) + "[" + str(ty[2]) + "]"
        return ty[0]

    def expr(self, expect):
        return self.bin_or(expect)

    def bin_or(self, expect):
        left = self.bin_and(expect if expect == ("bool",) else None)
        while self.eat("||"):
            right = self.bin_and(("bool",))
            if left["ty"] != ("bool",) or right["ty"] != ("bool",):
                self.fail("|| applied to a value that is not bool")
            left = {"k": "bin", "op": "||", "a": left, "b": right, "ty": ("bool",)}
        return left

    def bin_and(self, expect):
        left = self.bin_eq(None)
        while self.eat("&&"):
            right = self.bin_eq(None)
            if left["ty"] != ("bool",) or right["ty"] != ("bool",):
                self.fail("&& applied to a value that is not bool")
            left = {"k": "bin", "op": "&&", "a": left, "b": right, "ty": ("bool",)}
        return left

    def bin_eq(self, expect):
        left = self.bin_rel(None)
        while True:
            op = None
            for cand in ("==", "!="):
                if self.eat(cand):
                    op = cand
                    break
            if not op:
                return left
            right = self.bin_rel(None)
            ty = self.arith_ty(left["ty"], right["ty"], eq=True)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ("bool",), "row": ty}

    def bin_rel(self, expect):
        left = self.bin_add(None)
        while True:
            op = None
            for cand in ("<=", ">=", "<", ">"):
                if self.eat(cand):
                    op = cand
                    break
            if not op:
                return left
            right = self.bin_add(None)
            ty = self.arith_ty(left["ty"], right["ty"], eq=True)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ("bool",), "row": ty}

    def bin_add(self, expect):
        left = self.bin_mul(None)
        while True:
            if self.eat("+"):
                op = "+"
            elif self.eat("-"):
                op = "-"
            else:
                return left
            right = self.bin_mul(None)
            ty = self.arith_ty(left["ty"], right["ty"], eq=False)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ty}

    def bin_mul(self, expect):
        left = self.unary(None)
        while True:
            if self.eat("*"):
                op = "*"
            elif self.eat("/"):
                op = "/"
            elif self.eat("%"):
                self.fail("%")
            else:
                return left
            right = self.unary(None)
            ty = self.arith_ty(left["ty"], right["ty"], eq=False)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ty}

    def arith_ty(self, a, b, eq):
        if a == b and a[0] in ("i32", "i64", "f64"):
            return a
        if a[0] in ("i32", "i64") and b == ("f64",):
            return ("f64",)
        if b[0] in ("i32", "i64") and a == ("f64",):
            return ("f64",)
        if a == ("bool",) or b == ("bool",):
            self.fail("no arithmetic row for bool")
        self.fail("no row for these operands")

    def unary(self, expect):
        self.skip()
        if self.eat("!"):
            e = self.unary(None)
            if e["ty"] != ("bool",):
                self.fail("! applied to a value that is not bool")
            return {"k": "unary", "op": "!", "a": e, "ty": ("bool",)}
        if self.eat("-"):
            e = self.unary(None)
            if e["ty"] not in (("i32",), ("i64",), ("f64",)):
                self.fail("no arithmetic row for this unary minus")
            return {"k": "unary", "op": "-", "a": e, "ty": e["ty"]}
        if self.eat("+"):
            return self.unary(expect)
        if self.eat("*"):
            e = self.unary(None)
            if e["ty"][0] != "ptr":
                self.fail("* of a value that is not a pointer")
            return {"k": "deref", "a": e, "ty": e["ty"][1]}
        if self.eat("&"):
            e = self.unary(None)
            if e["k"] not in ("name", "field", "index", "deref"):
                self.fail("& of a value that is not a cell")
            if e["k"] == "name" and self.ids[e["id"]].get("xor"):
                self.fail("a pointer to an @xor cell")
            return {"k": "addr", "a": e, "ty": ("ptr", e["ty"])}
        return self.primary(expect)

    def primary(self, expect):
        self.skip()
        if self.eat("("):
            if self.peek() == ")":
                self.fail("name() is not a call")
            e = self.expr(expect)
            self.expect(")")
            return e
        if self.eat("as"):
            self.expect("[")
            ty = self.type_name()
            self.expect(",")
            inner = self.expr(None)
            self.expect("]")
            self.convert(inner["ty"], ty)
            return {"k": "as", "ty": ty, "a": inner}
        if self.eat("true"):
            return {"k": "bool", "v": True, "ty": ("bool",)}
        if self.eat("false"):
            return {"k": "bool", "v": False, "ty": ("bool",)}
        if self.peek().isdigit():
            return self.number(expect)
        name = self.ident()
        self.skip()
        if self.peek() == "(":
            self.fail("name() is not a call")
        if self.eat("in"):
            args = self.arglist()
            if name == "print":
                if len(args) != 1:
                    self.fail("print takes one value")
                return {"k": "call", "name": "print", "args": args, "ty": None}
            if name == "size":
                if len(args) != 1 or args[0]["ty"][0] != "arr":
                    self.fail("size takes one array")
                return {"k": "size", "a": args[0], "ty": ("i32",)}
            info = self.lookup(name)
            if info is None and name not in self.fns:
                self.fail(f"'{name}' is used before it is declared")
            if name in self.fns:
                return self.finish_call(name, args)
            self.fail(f"'{name}' is not a function")
        info = self.lookup(name)
        if info is None:
            self.fail(f"'{name}' is used before it is declared")
        node = {"k": "name", "id": info["id"], "ty": info["ty"], "name": name}
        return self.postfix(node)

    def finish_call(self, name, args):
        fn = self.fns[name]
        if len(args) != len(fn["params"]):
            self.fail(f"{name} argument count")
        fitted = []
        for arg, param in zip(args, fn["params"]):
            arg = self.fit(arg, param["ty"])
            if arg["ty"] != param["ty"]:
                self.fail(f"{name} argument type")
            fitted.append(arg)
        return {"k": "call", "name": name, "args": fitted, "ty": fn["out"]}

    def postfix(self, node):
        while True:
            if self.eat("["):
                if node["ty"][0] != "arr":
                    self.fail("index of a value that is not an array")
                idx = self.expr(("i32",))
                if idx["ty"] != ("i32",):
                    self.fail("an index must be i32")
                self.expect("]")
                node = {"k": "index", "a": node, "i": idx, "ty": node["ty"][1]}
                continue
            if self.eat("."):
                field = self.ident()
                if node["ty"][0] != "obj":
                    self.fail("field of a value that is not an object")
                obj = self.objects[node["ty"][1]]
                self.skip()
                if self.eat("in"):
                    owner, fn = self.find_method(node["ty"][1], field)
                    if fn is None:
                        self.fail(f"no method {field}")
                    if fn.get("scope") == "private" and owner != node["ty"][1]:
                        self.fail("a subclass calling a private method")
                    args = self.arglist()
                    if len(args) != len(fn["params"]):
                        self.fail(f"{field} argument count")
                    fitted = []
                    for arg, param in zip(args, fn["params"]):
                        arg = self.fit(arg, param["ty"])
                        if arg["ty"] != param["ty"]:
                            self.fail(f"{field} argument type")
                        fitted.append(arg)
                    node = {"k": "method", "a": node, "name": field, "obj": owner, "args": fitted, "ty": fn["out"]}
                    continue
                names = [f[0] for f in obj["fields"]]
                if field not in names:
                    self.fail(f"no field {field}")
                fty = obj["fields"][names.index(field)][1]
                owner = obj.get("field_owner", {}).get(field, node["ty"][1])
                if obj.get("field_marks", {}).get(field, {}).get("private") and owner != node["ty"][1]:
                    self.fail("a subclass naming a private field")
                node = {"k": "field", "a": node, "field": field, "ty": fty}
                continue
            return node

    def arglist(self):
        self.expect("[")
        args = []
        self.skip()
        if not self.starts("]"):
            while True:
                args.append(self.expr(None))
                self.skip()
                if self.eat(","):
                    continue
                break
        self.expect("]")
        return args

    def number(self, expect):
        start = self.i
        while self.peek().isdigit():
            self.get()
        if self.peek() == "." and self.i + 1 < len(self.s) and self.s[self.i + 1].isdigit():
            self.get()
            while self.peek().isdigit():
                self.get()
            return {"k": "f64", "n": float(self.s[start : self.i]), "ty": ("f64",)}
        n = int(self.s[start : self.i])
        if expect == ("i64",):
            ty = ("i64",)
        elif expect == ("i32",) or expect is None:
            ty = ("i32",)
            if n > 2147483647:
                self.fail("integer does not fit in i32")
        elif expect == ("f64",):
            self.fail("integer value used as f64")
        else:
            ty = ("i32",)
        return {"k": "int", "n": n, "ty": ty}

    def convert(self, src, dst):
        ok = {
            (("i32",), ("i64",)),
            (("i32",), ("f64",)),
            (("i64",), ("f64",)),
        }
        if src == dst:
            return
        if (src, dst) not in ok:
            self.fail("as with no row")


def parse(src):
    return Front(src).parse()
