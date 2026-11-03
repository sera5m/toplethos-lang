"""O3 tags. The locked R5 verbs from ikitaku, applied to a function's shape.

This is not the ikitaku process. The C++ reasoner stays a reasoner. O3 uses
its verb names as the tag set, then the normal passes run.
"""

# Locked object-work verbs. A function is tagged with the ones its shape supports.
VERBS = (
    "map", "reduce", "filter", "seek", "repeat", "sequence",
    "decision", "compare", "emit", "absorb", "copy", "bound",
)


def tag(prog):
    ann = prog.get("ann") or {}
    for fn in prog["fns"].values():
        rec = ann.get(fn["name"], {})
        shape = rec.get("shape") or ["map"]
        fn["ikitaku"] = [v for v in shape if v in VERBS] or ["map"]
    for oname, obj in prog["objects"].items():
        for fn in obj["methods"].values():
            key = oname + "." + fn["name"]
            rec = ann.get(key, {})
            shape = rec.get("shape") or ["map"]
            fn["ikitaku"] = [v for v in shape if v in VERBS] or ["map"]


def strength(prog):
    """An integer multiply by 2^k becomes a shift. Same wrap, fewer instructions."""
    for fn in list(prog["fns"].values()) + [
        m for obj in prog["objects"].values() for m in obj["methods"].values()
    ]:
        _stmts(fn["body"])


def _stmts(stmts):
    for i, st in enumerate(stmts):
        op = st[0]
        if op == "decl" and st[3] is not None:
            stmts[i] = (op, st[1], st[2], _expr(st[3]), st[4])
        elif op == "store":
            stmts[i] = (op, st[1], _expr(st[2]), st[3])
        elif op == "expr":
            stmts[i] = (op, _expr(st[1]), st[2])
        elif op == "return" and st[1] is not None:
            stmts[i] = (op, _expr(st[1]), st[2])
        elif op == "if":
            _stmts(st[2])
            _stmts(st[3])
            stmts[i] = (op, _expr(st[1]), st[2], st[3], st[4])
        elif op == "loop":
            _stmts(st[1])
        elif op == "do":
            _stmts(st[3])
            if len(st) > 4 and isinstance(st[4], list):
                _stmts(st[4])
        elif op == "time" and len(st) > 3 and isinstance(st[3], list):
            _stmts(st[3])


def _expr(e):
    if not isinstance(e, dict):
        return e
    for key in ("a", "b", "i"):
        if isinstance(e.get(key), dict):
            e[key] = _expr(e[key])
    if e.get("args"):
        e["args"] = [_expr(a) for a in e["args"]]
    if e.get("k") == "bin" and e.get("op") == "*" and e.get("ty") in (("i32",), ("i64",)):
        bits = 32 if e["ty"] == ("i32",) else 64
        k = _pow2(e.get("b"), bits)
        left = e.get("a")
        if k is None:
            k = _pow2(e.get("a"), bits)
            left = e.get("b")
        if k is not None and left is not None:
            return {
                "k": "bin", "op": "<<", "a": left,
                "b": {"k": "int", "n": k, "ty": ("i32",)},
                "ty": e["ty"], "row": e["ty"],
            }
    return e


def _pow2(e, bits):
    if not isinstance(e, dict) or e.get("k") != "int":
        return None
    n = e.get("n")
    if not isinstance(n, int) or n <= 1 or n & (n - 1):
        return None
    k = n.bit_length() - 1
    if k <= 0 or k >= bits:
        return None
    return k
