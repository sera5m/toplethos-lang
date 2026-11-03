"""Reference table for one program. Both hosts can read it. It does not change values."""


def analyze(prog):
    ids = prog["ids"]
    out = {}
    for fn in list(prog["fns"].values()) + [
        m for obj in prog["objects"].values() for m in obj["methods"].values()
    ]:
        rec = {
            "name": fn["name"],
            "line": fn["line"],
            "params": [p["name"] for p in fn["params"]],
            "out": fn["out"][0] if fn["out"] else None,
            "names": {},
            "calls": [],
            "loops": 0,
            "forward": False,
            "effects": [],
            "shape": [],
            "decisions": 0,
        }
        _walk(fn["body"], rec, ids)
        rec["shape"] = _shape(rec)
        rec["complex"] = "O(n)" if rec["forward"] and rec["loops"] else "unknown"
        rec["pure"] = not rec["effects"] and not rec["calls"]
        out[fn["name"] if not fn["method"] else fn["method"] + "." + fn["name"]] = rec
    return out


def report(prog):
    ann = prog.get("ann") or analyze(prog)
    level = prog.get("opt") or "01"
    if level == "03":
        from ikitaku_opt import tag
        tag(prog)
    lines = [f"opt {level}"]
    tagged = {}
    for fn in prog["fns"].values():
        if fn.get("ikitaku"):
            tagged[fn["name"]] = " ".join(fn["ikitaku"])
    for oname, obj in prog["objects"].items():
        for fn in obj["methods"].values():
            if fn.get("ikitaku"):
                tagged[oname + "." + fn["name"]] = " ".join(fn["ikitaku"])
    for key in sorted(ann):
        rec = ann[key]
        eff = ",".join(rec["effects"]) if rec["effects"] else "pure" if rec["pure"] else "calls"
        mark = f"  tag {tagged[key]}" if key in tagged else ""
        lines.append(
            f"fn {key}  shape {' '.join(rec['shape']) or '-'}  {rec['complex']}  loops {rec['loops']}  {eff}{mark}"
        )
        for name in sorted(rec["names"]):
            n = rec["names"][name]
            lines.append(
                f"  {n['ty']} {name}  write {n['write']}  read {n['read']}  line {n['line']}"
            )
        for call in rec["calls"]:
            lines.append(f"  call {call}")
    return "\n".join(lines) + "\n"


def impure(prog, fn):
    """Why an @inlined function cannot be compiled and called. Empty means it can."""
    if fn["method"]:
        return "method"
    if fn["out"] not in (None, ("i32",), ("i64",), ("f64",), ("bool",)):
        return "result"
    for p in fn["params"]:
        if p["ty"] not in (("i32",), ("i64",), ("f64",), ("bool",)):
            return "parameter"
    rec = (prog.get("ann") or {}).get(fn["name"])
    if rec is None:
        return "unanalyzed"
    if rec["effects"] or rec["calls"]:
        return ",".join(rec["effects"] + [f"call {c}" for c in rec["calls"]]) or "effect"
    return ""


def _walk(stmts, rec, ids):
    for st in stmts:
        op = st[0]
        if op in ("decl", "store", "expr", "return"):
            if op == "decl":
                _bind(rec, ids, st[1], st[2], st[4])
                if st[3] is not None:
                    _expr(st[3], rec, ids, False)
            elif op == "store":
                _expr(st[1], rec, ids, True)
                _expr(st[2], rec, ids, False)
            elif op == "expr":
                _expr(st[1], rec, ids, False)
            elif st[1] is not None:
                _expr(st[1], rec, ids, False)
        elif op == "if":
            rec["decisions"] += 1
            _expr(st[1], rec, ids, False)
            _walk(st[2], rec, ids)
            _walk(st[3], rec, ids)
        elif op == "loop":
            rec["loops"] += 1
            before = _reads(rec)
            _walk(st[1], rec, ids)
            if _forward_index(st[1], rec, before):
                rec["forward"] = True
        elif op == "do":
            rec["loops"] += 1
            rec["forward"] = True
            _expr(st[1], rec, ids, False)
            _walk(st[3], rec, ids)
            if len(st) > 4 and st[4]:
                _walk(st[4], rec, ids)
        elif op == "adecl":
            _bind(rec, ids, st[1], st[2], st[4] if len(st) > 4 else 0)
            for item in st[3] or []:
                _expr(item, rec, ids, False)
        elif op in ("fork", "finish", "lock", "unlock", "time"):
            rec["effects"].append(op)
            if op == "time" and len(st) > 3:
                _walk(st[3], rec, ids)
        elif op == "leave" or op == "continue":
            pass
        else:
            rec["effects"].append(op)


def _bind(rec, ids, iid, ty, line):
    info = ids.get(iid, {})
    name = info.get("name", iid)
    row = rec["names"].setdefault(name, {"ty": _ty(ty), "write": 0, "read": 0, "line": line})
    row["write"] += 1
    if info.get("time") in ("static", "config"):
        _add(rec, "static")


def _expr(e, rec, ids, write):
    if not isinstance(e, dict):
        return
    k = e.get("k")
    if k == "name":
        info = ids.get(e["id"], {})
        name = info.get("name", e["id"])
        row = rec["names"].setdefault(name, {"ty": _ty(e.get("ty")), "write": 0, "read": 0, "line": 0})
        if write:
            row["write"] += 1
            if info.get("time") in ("static", "config"):
                _add(rec, "static")
        else:
            row["read"] += 1
        return
    if k == "call":
        if e["name"] in ("print", "loadbmp"):
            _add(rec, e["name"])
        elif e["name"] not in ("size",):
            if e["name"] not in rec["calls"]:
                rec["calls"].append(e["name"])
        for a in e.get("args") or []:
            _expr(a, rec, ids, False)
        return
    if k == "method":
        name = e.get("name")
        if name and name not in rec["calls"]:
            rec["calls"].append(name)
        _expr(e.get("a"), rec, ids, False)
        for a in e.get("args") or []:
            _expr(a, rec, ids, False)
        return
    if k == "upd":
        _expr(e.get("a"), rec, ids, False)
        _expr(e.get("a"), rec, ids, True)
        return
    if k == "loadbmp":
        _add(rec, "loadbmp")
    if k in ("start", "run"):
        _add(rec, k)
    for key in ("a", "b", "i"):
        if isinstance(e.get(key), dict):
            _expr(e[key], rec, ids, write and key == "a" and k == "upd")
    for a in e.get("args") or []:
        _expr(a, rec, ids, False)


def _shape(rec):
    shape = []
    if rec["loops"]:
        shape.append("repeat")
    if rec["decisions"]:
        shape.append("decision")
    if any(x in rec["effects"] for x in ("print", "loadbmp")):
        shape.append("emit" if "print" in rec["effects"] else "absorb")
    if rec["calls"]:
        shape.append("sequence")
    if not shape:
        shape.append("map")
    return shape


def _ops(rec):
    return rec["effects"]


def _forward_index(body, rec, before):
    # A loop whose integer names are only incremented is a forward scan.
    for name, row in rec["names"].items():
        if row["ty"] not in ("i32", "i64"):
            continue
        prev = before.get(name, 0)
        if row["write"] > prev and row["read"] >= prev:
            return True
    return False


def _reads(rec):
    return {name: row["write"] for name, row in rec["names"].items()}


def _add(rec, effect):
    if effect not in rec["effects"]:
        rec["effects"].append(effect)


def _ty(ty):
    if not ty:
        return "?"
    if ty[0] == "ptr":
        return _ty(ty[1]) + "*"
    if ty[0] == "arr":
        return _ty(ty[1]) + "[]"
    if ty[0] == "obj":
        return ty[1]
    return ty[0]
