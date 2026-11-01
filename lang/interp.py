"""Execute the checked program. Same results as the compiler."""

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
        self.frames = []
        self.returned = False

    def run(self):
        self.call(self.prog["fns"]["main"], [])
        return "".join(self.out)

    def call(self, fn, args):
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
        try:
            return self.stmts(fn["body"])
        finally:
            self.returned = saved
            self.frames.pop()

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
                self.returned = True
                if st[1] is None:
                    return None
                return self.eval(st[1])
            if op == "leave":
                raise _Leave()
            if op == "continue":
                raise _Continue()
            if op == "decl":
                _, iid, ty, expr, _ = st
                slot = cell(ty)
                if ty[0] == "obj":
                    slot["present"] = True
                    slot["v"] = self.fresh_obj(ty[1])
                elif expr is not None:
                    slot["v"] = self.eval(expr)
                    slot["present"] = True
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
            return slot["v"]
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
            v = self.eval(e["a"])
            if e["op"] == "!":
                return not v
            return self.neg(v, e["ty"])
        if k == "bin":
            return self.binop(e)
        if k == "call":
            if e["name"] == "print":
                self.out.append(self.show(self.eval(e["args"][0]), e["args"][0]["ty"]) + "\n")
                return None
            args = [self.eval(a) for a in e["args"]]
            return self.call(self.prog["fns"][e["name"]], args)
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
            if op == "/" and b == 0.0:
                raise Failure("division by zero")
            if op == "+":
                return a + b
            if op == "-":
                return a - b
            if op == "*":
                return a * b
            return a / b
        bits = 32 if row == ("i32",) else 64
        if op == "/":
            if b == 0:
                raise Failure("division by zero")
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
