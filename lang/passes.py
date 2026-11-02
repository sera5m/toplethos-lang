"""Phase 1 simplifies the tree. Phase 3 applies the rule table.

Both run after the front end and before amd64 emission. The interpreter
does not use this module, so a rule may only change how a result is
computed, not the result.
"""

SCALARS = {("f64",), ("i32",), ("i64",), ("bool",)}


def optimize(prog):
    ids = prog["ids"]
    for fn in prog["fns"].values():
        _fn(fn, ids)
    for obj in prog["objects"].values():
        for fn in obj["methods"].values():
            _fn(fn, ids)
    return prog


def _scalar(ty):
    return ty in SCALARS or (isinstance(ty, tuple) and ty and ty[0] == "ptr")


def _wrap32(n):
    n &= 0xFFFFFFFF
    if n >= 0x80000000:
        n -= 0x100000000
    return n


def _idiv(a, b):
    if b == 0:
        return None
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def _fold_i32(op, a, b):
    if op == "+":
        return _wrap32(a + b)
    if op == "-":
        return _wrap32(a - b)
    if op == "*":
        return _wrap32(a * b)
    if op == "/":
        return _idiv(a, b)
    if op == "%":
        q = _idiv(a, b)
        if q is None:
            return None
        return _wrap32(a - q * b)
    return None


def _fold_f64(op, a, b):
    if op == "/" and b == 0.0:
        return None
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    if op == "/":
        return a / b
    return None


def _expr(e, present, ids):
    if not isinstance(e, dict):
        return e
    for key in ("a", "b", "i"):
        if isinstance(e.get(key), dict):
            e[key] = _expr(e[key], present, ids)
    if e.get("k") == "name" and e.get("id") in present:
        info = ids.get(e["id"], {})
        if not info.get("ephemeral") and _scalar(e.get("ty")):
            e["fast"] = True
    if e.get("k") == "bin" and e.get("ty") == ("i32",):
        if e["a"].get("k") == "int" and e["b"].get("k") == "int":
            n = _fold_i32(e["op"], e["a"]["n"], e["b"]["n"])
            if n is not None:
                return {"k": "int", "n": n, "ty": ("i32",)}
    if e.get("k") == "bin" and e.get("ty") == ("f64",):
        if e["a"].get("k") == "f64" and e["b"].get("k") == "f64":
            n = _fold_f64(e["op"], e["a"]["n"], e["b"]["n"])
            if n is not None:
                return {"k": "f64", "n": n, "ty": ("f64",)}
    return e


def _phase1(stmts, present, ids):
    for i, st in enumerate(stmts):
        op = st[0]
        if op == "decl":
            expr = _expr(st[3], present, ids) if st[3] is not None else None
            stmts[i] = ("decl", st[1], st[2], expr, st[4])
            _note_present(present, ids, st[1], st[2])
        elif op == "store":
            _expr(st[1], present, ids)
            rhs = _expr(st[2], present, ids)
            stmts[i] = ("store", st[1], rhs, st[3])
            if st[1].get("k") == "name":
                _note_present(present, ids, st[1]["id"], st[1]["ty"])
                _xor_kill(present, ids, st[1], rhs)
        elif op == "expr":
            stmts[i] = ("expr", _expr(st[1], present, ids), st[2])
        elif op == "return":
            expr = _expr(st[1], present, ids) if st[1] is not None else None
            stmts[i] = ("return", expr, st[2])
        elif op == "if":
            cond = _expr(st[1], present, ids)
            left, right = set(present), set(present)
            _phase1(st[2], left, ids)
            _phase1(st[3], right, ids)
            present.clear()
            present.update(left & right)
            stmts[i] = ("if", cond, st[2], st[3], st[4])
        elif op == "loop":
            _phase1(st[1], set(present), ids)
        elif op == "do":
            count = _expr(st[1], present, ids)
            inner = set(present)
            inner.add(st[2])
            _phase1(st[3], inner, ids)
            _phase1(st[4], set(present), ids)
            stmts[i] = ("do", count, st[2], st[3], st[4], st[5])
        elif op == "time":
            watch = _expr(st[2], present, ids)
            _phase1(st[3], present, ids)
            stmts[i] = ("time", st[1], watch, st[3], st[4])
        elif op == "adecl" and st[3] is not None:
            for n, item in enumerate(st[3]):
                st[3][n] = _expr(item, present, ids)
    return stmts


def _note_present(present, ids, iid, ty):
    info = ids.get(iid, {})
    if info.get("ephemeral"):
        return
    if _scalar(ty):
        present.add(iid)


def _xor_kill(present, ids, dest, rhs):
    if dest.get("k") != "name" or rhs.get("k") != "name":
        return
    if dest["id"] == rhs["id"]:
        return
    d = ids.get(dest["id"], {})
    s = ids.get(rhs["id"], {})
    if d.get("xor") and s.get("xor"):
        present.discard(rhs["id"])


def _blocked(ids, iid):
    info = ids.get(iid, {})
    return bool(info.get("ephemeral") or info.get("xor") or info.get("mutex") or info.get("time") in ("static", "config"))


def _pure_addend(e):
    if not isinstance(e, dict):
        return False
    if e.get("k") == "f64":
        return True
    if e.get("k") == "field" and e.get("a", {}).get("k") == "name":
        return True
    if e.get("k") == "index" and e.get("a", {}).get("k") == "name" and e.get("i", {}).get("k") == "int":
        return True
    return False


def _factor_of(rhs, dest_id):
    """Match addend + (factor * dest), or the multiply on the left. Two roundings."""
    if not isinstance(rhs, dict) or rhs.get("k") != "bin" or rhs.get("op") != "+":
        return None
    left, right = rhs["a"], rhs["b"]
    mul = addend = None
    if isinstance(right, dict) and right.get("k") == "bin" and right.get("op") == "*":
        mul, addend = right, left
    elif isinstance(left, dict) and left.get("k") == "bin" and left.get("op") == "*":
        mul, addend = left, right
    else:
        return None
    if not _pure_addend(addend):
        return None
    a, b = mul["a"], mul["b"]
    if a.get("k") == "name" and a.get("id") == dest_id and b.get("ty") == ("f64",):
        if b.get("k") == "name" and b.get("id") == dest_id:
            return None
        return addend, b
    if b.get("k") == "name" and b.get("id") == dest_id and a.get("ty") == ("f64",):
        if a.get("k") == "name" and a.get("id") == dest_id:
            return None
        return addend, a
    return None


def _same_factor(x, y):
    if x.get("k") == "name" and y.get("k") == "name":
        return x["id"] == y["id"]
    if x.get("k") == "f64" and y.get("k") == "f64":
        return x["n"] == y["n"]
    return False


def _factor_safe(fac):
    if fac.get("k") == "f64":
        return True
    return fac.get("k") == "name" and fac.get("fast") and fac.get("ty") == ("f64",)


def _phase3(stmts, ids=None):
    # ids is closed over by the caller via the function attribute set in _fn.
    return _fuse(stmts, _phase3.ids)


def _fuse(stmts, ids):
    out = []
    i = 0
    while i < len(stmts):
        st = _nest(stmts[i], ids)
        matched = _try_fuse(st, stmts, i, ids)
        if matched is None:
            out.append(st)
            i += 1
            continue
        out.extend(matched[0])
        i = matched[1]
    return out


def _nest(st, ids):
    op = st[0]
    if op == "if":
        return (op, st[1], _fuse(st[2], ids), _fuse(st[3], ids), st[4])
    if op == "loop":
        return (op, _fuse(st[1], ids), st[2])
    if op == "do":
        fused = (op, st[1], st[2], _fuse(st[3], ids), _fuse(st[4], ids), st[5])
        return fused + (st[6],) if len(st) > 6 else fused
    if op == "time":
        return (op, st[1], st[2], _fuse(st[3], ids), st[4])
    return st


def _try_fuse(st, stmts, i, ids):
    dest_id, init, line = _chain_start(st, ids)
    if dest_id is None:
        return None
    coeffs = []
    factor = None
    j = i + 1
    while j < len(stmts):
        nxt = stmts[j]
        if nxt[0] != "store" or nxt[1].get("k") != "name" or nxt[1].get("id") != dest_id:
            break
        got = _factor_of(nxt[2], dest_id)
        if got is None:
            break
        coeff, fac = got
        if factor is None:
            if not _factor_safe(fac):
                break
            factor = fac
        elif not _same_factor(factor, fac):
            break
        coeffs.append(coeff)
        j += 1
    if len(coeffs) < 2:
        return None
    dest = {"k": "name", "id": dest_id, "ty": ("f64",)}
    chain = ("fchain", dest, init, factor, coeffs, line)
    if st[0] == "decl":
        kept = ("decl", st[1], st[2], None, st[4])
        return [kept, chain], j
    return [chain], j


def _chain_start(st, ids):
    if st[0] == "decl":
        iid, ty, expr, line = st[1], st[2], st[3], st[4]
        if ty != ("f64",) or expr is None or not _pure_addend(expr) or _blocked(ids, iid):
            return None, None, None
        return iid, expr, line
    if st[0] == "store" and st[1].get("k") == "name" and st[1].get("ty") == ("f64",):
        iid = st[1]["id"]
        if _blocked(ids, iid) or not _pure_addend(st[2]):
            return None, None, None
        return iid, st[2], st[3]
    return None, None, None


# _fn fills this before _phase3 runs. A default keeps a stray call from crashing.
_phase3.ids = {}


def _fn(fn, ids):
    present = set()
    for p in fn["params"]:
        if _scalar(p["ty"]):
            present.add(p["id"])
    _phase1(fn["body"], present, ids)
    _phase2(fn["body"], ids)
    _phase3.ids = ids
    fn["body"] = _phase3(fn["body"])


# USE.md, section "Known rewrites". A row fires only when its proof holds.
# The value is the one the interpreter computes. Interlocks are not here.
UNROLL_MAX = 16


def _phase2(stmts, ids):
    consts = _const_arrays(stmts, ids)
    _fold_stmts(stmts, consts)
    _unroll_stmts(stmts, consts)
    # The unrolled stores still say c[6]. Fold those now.
    _fold_stmts(stmts, consts)
    _drop_dead_arrays(stmts)
    _mark_regloops(stmts, ids)
    return stmts


def _drop_dead_arrays(stmts):
    used = set()
    def see(e):
        if not isinstance(e, dict):
            return
        if e.get("k") == "name":
            used.add(e["id"])
        for v in e.values():
            if isinstance(v, dict):
                see(v)
            elif isinstance(v, list):
                for x in v:
                    see(x)
    def walk(body):
        for st in body:
            for part in st[1:]:
                see(part)
            op = st[0]
            if op == "if":
                walk(st[2]); walk(st[3])
            elif op == "loop":
                walk(st[1])
            elif op == "do":
                walk(st[3]); walk(st[4])
            elif op == "time":
                walk(st[3])
    walk(stmts)
    stmts[:] = [st for st in stmts if not (st[0] == "adecl" and st[1] not in used)]
    for st in stmts:
        op = st[0]
        if op == "if":
            _drop_dead_arrays(st[2]); _drop_dead_arrays(st[3])
        elif op == "loop":
            _drop_dead_arrays(st[1])
        elif op == "do":
            _drop_dead_arrays(st[3]); _drop_dead_arrays(st[4])
        elif op == "time":
            _drop_dead_arrays(st[3])


def _walk(stmts, fn):
    for st in stmts:
        fn(st)
        op = st[0]
        if op == "if":
            _walk(st[2], fn)
            _walk(st[3], fn)
        elif op == "loop":
            _walk(st[1], fn)
        elif op == "do":
            _walk(st[3], fn)
            _walk(st[4], fn)
        elif op == "time":
            _walk(st[3], fn)


def _const_arrays(stmts, ids):
    found = {}
    killed = set()

    def see(st):
        if st[0] == "adecl" and st[3]:
            items = st[3]
            if all(isinstance(it, dict) and it.get("k") in ("f64", "int") for it in items):
                found[st[1]] = list(items)
                # A short brace zero-fills. Record the zeros too.
                n = st[2][2]
                et = st[2][1]
                zero = {"k": "f64", "n": 0.0, "ty": ("f64",)} if et == ("f64",) else {"k": "int", "n": 0, "ty": et}
                while len(found[st[1]]) < n:
                    found[st[1]].append(dict(zero))
        if st[0] == "store":
            dest = st[1]
            if dest.get("k") == "name":
                killed.add(dest["id"])
            if dest.get("k") == "index" and dest.get("a", {}).get("k") == "name":
                killed.add(dest["a"]["id"])
        _kill_addr(st, killed)

    _walk(stmts, see)
    return {i: items for i, items in found.items() if i not in killed}


def _kill_addr(st, killed):
    def rec(e):
        if not isinstance(e, dict):
            return
        if e.get("k") == "addr" and e.get("a", {}).get("k") == "name":
            killed.add(e["a"]["id"])
        for v in e.values():
            if isinstance(v, dict):
                rec(v)
            elif isinstance(v, list):
                for x in v:
                    rec(x)
    for part in st[1:]:
        rec(part)


def _fold_stmts(stmts, consts):
    if not consts:
        return

    def rec(e):
        if not isinstance(e, dict):
            return e
        e = dict(e)
        for key in ("a", "b", "i"):
            if isinstance(e.get(key), dict):
                e[key] = rec(e[key])
        if e.get("k") == "index" and e.get("a", {}).get("k") == "name":
            if e["a"]["id"] in consts and e.get("i", {}).get("k") == "int":
                n = e["i"]["n"]
                items = consts[e["a"]["id"]]
                if 0 <= n < len(items):
                    return dict(items[n])
        return e

    for i, st in enumerate(list(stmts)):
        op = st[0]
        if op == "decl" and st[3] is not None:
            stmts[i] = (op, st[1], st[2], rec(st[3]), st[4])
        elif op == "store":
            stmts[i] = (op, rec(st[1]), rec(st[2]), st[3])
        elif op == "expr":
            stmts[i] = (op, rec(st[1]), st[2])
        elif op == "return" and st[1] is not None:
            stmts[i] = (op, rec(st[1]), st[2])
        elif op == "if":
            _fold_stmts(st[2], consts)
            _fold_stmts(st[3], consts)
            stmts[i] = (op, rec(st[1]), st[2], st[3], st[4])
        elif op == "loop":
            _fold_stmts(st[1], consts)
        elif op == "do":
            _fold_stmts(st[3], consts)
            _fold_stmts(st[4], consts)
        elif op == "time":
            _fold_stmts(st[3], consts)


def _unroll_stmts(stmts, consts):
    i = 0
    while i < len(stmts):
        st = stmts[i]
        op = st[0]
        if op == "if":
            _unroll_stmts(st[2], consts)
            _unroll_stmts(st[3], consts)
        elif op == "loop":
            _unroll_stmts(st[1], consts)
        elif op == "do":
            _unroll_stmts(st[3], consts)
            _unroll_stmts(st[4], consts)
        elif op == "time":
            _unroll_stmts(st[3], consts)
        if (op == "decl" and st[2] == ("i32",) and isinstance(st[3], dict)
                and st[3].get("k") == "int"):
            nxt = _countdown(stmts, i, st, consts)
            if nxt is not None:
                stmts[i:i + 2] = nxt
                i += len(nxt)
                continue
        i += 1


def _countdown(stmts, i, decl, consts):
    if i + 1 >= len(stmts) or stmts[i + 1][0] != "loop":
        return None
    n = decl[3]["n"]
    if not (0 <= n <= UNROLL_MAX):
        return None
    kid = decl[1]
    body = stmts[i + 1][1]
    if len(body) != 3:
        return None
    guard, step, dec = body
    if not _is_leave_when_neg(guard, kid) or not _is_dec(dec, kid):
        return None
    if step[0] != "store" or not _indexes_only(step, kid, consts):
        return None
    # Drop the decl of k. It would sit between y's initializer and the
    # steps, and the chain rule only sees adjacent stores.
    out = []
    for v in range(n, -1, -1):
        out.append(_subst_k(step, kid, v))
    return out


def _is_leave_when_neg(st, kid):
    if st[0] != "if" or st[3]:
        return False
    cond = st[1]
    if cond.get("k") != "unary" or cond.get("op") != "!":
        return False
    inner = cond.get("a", {})
    if inner.get("k") != "bin" or inner.get("op") != ">=":
        return False
    a, b = inner.get("a", {}), inner.get("b", {})
    if a.get("k") != "name" or a.get("id") != kid:
        return False
    if not (b.get("k") == "int" and b.get("n") == 0):
        return False
    return bool(st[2]) and st[2][0][0] == "leave"


def _is_dec(st, kid):
    if st[0] != "store":
        return False
    dest = st[1]
    if dest.get("k") != "name" or dest.get("id") != kid:
        return False
    rhs = st[2]
    return (rhs.get("k") == "bin" and rhs.get("op") == "-"
            and rhs.get("a", {}).get("k") == "name" and rhs["a"].get("id") == kid
            and rhs.get("b", {}).get("k") == "int" and rhs["b"].get("n") == 1)


def _indexes_only(st, kid, consts):
    bad = [False]

    def rec(e, as_index_of_k=False):
        if not isinstance(e, dict) or bad[0]:
            return
        if e.get("k") == "index":
            base, idx = e.get("a", {}), e.get("i", {})
            if base.get("k") != "name" or base.get("id") not in consts:
                bad[0] = True
            if not (idx.get("k") == "name" and idx.get("id") == kid):
                bad[0] = True
            rec(base)
            return
        if e.get("k") == "name" and e.get("id") == kid:
            bad[0] = True
            return
        for v in e.values():
            if isinstance(v, dict):
                rec(v)
    rec(st[1])
    rec(st[2])
    return not bad[0]


def _subst_k(st, kid, v):
    def rec(e):
        if not isinstance(e, dict):
            return e
        e = {key: rec(val) if isinstance(val, dict) else val for key, val in e.items()}
        if e.get("k") == "name" and e.get("id") == kid:
            return {"k": "int", "n": v, "ty": ("i32",)}
        return e
    return ("store", rec(st[1]), rec(st[2]), st[3])


def _mark_regloops(stmts, ids):
    for i, st in enumerate(stmts):
        op = st[0]
        if op == "if":
            _mark_regloops(st[2], ids)
            _mark_regloops(st[3], ids)
        elif op == "loop":
            _mark_regloops(st[1], ids)
        elif op == "do":
            _mark_regloops(st[3], ids)
            _mark_regloops(st[4], ids)
            if _regloop_ok(st, ids):
                mark = dict(st[6]) if len(st) > 6 else {}
                mark["reg"] = True
                stmts[i] = st[:6] + (mark,)
        elif op == "time":
            _mark_regloops(st[3], ids)


def _regloop_ok(st, ids):
    if st[1].get("k") != "int":
        return False
    banned = {"call", "method", "index", "field", "deref", "addr", "start", "run"}

    def rec(e):
        if not isinstance(e, dict):
            return True
        if e.get("k") in banned:
            return False
        if e.get("k") == "name":
            info = ids.get(e["id"], {})
            if e.get("ty") not in (("f64",), ("i32",)):
                return False
            if info.get("mutex") or info.get("xor") or info.get("time") in ("static", "config"):
                return False
        return all(rec(v) for v in e.values() if isinstance(v, dict))

    for inner in st[3]:
        if inner[0] not in ("store", "expr"):
            return False
        if any(isinstance(p, dict) and not rec(p) for p in inner[1:]):
            return False
    return True
