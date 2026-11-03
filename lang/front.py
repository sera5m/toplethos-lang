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
        self.opt = None
        self.loop_names = []
        self.included = set()
        self.base = ""
        self.file = ""
        self.lock_targets = set()

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
            if self.s.startswith("/*", self.i):
                self.get()
                self.get()
                while not self.s.startswith("*/", self.i):
                    if not self.peek():
                        self.fail("unclosed comment")
                    self.get()
                self.get()
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
        if tok == "+" and self.starts("+="):
            return False
        if tok == "=" and (self.starts("==") or self.starts("=>")):
            return False
        if tok == "+" and self.starts("++"):
            return False
        if tok == "-" and self.starts("--"):
            return False
        if tok == "*" and self.starts("**"):
            return False
        if tok == "/" and self.starts("/_/"):
            return False
        if tok == "&" and self.starts("&&"):
            return False
        if tok == "|" and self.starts("||"):
            return False
        if tok == "^" and self.starts("^="):
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
        if name == "name":
            return self.ident()
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
            "require", "ensure", "childof", "mutex", "inlined",
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
                if name == "parallel":
                    if where != "fn":
                        self.fail("@parallel is for a function")
                    arg = self.thread_count()
                    self.expect("]")
                elif name == "name":
                    if where != "loop":
                        self.fail("@name is for a loop")
                    arg = self.ident()
                    self.expect("]")
                elif name not in needs:
                    self.skip_bracket_body()
                    self.later(f"@{name}")
                else:
                    arg = self.mark_arg(name)
                    self.expect("]")
            elif name in needs or name in ("parallel", "name"):
                self.fail(f"@{name} needs an argument")
            if name in out:
                self.fail(f"a second @{name}")
            if name not in live and name not in ("parallel", "name"):
                self.later(f"@{name}")
            if name == "childof" and where != "object":
                self.fail("@childof is for an object")
            if name in ("divzero", "ismutationof", "require", "ensure", "inlined") and where != "fn":
                self.fail(f"@{name} is for a function")
            if name in ("val", "xor", "ephemeral", "static", "config", "mutex") and where not in ("bind", "field"):
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
        if "mutex" in ms:
            info["mutex"] = True
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

    def note_uses(self, e, depth=0):
        if not isinstance(e, dict):
            return
        if e.get("k") == "upd":
            self.note_store(self.store_info(e["a"]), depth)
        if e.get("k") == "name":
            info = self.ids[e["id"]]
            if info.get("ephemeral"):
                info["uses"] = info.get("uses", 0) + 1
                if info["uses"] > 1:
                    self.fail("a second use of @ephemeral")
        for key in ("a", "b", "i"):
            if isinstance(e.get(key), dict):
                self.note_uses(e[key], depth)
        for arg in e.get("args") or []:
            self.note_uses(arg, depth)

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
                    self.note_uses(st[3], depth)
                elif op == "store":
                    info = self.store_info(st[1])
                    self.note_store(info, depth)
                    if st[1].get("k") == "name" and self.ids[st[1]["id"]].get("ephemeral"):
                        self.ids[st[1]["id"]]["uses"] = self.ids[st[1]["id"]].get("uses", 0) + 1
                        if self.ids[st[1]["id"]]["uses"] > 1:
                            self.fail("a second use of @ephemeral")
                    self.note_uses(st[2], depth)
                elif op == "expr":
                    self.note_uses(st[1], depth)
                elif op == "return" and st[1] is not None:
                    self.note_uses(st[1], depth)
                elif op == "if":
                    self.note_uses(st[1], depth)
                    walk(st[2], depth)
                    walk(st[3], depth)
                elif op == "loop":
                    walk(st[1], depth + 1)
                elif op == "do":
                    self.note_uses(st[1], depth)
                    walk(st[3], depth + 1)
                    walk(st[4], depth)
                elif op == "time":
                    self.note_uses(st[2], depth)
                    walk(st[3], depth)
        walk(fn["body"], 0)
        if isinstance(fn.get("require"), dict):
            self.note_uses(fn["require"])
        if isinstance(fn.get("ensure"), dict):
            self.note_uses(fn["ensure"])

    def at_word(self, words):
        self.skip()
        return any(self.starts(w) for w in words)

    def parse(self, require_main=True):
        self.parse_items()
        if not require_main:
            return None
        self.link_mutations()
        if "main" not in self.fns:
            raise CompileError("a program with no main")
        fn = self.fns["main"]
        if fn["params"] or fn["out"] != ("i32",):
            self.fail("main takes nothing and its result type is i32")
        prog = {"fns": self.fns, "objects": self.objects, "ids": self.ids, "opt": self.opt or "01"}
        from analyze import analyze, impure, symbol_table
        prog["ann"] = analyze(prog)
        prog["symbols"] = symbol_table(prog)
        for item in list(self.fns.values()) + [
            m for obj in self.objects.values() for m in obj["methods"].values()
        ]:
            if item.get("inlined"):
                why = impure(prog, item)
                if why:
                    self.fail(f"@inlined function is not pure ({why})")
        return prog

    def parse_items(self):
        while True:
            self.skip()
            if not self.peek():
                return
            if self.eat("@"):
                self.expect("globaloptimize")
                self.expect("[")
                digits = []
                self.skip()
                while self.peek().isdigit():
                    digits.append(self.get())
                level = "".join(digits)
                if level not in ("01", "02", "03"):
                    self.fail("@globaloptimize level")
                self.expect("]")
                self.expect(";")
                if self.opt is not None:
                    self.fail("a second @globaloptimize")
                if self.fns or self.objects:
                    self.fail("@globaloptimize before the first function")
                self.opt = level
                continue
            if self.eat("fn"):
                self.function(None)
                continue
            if self.eat("object"):
                self.object()
                continue
            if self.eat("include"):
                self.include_file()
                continue
            if self.at_word(("namespace", "flow", "pipe", "pool", "enum", "macro", "struct")):
                w = self.ident()
                self.later(w)
            self.fail("expected fn or object")

    def include_file(self):
        import os
        if self.eat("lib"):
            self.later("include lib")
        path = self.quoted()
        self.expect(";")
        if path.endswith((".h", ".hh", ".hpp", ".hxx")):
            self.fail("include of a C or C++ header as Toplethos source")
        if not path.endswith(".tpth_s"):
            self.fail("include of a file that is not .tpth_s")
        full = path if os.path.isabs(path) else os.path.join(self.base, path)
        full = os.path.abspath(full)
        if full in self.included:
            return
        if not os.path.isfile(full):
            self.fail(path)
        self.included.add(full)
        text = open(full).read()
        sub = Front(text)
        sub.fns = self.fns
        sub.objects = self.objects
        sub.ids = self.ids
        sub.nid = self.nid
        sub.opt = self.opt
        sub.included = self.included
        sub.base = os.path.dirname(full)
        sub.file = full
        sub.parse_items()
        self.nid = sub.nid
        self.opt = sub.opt

    def thread_count(self):
        self.skip()
        if not self.peek().isdigit():
            self.fail("@parallel takes a constant thread count")
        n = 0
        while self.peek().isdigit():
            n = n * 10 + int(self.get())
        if n < 1:
            self.fail("@parallel takes a constant thread count")
        return n

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
            "inlined": "inlined" in ms,
            "parallel": ms.get("parallel"),
            "file": self.file,
        }
        if ms.get("parallel"):
            ps = params
            if out != ("f64",):
                self.fail("a parallel function returns f64")
            if not ps or ps[0]["ty"] != ("i32",):
                self.fail("a parallel function takes the lane as i32")
            if len(ps) > 2 or (len(ps) == 2 and ps[1]["ty"][0] != "ptr"):
                self.fail("a parallel function takes the lane and one pointer")
        if ms.get("divzero") is True:
            self.fail("@divzero with an argument other than 0 or max")
        self.cur_fn = prev
        self.audit(fn)
        self.returned_all(fn)
        self.check_fn_mutex(fn)
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

    def extend_stmt(self, body, st):
        if st[0] == "seq":
            self.extend_stmt(body, st[1])
            self.extend_stmt(body, st[2])
        else:
            body.append(st)

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
            self.extend_stmt(body, self.statement())

    def statement(self):
        self.skip()
        line = self.line
        if self.eat("return"):
            return self.ret(line)
        if self.eat("leave"):
            name = None
            if self.eat("["):
                name = self.ident()
                self.expect("]")
                if name not in self.loop_names:
                    self.fail("leave[name] with no matching @name")
            elif self.loop == 0:
                self.fail("leave outside loop")
            self.expect(";")
            if name is None:
                return ("leave", line)
            return ("leave", line, name)
        if self.eat("continue"):
            if self.loop == 0:
                self.fail("continue outside loop")
            self.expect(";")
            return ("continue", line)
        if self.eat("if"):
            return self.if_stmt(line)
        if self.starts("@"):
            return self.time_stmt(line)
        if self.eat("fork"):
            return self.fork_stmt(line)
        if self.eat("finish"):
            return self.finish_stmt(line)
        if self.eat("lock"):
            return self.lock_stmt(line, "lock")
        if self.eat("unlock"):
            return self.lock_stmt(line, "unlock")
        if self.eat("hold"):
            return self.hold_stmt(line)
        if self.eat("loop"):
            ms = self.marks("loop")
            lname = ms.get("name")
            if lname:
                if lname in self.loop_names:
                    self.fail("a second @name")
                self.loop_names.append(lname)
            self.loop += 1
            body = self.block()
            self.loop -= 1
            if lname:
                self.loop_names.pop()
            if lname:
                return ("loop", body, line, {"name": lname})
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

    def int_const(self):
        e = self.expr(("i32",))
        if e["k"] != "int" or e["n"] < 1 or e["n"] > 64:
            self.fail("fork takes a constant thread count")
        return e["n"]

    def check_worker(self, name, extra):
        fn = self.fns.get(name)
        if fn is None:
            self.fail(f"'{name}' is not a function")
        if fn["out"] != ("f64",):
            self.fail("a parallel function returns f64")
        ps = fn["params"]
        if not ps or ps[0]["ty"] != ("i32",):
            self.fail("a parallel function takes the lane as i32")
        if extra is None and len(ps) != 1:
            self.fail("a parallel function takes the lane as i32")
        if extra is not None and (len(ps) != 2 or ps[1]["ty"][0] != "ptr"):
            self.fail("a parallel function takes the lane and one pointer")
        return fn

    def with_cell(self):
        if not self.eat("with"):
            return None
        self.expect("[")
        name = self.ident()
        self.expect("]")
        info = self.lookup(name)
        if info is None or not info.get("mutex"):
            self.fail("with takes an @mutex cell")
        return info["id"]

    def time_stmt(self, line):
        self.expect("@")
        if not self.eat("time"):
            self.later("attribute on a statement")
        self.expect("[")
        label = self.quoted()
        self.expect("]")
        self.expect("watch")
        watched = self.expr(("f64",))
        if watched["k"] != "name" or watched["ty"] != ("f64",):
            self.fail("@time watches an f64 name")
        body = self.block()
        return ("time", label, watched, body, line)

    def fork_stmt(self, line):
        name = self.ident()
        n = None
        if self.eat("in"):
            self.expect("[")
            n = self.int_const()
            self.expect("]")
        extra = self.with_cell()
        self.expect(";")
        fn = self.check_worker(name, extra)
        if n is None:
            if not fn.get("parallel"):
                self.fail("fork takes a constant thread count")
            n = fn["parallel"]
        elif fn.get("parallel") and n != fn["parallel"]:
            self.fail("@parallel count")
        return ("fork", name, n, extra, line)

    def finish_stmt(self, line):
        name = self.ident()
        self.expect(";")
        return ("finish", name, line)

    def lock_stmt(self, line, kind):
        self.expect("[")
        name = self.ident()
        self.expect("]")
        self.expect(";")
        info = self.lookup(name)
        if info is None:
            self.fail(f"'{name}' is used before it is declared")
        if not info.get("mutex") and info["ty"][0] != "ptr":
            self.fail("lock of a cell that is not @mutex")
        return (kind, info["id"], info["ty"], line)

    def state_cond(self):
        self.expect("state")
        self.expect("[")
        name = self.ident()
        self.expect("]")
        info = self.lookup(name)
        if info is None:
            self.fail(f"'{name}' is used before it is declared")
        op = "=="
        word = "present"
        if self.eat("==") or self.eat("!="):
            op = "==" if self.s.startswith("==", self.i - 2) else "!="
            word = self.ident()
            if word not in ("absent", "present", "empty"):
                self.fail("state is absent, present, or empty")
        return {"k": "statecmp", "id": info["id"], "op": op, "word": word, "ty": ("bool",)}

    def hold_stmt(self, line):
        self.expect("[")
        name = self.ident()
        self.expect("]")
        info = self.lookup(name)
        if info is None:
            self.fail(f"'{name}' is used before it is declared")
        if not info.get("mutex") and info["ty"][0] != "ptr":
            self.fail("hold of a cell that is not @mutex")
        body = self.block()
        return ("hold", info["id"], info["ty"], body, line)

    def if_stmt(self, line):
        if self.eat("["):
            cond = self.state_cond()
            self.expect("]")
        else:
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
        ms = self.marks("loop")
        lname = ms.get("name")
        if lname:
            if lname in self.loop_names:
                self.fail("a second @name")
            self.loop_names.append(lname)
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
            self.extend_stmt(body, self.statement())
        self.pop()
        self.do_depth -= 1
        self.loop -= 1
        if lname:
            self.loop_names.pop()
        onfin = []
        self.skip()
        if self.eat("onFinished"):
            onfin = self.block()
        st = ("do", count, iid, body, onfin, line)
        if lname:
            st = ("do", count, iid, body, onfin, line, {"name": lname})
        return st

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
        if info.get("mutex") and ty not in (("i32",), ("i64",), ("f64",)):
            self.fail("@mutex on this type")
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
        ms = self.marks("bind")
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
        info = {"id": iid, "ty": aty, "ro": False, "name": name}
        self.paint(info, ms)
        if info.get("mutex"):
            self.fail("@mutex on this type")
        self.bind(name, info)
        return ("adecl", iid, aty, items, line)

    def assign_or_call(self, line):
        expr = self.expr(None)
        op = self.assign_op()
        if op:
            return self.finish_assign(expr, op, line)
        if expr["k"] in ("call", "method", "loadbmp", "loadbytes", "upd"):
            self.expect(";")
            return ("expr", expr, line)
        self.fail("a statement, other than a brace block, that does not end with ;")

    def assign_op(self):
        if self.eat("+="):
            return "+="
        if self.eat("^="):
            return "^="
        if self.eat("="):
            return "="
        return None

    def finish_assign(self, dest, op, line):
        if dest["k"] not in ("name", "field", "deref", "index"):
            self.fail("expr = name")
        self.write_target(dest)
        rhs = self.expr(dest["ty"] if op == "=" else None)
        nxt = self.assign_op()
        if nxt:
            inner = self.finish_assign(rhs, nxt, line)
            rhs = dict(rhs)
            return ("seq", inner, self.store_of(dest, op, rhs, line))
        self.expect(";")
        return self.store_of(dest, op, rhs, line)

    def store_of(self, dest, op, rhs, line):
        if op == "+=":
            rhs = self.fit(rhs, dest["ty"])
            if rhs["ty"] != dest["ty"] or dest["ty"] not in (("i32",), ("i64",), ("f64",)):
                self.fail("no row for these operands")
            rhs = {"k": "bin", "op": "+", "a": dest, "b": rhs, "ty": dest["ty"]}
        elif op == "^=":
            self.convert(rhs["ty"], dest["ty"])
            if rhs["ty"] != dest["ty"]:
                rhs = {"k": "as", "ty": dest["ty"], "a": rhs}
        else:
            rhs = self.fit(rhs, dest["ty"])
            if rhs["ty"] != dest["ty"]:
                self.fail(f"cannot store {self.show(rhs['ty'])} in {self.show(dest['ty'])}")
        return ("store", dest, rhs, line)

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
        left = self.bin_bor(None)
        while self.eat("&&"):
            right = self.bin_bor(None)
            if left["ty"] != ("bool",) or right["ty"] != ("bool",):
                self.fail("&& applied to a value that is not bool")
            left = {"k": "bin", "op": "&&", "a": left, "b": right, "ty": ("bool",)}
        return left

    def bin_bor(self, expect):
        left = self.bin_xor(None)
        while self.starts("|") and not self.starts("||"):
            self.eat("|")
            right = self.bin_xor(None)
            left = self.bits(left, right, "|")
        return left

    def bin_xor(self, expect):
        left = self.bin_band(None)
        while self.starts("^") and not self.starts("^="):
            self.eat("^")
            right = self.bin_band(None)
            left = self.bits(left, right, "^")
        return left

    def bin_band(self, expect):
        left = self.bin_eq(None)
        while self.starts("&") and not self.starts("&&"):
            self.eat("&")
            right = self.bin_eq(None)
            left = self.bits(left, right, "&")
        return left

    def bits(self, left, right, op):
        if left["ty"] != right["ty"] or left["ty"] not in (("i32",), ("i64",)):
            self.fail("no row for these operands")
        return {"k": "bin", "op": op, "a": left, "b": right, "ty": left["ty"], "row": left["ty"]}

    def bin_eq(self, expect):
        left = self.bin_rel(None)
        while True:
            op = None
            for cand in ("!$=", "$=", "==", "!="):
                if self.eat(cand):
                    op = cand
                    break
            if not op:
                return left
            right = self.bin_rel(None)
            if op in ("$=", "!$="):
                left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ("bool",)}
                continue
            ty = self.arith_ty(left["ty"], right["ty"], eq=True)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ("bool",), "row": ty}

    def bin_rel(self, expect):
        left = self.bin_shift(None)
        while True:
            op = None
            for cand in ("<=", ">=", "<", ">"):
                if self.eat(cand):
                    op = cand
                    break
            if not op:
                return left
            right = self.bin_shift(None)
            ty = self.arith_ty(left["ty"], right["ty"], eq=True)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ("bool",), "row": ty}

    def bin_shift(self, expect):
        left = self.bin_add(None)
        while self.eat("<<") or self.eat(">>"):
            op = "<<" if self.s.startswith("<<", self.i - 2) else ">>"
            right = self.bin_add(None)
            if left["ty"] not in (("i32",), ("i64",)) or right["ty"] != ("i32",):
                self.fail("no row for these operands")
            if right["k"] == "int" and not (0 <= right["n"] < (32 if left["ty"] == ("i32",) else 64)):
                self.fail("a constant shift count outside 0 through one less than the width")
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": left["ty"], "row": left["ty"]}
        return left

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
        left = self.bin_pow(None)
        while True:
            if self.eat("/_/"):
                op = "//"
            elif self.eat("/"):
                op = "/"
            elif self.eat("%"):
                op = "%"
            elif self.eat("*"):
                op = "*"
            else:
                return left
            right = self.bin_pow(None)
            if op in ("//", "%"):
                if left["ty"] != right["ty"] or left["ty"] not in (("i32",), ("i64",), ("f64",)):
                    self.fail("no row for these operands")
                if op == "%" and left["ty"] == ("f64",):
                    self.fail("no row for these operands")
                ty = left["ty"]
            else:
                ty = self.arith_ty(left["ty"], right["ty"], eq=False)
            left = {"k": "bin", "op": op, "a": left, "b": right, "ty": ty, "row": ty}

    def bin_pow(self, expect):
        left = self.unary(None)
        if not self.eat("**"):
            return left
        right = self.bin_pow(None)
        if left["ty"] != right["ty"] or left["ty"] not in (("i32",), ("i64",), ("f64",)):
            self.fail("no row for these operands")
        if left["ty"] != ("f64",) and right["k"] == "int" and right["n"] < 0:
            self.fail("negative power")
        return {"k": "bin", "op": "**", "a": left, "b": right, "ty": left["ty"], "row": left["ty"]}

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
        if self.eat("++"):
            return self.bump(self.unary(None), 1, True)
        if self.eat("--"):
            return self.bump(self.unary(None), -1, True)
        if self.eat("~"):
            e = self.unary(None)
            if e["ty"] not in (("i32",), ("i64",)):
                self.fail("no row for these operands")
            return {"k": "unary", "op": "~", "a": e, "ty": e["ty"]}
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
        return self.postfix(self.primary(expect))

    def bump(self, e, delta, pre):
        if e["k"] not in ("name", "field", "index", "deref"):
            self.fail("++ of a value that is not a cell" if delta == 1 else "-- of a value that is not a cell")
        if e["ty"] not in (("i32",), ("i64",)):
            self.fail("++ of a value that is not an integer" if delta == 1 else "-- of a value that is not an integer")
        self.write_target(e)
        return {"k": "upd", "op": "++" if delta == 1 else "--", "pre": pre, "a": e, "ty": e["ty"]}

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
        if self.eat("start"):
            name = self.ident()
            if name not in self.fns:
                self.fail(f"'{name}' is not a function")
            return {"k": "start", "name": name, "ty": ("f64",)}
        if self.eat("run"):
            name = self.ident()
            self.expect("in")
            self.expect("[")
            n = self.int_const()
            self.expect("]")
            extra = self.with_cell()
            self.check_worker(name, extra)
            return {"k": "run", "name": name, "n": n, "extra": extra, "ty": ("f64",)}
        name = self.ident()
        self.skip()
        if self.peek() == "(":
            self.fail("name() is not a call")
        if self.eat("in"):
            if name == "loadbmp":
                return self.loadbmp()
            if name == "loadbytes":
                return self.loadbytes()
            args = self.arglist()
            if name == "print":
                if len(args) != 1:
                    self.fail("print takes one value")
                return {"k": "call", "name": "print", "args": args, "ty": None}
            if name == "size":
                if len(args) != 1 or args[0]["ty"][0] != "arr":
                    self.fail("size takes one array")
                return {"k": "size", "a": args[0], "ty": ("i32",)}
            if name in ("min", "max", "clamp", "abs", "swap"):
                return self.builtin(name, args)
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
        if fn.get("scope") != "public" and fn.get("file", "") != self.file:
            self.fail(f"'{name}' is private to another file")
        if len(args) != len(fn["params"]):
            self.fail(f"{name} argument count")
        fitted = []
        for arg, param in zip(args, fn["params"]):
            arg = self.fit(arg, param["ty"])
            if arg["ty"] != param["ty"]:
                self.fail(f"{name} argument type")
            fitted.append(arg)
        return {"k": "call", "name": name, "args": fitted, "ty": fn["out"]}

    def builtin(self, name, args):
        atoms = (("i32",), ("i64",), ("f64",))
        if name == "abs":
            if len(args) != 1 or args[0]["ty"] not in atoms:
                self.fail("no row for these operands")
            return {"k": "call", "name": "abs", "args": args, "ty": args[0]["ty"]}
        if name == "swap":
            if len(args) != 2 or args[0]["ty"] != args[1]["ty"] or args[0]["ty"][0] != "ptr":
                self.fail("no row for these operands")
            return {"k": "call", "name": "swap", "args": args, "ty": None}
        need = 3 if name == "clamp" else 2
        if len(args) != need:
            self.fail("no row for these operands")
        ty = args[0]["ty"]
        if ty not in atoms or any(a["ty"] != ty for a in args):
            self.fail("min, max, or clamp of two different atoms")
        return {"k": "call", "name": name, "args": args, "ty": ty}

    def collect_locks(self, body):
        for st in body:
            op = st[0]
            if op in ("lock", "unlock", "hold"):
                self.lock_targets.add(st[1])
            if op == "if":
                self.collect_locks(st[2])
                self.collect_locks(st[3])
            elif op == "loop":
                self.collect_locks(st[1])
            elif op == "do":
                self.collect_locks(st[3])
                self.collect_locks(st[4])
            elif op == "hold":
                self.collect_locks(st[3])
            elif op == "time":
                self.collect_locks(st[3])

    def check_fn_mutex(self, fn):
        self.lock_targets = set()
        self.collect_locks(fn["body"])
        end = self.check_mutex(fn["body"], set(), set())
        if end:
            self.fail("a path that locks and does not unlock")

    def check_mutex(self, body, locked, at_loop):
        locked = set(locked)
        for st in body:
            op = st[0]
            if op == "lock":
                locked.add(st[1])
            elif op == "unlock":
                if st[1] not in locked:
                    self.fail("unlock of a cell that is not locked")
                locked.discard(st[1])
            elif op == "hold":
                self.check_mutex(st[3], set(locked) | {st[1]}, at_loop)
            elif op == "if":
                left = self.check_mutex(st[2], locked, at_loop)
                right = self.check_mutex(st[3], locked, at_loop)
                if self.always_returns(st[2]) and not self.always_returns(st[3]):
                    locked = right
                elif self.always_returns(st[3]) and not self.always_returns(st[2]):
                    locked = left
                elif left != right:
                    self.fail("a path that locks and does not unlock")
                else:
                    locked = left
            elif op == "loop":
                end = self.check_mutex(st[1], locked, set(locked))
                if end != locked:
                    self.fail("a path that locks and does not unlock")
            elif op == "do":
                end = self.check_mutex(st[3], locked, set(locked))
                if end != locked:
                    self.fail("a path that locks and does not unlock")
                endf = self.check_mutex(st[4], locked, at_loop)
                if endf != locked:
                    self.fail("a path that locks and does not unlock")
            elif op == "time":
                self.guard_expr(st[2], locked)
                end = self.check_mutex(st[3], locked, at_loop)
                if end != locked:
                    self.fail("a path that locks and does not unlock")
            elif op == "return":
                if st[1] is not None:
                    self.guard_expr(st[1], locked)
                if locked:
                    self.fail("a path that locks and does not unlock")
            elif op in ("leave", "continue"):
                if locked != at_loop:
                    self.fail("a path that locks and does not unlock")
            elif op == "decl":
                if st[3] is not None:
                    self.guard_expr(st[3], locked)
            elif op == "store":
                self.guard_expr(st[1], locked)
                self.guard_expr(st[2], locked)
            elif op == "expr":
                self.guard_expr(st[1], locked)
            elif op == "adecl":
                for item in st[3] or []:
                    self.guard_expr(item, locked)
        return locked

    def guard_expr(self, e, locked, under_deref=False):
        if not isinstance(e, dict):
            return
        if e.get("k") == "deref":
            self.guard_expr(e.get("a"), locked, True)
            return
        if e.get("k") == "statecmp":
            return
        if e.get("k") == "name":
            info = self.ids.get(e["id"], {})
            if info.get("mutex") and e["id"] not in locked:
                self.fail("a read or a store of a @mutex cell outside lock or hold")
            if under_deref and e["id"] in self.lock_targets and e["id"] not in locked:
                self.fail("a read or a store of a @mutex cell outside lock or hold")
            return
        for v in e.values():
            if isinstance(v, dict):
                self.guard_expr(v, locked)
            elif isinstance(v, list):
                for item in v:
                    self.guard_expr(item, locked)

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
            if self.eat("++"):
                node = self.bump(node, 1, False)
                continue
            if self.eat("--"):
                node = self.bump(node, -1, False)
                continue
            return node

    def loadbmp(self):
        self.expect("[")
        path = self.quoted()
        self.expect(",")
        arr = self.expr(None)
        self.expect("]")
        if arr["k"] != "name" or arr["ty"][0] != "arr" or arr["ty"][1] != ("i32",):
            self.fail("loadbmp takes a string and an i32 array")
        return {"k": "loadbmp", "path": path, "id": arr["id"], "n": arr["ty"][2], "ty": None}

    def loadbytes(self):
        self.expect("[")
        path = self.quoted()
        self.expect(",")
        arr = self.expr(None)
        self.expect("]")
        if arr["k"] != "name" or arr["ty"][0] != "arr" or arr["ty"][1] != ("i32",):
            self.fail("loadbytes takes a string and an i32 array")
        return {"k": "loadbytes", "path": path, "id": arr["id"], "n": arr["ty"][2], "ty": ("i32",)}

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
