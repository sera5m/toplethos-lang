"""Phase 1 simplifies the tree. Phase 3 applies the rule table.

Both run after the front end and before amd64 emission. The interpreter
does not use this module, so a rule may only change how a result is
computed, not the result.
"""

SCALARS = {("f64",), ("i32",), ("i64",), ("bool",)}


def optimize(prog):
    _inline(prog)
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
    _fold_vals(stmts, ids)
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


def _fold_vals(stmts, ids):
    # A frozen f64 whose initializer is a constant is that constant.
    # The chain rule only sees literals, not names.
    found = {}
    killed = set()

    def see(st):
        if st[0] == "decl" and isinstance(st[3], dict) and st[3].get("k") == "f64":
            info = ids.get(st[1], {})
            if info.get("freeze") and info.get("ty") == ("f64",):
                found[st[1]] = st[3]
        _kill_addr(st, killed)

    _walk(stmts, see)
    found = {i: v for i, v in found.items() if i not in killed}
    if found:
        _swap_names(stmts, found)


def _swap_names(stmts, found):
    def rec(e):
        if not isinstance(e, dict):
            return e
        if e.get("k") == "name" and e.get("id") in found:
            return dict(found[e["id"]])
        e = dict(e)
        for key, val in list(e.items()):
            if isinstance(val, dict):
                e[key] = rec(val)
            elif isinstance(val, list):
                e[key] = [rec(x) if isinstance(x, dict) else x for x in val]
        return e

    def one(st):
        op = st[0]
        if op == "decl":
            expr = rec(st[3]) if isinstance(st[3], dict) else st[3]
            return (op, st[1], st[2], expr, st[4])
        if op == "store":
            return (op, rec(st[1]), rec(st[2]), st[3])
        if op == "expr":
            return (op, rec(st[1]), st[2])
        if op == "return":
            return (op, rec(st[1]) if isinstance(st[1], dict) else st[1], st[2])
        if op == "if":
            _swap_names(st[2], found)
            _swap_names(st[3], found)
            return (op, rec(st[1]), st[2], st[3], st[4])
        if op == "loop":
            _swap_names(st[1], found)
            return st
        if op == "do":
            _swap_names(st[3], found)
            _swap_names(st[4], found)
            count = rec(st[1]) if isinstance(st[1], dict) else st[1]
            out = (op, count, st[2], st[3], st[4], st[5])
            return out + (st[6],) if len(st) > 6 else out
        if op == "time":
            _swap_names(st[3], found)
            return (op, st[1], rec(st[2]), st[3], st[4])
        if op == "adecl" and st[3]:
            return (op, st[1], st[2], [rec(it) for it in st[3]], st[4])
        return st

    for i, st in enumerate(list(stmts)):
        stmts[i] = one(st)


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


def _inline(prog):
    ids = prog["ids"]
    fns = prog["fns"]
    ctr = [0]
    eligible = {name: fn for name, fn in fns.items() if _inline_ok(fn, fns, ids)}
    calls = {name: _direct_calls(fn["body"]) for name, fn in eligible.items()}
    pending = {name: {c for c in calls[name] if c in eligible and c != name} for name in eligible}
    order = []
    ready = [name for name, deps in pending.items() if not deps]
    seen = set()
    while ready:
        name = ready.pop()
        if name in seen:
            continue
        seen.add(name)
        order.append(name)
        for other, deps in pending.items():
            if name in deps:
                deps.remove(name)
                if not deps and other not in seen and other not in ready:
                    ready.append(other)
    for name in order:
        src = fns[name]
        for fn in list(fns.values()):
            if fn["name"] == name or fn.get("divzero") != src.get("divzero"):
                continue
            fn["body"] = _paste_calls(fn["body"], name, src, ids, ctr)
        for obj in prog["objects"].values():
            for fn in obj["methods"].values():
                if fn.get("divzero") != src.get("divzero"):
                    continue
                fn["body"] = _paste_calls(fn["body"], name, src, ids, ctr)


def _marked(ids, iid):
    info = ids.get(iid, {})
    return bool(info.get("time") or info.get("xor") or info.get("mutex") or info.get("ephemeral") or info.get("freeze"))


def _plain_body(stmts, ids):
    for st in stmts:
        op = st[0]
        if op in ("decl", "adecl") and _marked(ids, st[1]):
            return False
        if op == "if" and not (_plain_body(st[2], ids) and _plain_body(st[3], ids)):
            return False
    return True


def _always_returns(stmts):
    for st in stmts:
        op = st[0]
        if op == "return":
            return True
        if op == "if" and _always_returns(st[2]) and _always_returns(st[3]):
            return True
    return False


def _inline_ok(fn, fns, ids):
    if fn.get("method") or fn.get("require") or fn.get("ensure") or fn.get("out") is None:
        return False
    if not _scalar(fn["out"]):
        return False
    for p in fn["params"]:
        if not _scalar(p["ty"]):
            return False
    if fn["name"] in _direct_calls(fn["body"]):
        return False
    count = _stmt_count(fn["body"])
    if count == 0 or count > 48:
        return False
    if not _straight(fn["body"]) or not _plain_body(fn["body"], ids) or not _always_returns(fn["body"]):
        return False
    return True


def _straight(stmts):
    for st in stmts:
        op = st[0]
        if op in ("decl", "store", "expr", "return"):
            continue
        if op == "if":
            if not _straight(st[2]) or not _straight(st[3]):
                return False
            continue
        return False
    return True


def _direct_calls(stmts):
    found = set()

    def see(e):
        if not isinstance(e, dict):
            return
        if e.get("k") == "call" and e.get("name"):
            found.add(e["name"])
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
                walk(st[2])
                walk(st[3])
            elif op == "loop":
                walk(st[1])
            elif op == "do":
                walk(st[3])
                walk(st[4])

    walk(stmts)
    return found


def _stmt_count(stmts):
    n = 0
    for st in stmts:
        n += 1
        if st[0] == "if":
            n += _stmt_count(st[2]) + _stmt_count(st[3])
    return n


def _paste_calls(stmts, name, fn, ids, ctr):
    out = []
    for st in stmts:
        pre, st2 = _paste_stmt(st, name, fn, ids, ctr)
        out.extend(pre)
        out.append(st2)
    return out


def _paste_stmt(st, name, fn, ids, ctr):
    op = st[0]
    if op == "decl":
        pre, expr = _paste_expr(st[3], name, fn, ids, ctr)
        return pre, ("decl", st[1], st[2], expr, st[4])
    if op == "store":
        pre, rhs = _paste_expr(st[2], name, fn, ids, ctr)
        return pre, ("store", st[1], rhs, st[3])
    if op == "expr":
        pre, expr = _paste_expr(st[1], name, fn, ids, ctr)
        return pre, ("expr", expr, st[2])
    if op == "return":
        pre, expr = _paste_expr(st[1], name, fn, ids, ctr)
        return pre, ("return", expr, st[2])
    if op == "if":
        pre, cond = _paste_expr(st[1], name, fn, ids, ctr)
        return pre, ("if", cond, _paste_calls(st[2], name, fn, ids, ctr), _paste_calls(st[3], name, fn, ids, ctr), st[4])
    if op == "loop":
        return [], ("loop", _paste_calls(st[1], name, fn, ids, ctr), st[2])
    if op == "do":
        pre, count = _paste_expr(st[1], name, fn, ids, ctr)
        return pre, ("do", count, st[2], _paste_calls(st[3], name, fn, ids, ctr), _paste_calls(st[4], name, fn, ids, ctr), *st[5:])
    if op == "time":
        pre, watch = _paste_expr(st[2], name, fn, ids, ctr)
        return pre, ("time", st[1], watch, _paste_calls(st[3], name, fn, ids, ctr), st[4])
    return [], st


def _paste_expr(e, name, fn, ids, ctr):
    if isinstance(e, list):
        pre = []
        out = []
        for item in e:
            extra, child = _paste_expr(item, name, fn, ids, ctr)
            pre.extend(extra)
            out.append(child)
        return pre, out
    if not isinstance(e, dict):
        return [], e
    # The right of && and || may not run. A call there stays a call.
    if e.get("k") == "bin" and e.get("op") in ("&&", "||"):
        e = dict(e)
        pre, left = _paste_expr(e.get("a"), name, fn, ids, ctr)
        e["a"] = left
        return pre, e
    e = dict(e)
    pre = []
    for key, val in list(e.items()):
        extra, child = _paste_expr(val, name, fn, ids, ctr)
        pre.extend(extra)
        e[key] = child
    args = e.get("args")
    if e.get("k") == "call" and e.get("name") == name and isinstance(args, list) and len(args) == len(fn["params"]):
        extra, result = _splice(e, fn, ids, ctr)
        pre.extend(extra)
        return pre, result
    return pre, e


def _new_id(ids, ctr, info):
    info = dict(info)
    while True:
        ctr[0] += 1
        nid = f"inl.{ctr[0]}"
        if nid not in ids:
            break
    info["id"] = nid
    ids[nid] = info
    return nid


def _splice(call, fn, ids, ctr):
    mapping = {}

    def fresh(old):
        nid = _new_id(ids, ctr, ids[old])
        mapping[old] = nid
        return nid

    line = fn["line"]
    pre = []
    for param, arg in zip(fn["params"], call["args"]):
        pre.append(("decl", fresh(param["id"]), param["ty"], arg, line))
    _collect_decls(fn["body"], fresh)
    body = _remap_stmts(fn["body"], mapping)
    result = _new_id(ids, ctr, {"ty": fn["out"], "ro": False, "name": "inl"})
    pre.append(("decl", result, fn["out"], None, line))
    if _single_tail(body):
        pre.extend(_return_to_store(body, result, fn["out"], line))
    else:
        body = _returns_to_leaves(body, result, fn["out"], line)
        body.append(("leave", line))
        pre.append(("loop", body, line))
    return pre, {"k": "name", "id": result, "ty": fn["out"], "name": "inl"}


def _single_tail(stmts):
    found = [0]

    def walk(body):
        for st in body:
            if st[0] == "return":
                found[0] += 1
            elif st[0] == "if":
                walk(st[2])
                walk(st[3])

    walk(stmts)
    return found[0] == 1 and bool(stmts) and stmts[-1][0] == "return"


def _collect_decls(stmts, fresh):
    for st in stmts:
        op = st[0]
        if op in ("decl", "adecl"):
            fresh(st[1])
        elif op == "if":
            _collect_decls(st[2], fresh)
            _collect_decls(st[3], fresh)
        elif op == "loop":
            _collect_decls(st[1], fresh)
        elif op == "do":
            fresh(st[2])
            _collect_decls(st[3], fresh)
            _collect_decls(st[4], fresh)
        elif op == "time":
            _collect_decls(st[3], fresh)


def _remap_stmts(stmts, mapping):
    out = []
    for st in stmts:
        op = st[0]
        if op == "decl":
            out.append(("decl", mapping[st[1]], st[2], _remap(st[3], mapping), st[4]))
        elif op == "adecl":
            out.append(("adecl", mapping[st[1]], st[2], _remap(st[3], mapping), st[4]))
        elif op == "store":
            out.append(("store", _remap(st[1], mapping), _remap(st[2], mapping), st[3]))
        elif op == "expr":
            out.append(("expr", _remap(st[1], mapping), st[2]))
        elif op == "return":
            out.append(("return", _remap(st[1], mapping), st[2]))
        elif op == "if":
            out.append(("if", _remap(st[1], mapping), _remap_stmts(st[2], mapping), _remap_stmts(st[3], mapping), st[4]))
        elif op == "loop":
            out.append(("loop", _remap_stmts(st[1], mapping), st[2]))
        elif op == "do":
            out.append(("do", _remap(st[1], mapping), mapping[st[2]], _remap_stmts(st[3], mapping), _remap_stmts(st[4], mapping), *st[5:]))
        elif op == "time":
            out.append(("time", st[1], _remap(st[2], mapping), _remap_stmts(st[3], mapping), st[4]))
        else:
            out.append(st)
    return out


def _remap(e, mapping):
    if isinstance(e, list):
        return [_remap(x, mapping) for x in e]
    if not isinstance(e, dict):
        return e
    e = {k: _remap(v, mapping) for k, v in e.items()}
    if e.get("k") == "name" and e.get("id") in mapping:
        e["id"] = mapping[e["id"]]
    return e


def _return_to_store(stmts, result, ty, line):
    out = []
    for st in stmts:
        op = st[0]
        if op == "return":
            if st[1] is not None:
                out.append(("store", {"k": "name", "id": result, "ty": ty, "name": "inl"}, st[1], line))
        elif op == "if":
            out.append(("if", st[1], _return_to_store(st[2], result, ty, line), _return_to_store(st[3], result, ty, line), st[4]))
        else:
            out.append(st)
    return out


def _returns_to_leaves(stmts, result, ty, line):
    out = []
    for st in stmts:
        op = st[0]
        if op == "return":
            if st[1] is not None:
                out.append(("store", {"k": "name", "id": result, "ty": ty, "name": "inl"}, st[1], line))
            out.append(("leave", line))
        elif op == "if":
            out.append(("if", st[1], _returns_to_leaves(st[2], result, ty, line), _returns_to_leaves(st[3], result, ty, line), st[4]))
        elif op == "loop":
            out.append(("loop", _returns_to_leaves(st[1], result, ty, line), st[2]))
        elif op == "do":
            out.append(("do", st[1], st[2], _returns_to_leaves(st[3], result, ty, line), _returns_to_leaves(st[4], result, ty, line), *st[5:]))
        else:
            out.append(st)
    return out
