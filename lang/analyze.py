"""Reference table for one program. Both hosts can read it. It does not change values.

Counts are source sites, not how many times a loop runs.
"""


def analyze(prog):
    ids = prog["ids"]
    out = {}
    fns = list(prog["fns"].values()) + [
        m for obj in prog["objects"].values() for m in obj["methods"].values()
    ]
    for fn in fns:
        key = fn["name"] if not fn["method"] else fn["method"] + "." + fn["name"]
        rec = {
            "name": fn["name"],
            "line": fn["line"],
            "params": [p["name"] for p in fn["params"]],
            "out": fn["out"][0] if fn["out"] else None,
            "names": {},
            "calls": [],
            "callers": [],
            "loops": 0,
            "loops_at": [],
            "forward": False,
            "effects": [],
            "shape": [],
            "decisions": 0,
        }
        for p in fn["params"]:
            _bind(rec, ids, p["id"], p["ty"], fn["line"])
        _walk(fn["body"], rec, ids)
        rec["shape"] = _shape(rec)
        rec["forward"] = bool(rec["loops_at"]) and all(row["forward"] for row in rec["loops_at"])
        rec["complex"] = "O(n)" if rec["loops"] and rec["forward"] else "unknown"
        rec["pure"] = not rec["effects"] and not rec["calls"]
        out[key] = rec
    for key, rec in out.items():
        for callee in rec["calls"]:
            if callee in out and key not in out[callee]["callers"]:
                out[callee]["callers"].append(key)
    return out


def report(prog, src=None):
    ann = prog.get("ann") or analyze(prog)
    level = prog.get("opt") or "01"
    if level == "03":
        from ikitaku_opt import tag
        tag(prog)
    tagged = {}
    for fn in prog["fns"].values():
        if fn.get("ikitaku"):
            tagged[fn["name"]] = " ".join(fn["ikitaku"])
    for oname, obj in prog["objects"].items():
        for fn in obj["methods"].values():
            if fn.get("ikitaku"):
                tagged[oname + "." + fn["name"]] = " ".join(fn["ikitaku"])
    lines = [f"opt {level}", ""]
    for key in sorted(ann):
        lines.extend(_fn_record(key, ann[key], tagged.get(key)))
        lines.append("")
    for key in sorted(ann):
        rec = ann[key]
        for name in sorted(rec["names"]):
            lines.extend(_name_record(key, name, rec["names"][name]))
            lines.append("")
        for row in rec["loops_at"]:
            lines.extend(_loop_record(key, row))
            lines.append("")
    if src is not None:
        lines.append("--- source ---")
        lines.extend(_annotate(src, ann))
    text = "\n".join(lines).rstrip() + "\n"
    return text


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


def _fn_record(key, rec, tag):
    eff = ",".join(rec["effects"]) if rec["effects"] else "pure" if rec["pure"] else "calls"
    rows = [
        "fn {",
        f"  name {key}",
        f"  line {rec['line']}",
        f"  params {_words(rec['params'])}",
        f"  out {rec['out'] or '-'}",
        f"  shape {_words(rec['shape'])}",
        f"  complex {rec['complex']}",
        f"  loops {rec['loops']}",
        f"  forward {'yes' if rec['forward'] else 'no'}",
        f"  effect {eff}",
        f"  calls {_words(rec['calls'])}",
        f"  callers {_words(rec['callers'])}",
    ]
    if tag:
        rows.append(f"  tag {tag}")
    rows.append("}")
    return rows


def _name_record(fn, name, row):
    decl = 1 if row["decl"] else 0
    refs = decl + row["write"] + row["read"]
    return [
        "name {",
        f"  fn {fn}",
        f"  name {name}",
        f"  ty {row['ty']}",
        f"  decl {row['decl'] or 0}",
        f"  reads {_nums(row['reads'])}",
        f"  read_n {row['read']}",
        f"  writes {_nums(row['writes'])}",
        f"  write_n {row['write']}",
        f"  refs {refs}",
        "}",
    ]


def _loop_record(fn, row):
    return [
        "loop {",
        f"  fn {fn}",
        f"  line {row['line']}",
        f"  forward {'yes' if row['forward'] else 'no'}",
        f"  cursor {_words(row['cursor'])}",
        f"  bound {_words(row['bound'])}",
        "}",
    ]


def _annotate(src, ann):
    long_at = {}
    short_at = {}
    loop_at = {}
    fn_at = {}
    for key, rec in ann.items():
        fn_at.setdefault(rec["line"], []).append(
            f"fn {key} shape {_words(rec['shape'])} complex {rec['complex']} "
            f"callers {_words(rec['callers'])}"
        )
        for name, row in rec["names"].items():
            if row["decl"]:
                decl = 1
                refs = decl + row["write"] + row["read"]
                long_at.setdefault(row["decl"], []).append(
                    f"{name} decl line {row['decl']} | read {_nums(_uniq(row['reads']))} | "
                    f"write {_nums(_uniq(row['writes']))} | refs {refs} "
                    f"(1 decl, {row['write']} write, {row['read']} read)"
                )
            for line in _uniq(row["reads"]):
                short_at.setdefault(line, []).append(f"{name} read")
            for line in _uniq(row["writes"]):
                short_at.setdefault(line, []).append(f"{name} write")
        for row in rec["loops_at"]:
            loop_at.setdefault(row["line"], []).append(
                "loop forward cursor {c} bound {b}".format(
                    c=_words(row["cursor"]), b=_words(row["bound"])
                )
                if row["forward"]
                else "loop not forward"
            )
    out = []
    for n, line in enumerate(src.splitlines(), 1):
        parts = []
        parts.extend(fn_at.get(n, []))
        parts.extend(loop_at.get(n, []))
        parts.extend(long_at.get(n, []))
        declared = set()
        for bit in long_at.get(n, []):
            declared.add(bit.split(" ", 1)[0])
        for bit in short_at.get(n, []):
            if bit.split(" ", 1)[0] in declared and bit.endswith(" write"):
                continue
            if bit not in parts:
                parts.append(bit)
        if parts:
            line = line + "  <<<" + " | ".join(parts) + ">>>"
        out.append(f"{n:4d} | {line}")
    return out


def _words(items):
    return " ".join(items) if items else "-"


def _nums(items):
    return " ".join(str(n) for n in items) if items else "-"


def _uniq(items):
    seen = []
    for n in items:
        if n not in seen:
            seen.append(n)
    return seen


def _walk(stmts, rec, ids):
    for st in stmts:
        op = st[0]
        line = st[-1] if isinstance(st[-1], int) else 0
        if op in ("decl", "store", "expr", "return"):
            if op == "decl":
                _bind(rec, ids, st[1], st[2], line)
                if st[3] is not None:
                    _expr(st[3], rec, ids, False, line)
            elif op == "store":
                inc = _inc_id(st)
                if inc is not None:
                    _touch(rec, ids, inc, True, line)
                    _expr(st[2], rec, ids, False, line)
                else:
                    _expr(st[1], rec, ids, True, line)
                    _expr(st[2], rec, ids, False, line)
            elif op == "expr":
                _expr(st[1], rec, ids, False, line)
            elif st[1] is not None:
                _expr(st[1], rec, ids, False, line)
        elif op == "if":
            rec["decisions"] += 1
            _expr(st[1], rec, ids, False, line)
            _walk(st[2], rec, ids)
            _walk(st[3], rec, ids)
        elif op == "loop":
            rec["loops"] += 1
            facts = _cursor_facts(st[1], ids)
            rec["loops_at"].append({
                "line": line,
                "forward": bool(facts["cursor"]),
                "cursor": facts["cursor"],
                "bound": facts["bound"],
            })
            _walk(st[1], rec, ids)
        elif op == "do":
            rec["loops"] += 1
            bound = []
            if isinstance(st[1], dict) and st[1].get("k") == "name":
                info = ids.get(st[1]["id"], {})
                if info.get("name"):
                    bound.append(info["name"])
            rec["loops_at"].append({
                "line": line, "forward": True, "cursor": ["i"], "bound": bound,
            })
            _bind(rec, ids, st[2], ("i32",), line)
            _expr(st[1], rec, ids, False, line)
            _walk(st[3], rec, ids)
            if len(st) > 4 and st[4]:
                _walk(st[4], rec, ids)
        elif op == "adecl":
            _bind(rec, ids, st[1], st[2], line)
            for item in st[3] or []:
                _expr(item, rec, ids, False, line)
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
    row = rec["names"].get(name)
    if row is None:
        row = _blank(ty, line)
        rec["names"][name] = row
    elif not row["decl"]:
        row["decl"] = line
    if info.get("time") in ("static", "config"):
        _add(rec, "static")


def _blank(ty, decl):
    return {"ty": _ty(ty), "write": 0, "read": 0, "line": decl, "decl": decl, "reads": [], "writes": []}


def _touch(rec, ids, iid, write, line):
    info = ids.get(iid, {})
    name = info.get("name", iid)
    row = rec["names"].get(name)
    if row is None:
        row = _blank(info.get("ty"), 0)
        rec["names"][name] = row
    if write:
        row["write"] += 1
        row["writes"].append(line)
        if info.get("time") in ("static", "config"):
            _add(rec, "static")
    else:
        row["read"] += 1
        row["reads"].append(line)


def _expr(e, rec, ids, write, line):
    if not isinstance(e, dict):
        return
    k = e.get("k")
    if k == "name":
        _touch(rec, ids, e["id"], write, line)
        return
    if k == "call":
        if e["name"] in ("print", "loadbmp", "loadbytes"):
            _add(rec, e["name"])
        elif e["name"] not in ("size",):
            if e["name"] not in rec["calls"]:
                rec["calls"].append(e["name"])
        for a in e.get("args") or []:
            _expr(a, rec, ids, False, line)
        return
    if k == "method":
        name = e.get("name")
        if name and name not in rec["calls"]:
            rec["calls"].append(name)
        _expr(e.get("a"), rec, ids, False, line)
        for a in e.get("args") or []:
            _expr(a, rec, ids, False, line)
        return
    if k == "upd":
        _expr(e.get("a"), rec, ids, False, line)
        _expr(e.get("a"), rec, ids, True, line)
        return
    if k == "index":
        _expr(e.get("a"), rec, ids, write, line)
        _expr(e.get("i"), rec, ids, False, line)
        return
    if k == "field":
        _expr(e.get("a"), rec, ids, write, line)
        return
    if k == "deref":
        _expr(e.get("a"), rec, ids, False, line)
        return
    if k == "loadbmp":
        _add(rec, "loadbmp")
        if e.get("id"):
            _touch(rec, ids, e["id"], True, line)
        return
    if k == "loadbytes":
        _add(rec, "loadbytes")
        if e.get("id"):
            _touch(rec, ids, e["id"], True, line)
        return
    for key in ("a", "b", "i"):
        if isinstance(e.get(key), dict):
            _expr(e[key], rec, ids, False, line)
    for a in e.get("args") or []:
        _expr(a, rec, ids, False, line)


def _inc_id(st):
    if st[0] != "store":
        return None
    lhs, rhs = st[1], st[2]
    if not isinstance(lhs, dict) or lhs.get("k") != "name":
        return None
    if not isinstance(rhs, dict) or rhs.get("k") != "bin" or rhs.get("op") != "+":
        return None
    a, b = rhs.get("a"), rhs.get("b")
    if _is_one(b) and isinstance(a, dict) and a.get("k") == "name" and a.get("id") == lhs.get("id"):
        return lhs["id"]
    if _is_one(a) and isinstance(b, dict) and b.get("k") == "name" and b.get("id") == lhs.get("id"):
        return lhs["id"]
    return None


def _is_one(e):
    return isinstance(e, dict) and e.get("k") == "int" and e.get("n") == 1


def _cursor_facts(stmts, ids):
    writes = {}
    comps = []

    def write(iid, kind):
        writes.setdefault(iid, set()).add(kind)

    def walk_expr(e, writing):
        if not isinstance(e, dict):
            return
        k = e.get("k")
        if k == "name":
            if writing:
                write(e["id"], "other")
            return
        if k == "index":
            walk_expr(e.get("a"), writing)
            walk_expr(e.get("i"), False)
            return
        if k == "upd":
            a = e.get("a")
            if isinstance(a, dict) and a.get("k") == "name" and e.get("op") == "++":
                write(a["id"], "inc")
            else:
                walk_expr(a, True)
            return
        if k == "bin" and e.get("op") in ("<", ">", "<=", ">="):
            a, b = e.get("a"), e.get("b")
            if (
                isinstance(a, dict) and isinstance(b, dict)
                and a.get("k") == "name" and b.get("k") == "name"
            ):
                comps.append((a["id"], b["id"]))
        if k in ("field", "deref"):
            walk_expr(e.get("a"), writing if k == "field" else False)
            return
        for key in ("a", "b", "i"):
            if isinstance(e.get(key), dict):
                walk_expr(e[key], False)
        for a in e.get("args") or []:
            walk_expr(a, False)

    def walk(sts):
        for st in sts:
            op = st[0]
            if op == "store":
                iid = _inc_id(st)
                if iid is not None:
                    write(iid, "inc")
                    walk_expr(st[2], False)
                else:
                    walk_expr(st[1], True)
                    walk_expr(st[2], False)
            elif op == "decl":
                write(st[1], "other")
                if st[3] is not None:
                    walk_expr(st[3], False)
            elif op == "expr":
                walk_expr(st[1], False)
            elif op == "return" and st[1] is not None:
                walk_expr(st[1], False)
            elif op == "if":
                walk_expr(st[1], False)
                walk(st[2])
                walk(st[3])
            elif op == "loop":
                walk(st[1])
            elif op == "do":
                walk_expr(st[1], False)
                walk(st[3])
                if len(st) > 4 and st[4]:
                    walk(st[4])
            elif op == "adecl":
                for item in st[3] or []:
                    walk_expr(item, False)

    walk(stmts)
    cursor_ids = []
    cursor = []
    for iid, kinds in writes.items():
        info = ids.get(iid, {})
        if kinds == {"inc"} and _ty(info.get("ty")) in ("i32", "i64"):
            cursor_ids.append(iid)
            cursor.append(info.get("name", iid))
    bound = []
    seen = set()
    for a, b in comps:
        for cur, other in ((a, b), (b, a)):
            if cur not in cursor_ids or other in writes or other in seen:
                continue
            info = ids.get(other, {})
            if _ty(info.get("ty")) in ("i32", "i64"):
                seen.add(other)
                bound.append(info.get("name", other))
    return {"cursor": sorted(cursor), "bound": sorted(bound)}


def _shape(rec):
    shape = []
    if rec["loops"]:
        shape.append("repeat")
    if rec["decisions"]:
        shape.append("decision")
    if any(x in rec["effects"] for x in ("print", "loadbmp", "loadbytes")):
        shape.append("emit" if "print" in rec["effects"] else "absorb")
    if rec["calls"]:
        shape.append("sequence")
    if not shape:
        shape.append("map")
    return shape


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
