"""Reference table for one program. Both hosts can read it. It does not change values.

Counts are source sites, not how many times a loop runs.

Three passes, each once, in order. A pass does not loop until something settles.
"""


def analyze(prog):
    state = {"prog": prog, "ids": prog["ids"], "fns": {}, "log": []}
    _pass_broad(state)
    _pass_function(state)
    _pass_reserved(state)
    prog["passes"] = state["log"]
    return state["fns"]


def _pass_broad(state):
    """Local structure only. No callee atom is known yet."""
    prog = state["prog"]
    ids = state["ids"]
    out = state["fns"]
    fns = list(prog["fns"].values()) + [
        m for obj in prog["objects"].values() for m in obj["methods"].values()
    ]
    for fn in fns:
        key = fn["name"] if not fn["method"] else fn["method"] + "." + fn["name"]
        rec = {
            "name": fn["name"],
            "line": fn["line"],
            "params": [p["name"] for p in fn["params"]],
            "out": _ty(fn["out"]) if fn["out"] else None,
            "names": {},
            "calls": [],
            "callers": [],
            "invoked": [],
            "sites": [],
            "ops": [],
            "incs": [],
            "ranges": [],
            "mutates": False,
            "atom": None,
            "key": key,
            "loops": 0,
            "loops_at": [],
            "cuts": [],
            "blocks": [],
            "forward": False,
            "effects": [],
            "shape": [],
            "decisions": 0,
            "_enums": prog.get("enums") or {},
        }
        for p in fn["params"]:
            _bind(rec, ids, p["id"], p["ty"], fn["line"], param=True)
        if fn.get("here"):
            _bind(rec, ids, fn["here"], ("obj", fn["method"]), fn["line"], param=True)
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
    state["log"].append({"n": 1, "name": "broad", "did": "walk"})


def _pass_function(state):
    """One visit per function, callees first. Then the blocks inside it."""
    fns = state["fns"]
    for key in _callee_first(fns):
        rec = fns[key]
        atom = _local_atom(rec)
        for callee in rec["calls"]:
            other = fns.get(callee)
            if other is None:
                atom = _worst(atom, "impure")
            else:
                known = other["atom"] if other["atom"] else _local_atom(other)
                atom = _worst(atom, known)
        rec["atom"] = atom
    _number_sites(fns)
    _number_ops(fns)
    for rec in fns.values():
        rec["blocks"] = _blocks(rec)
    state["log"].append({"n": 2, "name": "function", "did": "atom block"})


def _pass_reserved(state):
    """Kept for a later pass. It runs, and it changes nothing."""
    state["log"].append({"n": 3, "name": "reserved", "did": "-"})


def _local_atom(rec):
    if rec["mutates"]:
        return "mutable"
    if rec["effects"]:
        return "impure"
    return "pure"


def _worst(a, b):
    rank = {"pure": 0, "impure": 1, "mutable": 2}
    return a if rank[a] >= rank[b] else b


def _callee_first(fns):
    """Postorder. A cycle uses the local atom of the function already open."""
    order = []
    color = {}
    for start in fns:
        if color.get(start):
            continue
        stack = [(start, False)]
        while stack:
            key, ready = stack.pop()
            if ready:
                color[key] = 2
                order.append(key)
                continue
            if color.get(key):
                continue
            color[key] = 1
            stack.append((key, True))
            for callee in fns[key]["calls"]:
                if callee in fns and not color.get(callee):
                    stack.append((callee, False))
    return order


def _blocks(rec):
    cuts = sorted(set(rec["cuts"]) | {row["line"] for row in rec["loops_at"]})
    order = []
    by = {}
    for op in rec["ops"]:
        line = op["line"]
        row = by.get(line)
        if row is None:
            row = {"atom": "pure", "effect": [], "ops": 0}
            by[line] = row
            order.append(line)
        row["ops"] += 1
        atom = _region_atom(op)
        row["atom"] = _worst(row["atom"], atom)
        if atom != "pure":
            word = op.get("effect") or op["name"]
            if word and word not in row["effect"]:
                row["effect"].append(str(word))
    blocks = []
    cur = None
    prev = 0
    for line in order:
        row = by[line]
        split = cur is not None and (
            cur["atom"] != row["atom"] or any(prev < cut <= line for cut in cuts)
        )
        if cur is None or split:
            if cur:
                blocks.append(cur)
            cur = {
                "line": line,
                "end": line,
                "atom": row["atom"],
                "effect": list(row["effect"]),
                "ops": row["ops"],
            }
        else:
            cur["end"] = line
            cur["ops"] += row["ops"]
            for word in row["effect"]:
                if word not in cur["effect"]:
                    cur["effect"].append(word)
        prev = line
    if cur:
        blocks.append(cur)
    for i, block in enumerate(blocks, 1):
        block["id"] = "b" + str(i)
    return blocks


def _region_atom(op):
    if op["kind"] in ("call", "method", "start", "run"):
        atom = op["atom"]
        if atom in ("pure", "impure", "mutable"):
            return atom
        if op.get("effect") in ("print", "loadbmp", "loadbytes", "savebmp", "start", "run"):
            return "impure"
        return "pure"
    return "pure"


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
    for row in prog.get("passes") or []:
        lines.extend(_pass_record(row))
        lines.append("")
    for name in sorted(prog.get("enums") or {}, key=lambda n: prog["enums"][n]["id"]):
        lines.extend(_enum_record(prog["enums"][name], name))
        lines.append("")
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
        for block in rec.get("blocks") or []:
            lines.extend(_block_record(key, block))
            lines.append("")
    for key in sorted(ann):
        for site in ann[key]["sites"]:
            lines.extend(_ptr_record(site))
            lines.append("")
    ops = [op for key in ann for op in ann[key]["ops"]]
    ops.sort(key=lambda op: int(op["id"][2:]))
    for op in ops:
        lines.extend(_op_record(op))
        lines.append("")
    for key in sorted(ann):
        rec = ann[key]
        if rec["pats"] or rec["ops"]:
            lines.extend(_reduce_record(key, rec))
            lines.append("")
        for pat in rec["pats"]:
            lines.extend(_pat_record(key, pat))
            lines.append("")
    if prog.get("symbols"):
        for name in sorted(prog["symbols"]):
            lines.extend(_symbol_record(name, prog["symbols"][name]))
            lines.append("")
    if src is not None:
        lines.append("--- source ---")
        lines.extend(_annotate(src, ann))
    text = "\n".join(lines).rstrip() + "\n"
    return text


def symbol_table(prog):
    ann = prog.get("ann") or {}
    out = {}
    for name, fn in prog["fns"].items():
        rec = ann.get(name, {})
        calls = []
        for site in rec.get("sites", []):
            if site.get("kind") == "call" and site.get("name"):
                calls.append(site["name"])
        out[name] = {
            "cost": "unknown",
            "time": "unknown",
            "resources": "cpu",
            "inputs": [p["name"] for p in fn["params"]],
            "outputs": [] if fn["out"] is None else ["out"],
            "variables": sorted(rec.get("names", {})),
            "functions": sorted(set(calls)),
            "invokes": [s["id"] for s in rec.get("sites", [])],
            "calls": 0,
            "edges": {},
        }
    return out


def _pass_record(row):
    return [
        "pass {",
        f"  n {row['n']}",
        f"  name {row['name']}",
        f"  did {row['did']}",
        "}",
    ]


def _block_record(fn, block):
    return [
        "block {",
        f"  fn {fn}",
        f"  id {block['id']}",
        f"  line {block['line']}",
        f"  end {block['end']}",
        f"  atom {block['atom']}",
        f"  effect {_words(block['effect'])}",
        f"  ops {block['ops']}",
        "}",
    ]


def _symbol_record(name, row):
    edges = " ".join(f"{who}:{n}" for who, n in sorted(row["edges"].items())) or "-"
    return [
        "symbol {",
        f"  name {name}",
        f"  cost {row['cost']}",
        f"  time {row['time']}",
        f"  resources {row['resources']}",
        f"  inputs {_words(row['inputs'])}",
        f"  outputs {_words(row['outputs'])}",
        f"  variables {_words(row['variables'])}",
        f"  functions {_words(row['functions'])}",
        f"  invokes {_words(row['invokes'])}",
        f"  calls {row['calls']}",
        f"  edges {edges}",
        "}",
    ]


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


def _enum_record(en, name):
    rows = [
        "enum {",
        f"  name {name}",
        f"  id {en['id']}",
        f"  word {en['word']}",
        f"  width {en['width']}",
    ]
    for item in en["order"]:
        rows.append(f"  {item} {en['items'][item]}")
    rows.append("}")
    return rows


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
        f"  atom {rec.get('atom') or '-'}",
        f"  blocks {len(rec.get('blocks') or [])}",
        f"  calls {_words(rec['calls'])}",
        f"  callers {_words(rec['callers'])}",
        f"  invoked {_words(rec['invoked'])}",
    ]
    if tag:
        rows.append(f"  tag {tag}")
    rows.append("}")
    return rows


def _name_record(fn, name, row):
    decl = 1 if row["decl"] else 0
    refs = decl + row["write"] + row["read"]
    rows = [
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
    ]
    if row.get("word") is not None:
        rows.append(f"  word {row['word']}")
        rows.append(f"  width {row['width']}")
    rows.append("}")
    return rows


def _ptr_record(site):
    return [
        "annptr {",
        f"  id {site['id']}",
        f"  kind {site['kind']}",
        f"  name {site['name']}",
        f"  line {site['line']}",
        f"  caller {site['caller']}",
        f"  decl {site['decl']}",
        f"  decl_line {site['decl_line']}",
        f"  instance {site['instance']}",
        f"  recv {_words([site['recv']]) if site.get('recv') else '-'}",
        f"  args {_words(site['args'])}",
        f"  effect {site['effect']}",
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
    ptr_at = {}
    drop_at = {}
    block_at = {}
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
        for block in rec.get("blocks") or []:
            block_at.setdefault(block["line"], []).append(
                f"block {block['id']} {block['atom']} to {block['end']}"
            )
        for site in rec["sites"]:
            ptr_at.setdefault(site["line"], []).append(
                f"{site['id']} {site['kind']} {site['name']} decl {site['decl']} "
                f"effect {site['effect']} args {_words(site['args'])}"
            )
        for op in rec["ops"]:
            if op.get("drop"):
                _out, into = _dest_word(op)
                drop_at.setdefault(op["line"], []).append(
                    f"drop {op['id']} {op['atom']} {op['name']} into {into}"
                )
    out = []
    for n, line in enumerate(src.splitlines(), 1):
        parts = []
        parts.extend(fn_at.get(n, []))
        parts.extend(block_at.get(n, []))
        parts.extend(loop_at.get(n, []))
        parts.extend(ptr_at.get(n, []))
        parts.extend(drop_at.get(n, []))
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
                    rloc = _expr(st[3], rec, ids, False, line)
                    name = ids.get(st[1], {}).get("name", "?")
                    sop = _add_op(rec, "set", name, line, [rloc])
                    _link_drop(rloc, ("name", name), sop)
            elif op == "store":
                inc = _inc_id(st)
                if inc is not None or (isinstance(st[1], dict) and st[1].get("k") == "name"):
                    rloc = _expr(st[2], rec, ids, False, line)
                    iid = inc if inc is not None else st[1].get("id")
                    _touch(rec, ids, iid, True, line)
                    name = ids.get(iid, {}).get("name", "?")
                    if inc is not None:
                        rec["incs"].append((line, name))
                    sop = _add_op(rec, "set", name, line, [rloc])
                    _link_drop(rloc, ("name", name), sop)
                else:
                    _expr(st[1], rec, ids, True, line)
                    _expr(st[2], rec, ids, False, line)
            elif op == "expr":
                _expr(st[1], rec, ids, False, line)
            elif st[1] is not None:
                rloc = _expr(st[1], rec, ids, False, line)
                _link_drop(rloc, ("out",), None)
        elif op == "if":
            rec["decisions"] += 1
            rec["cuts"].append(line)
            _expr(st[1], rec, ids, False, line)
            _walk(st[2], rec, ids)
            _walk(st[3], rec, ids)
        elif op == "loop":
            rec["loops"] += 1
            rec["cuts"].append(line)
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
            rec["cuts"].append(line)
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
        elif op == "hold":
            rec["effects"].append("hold")
            _walk(st[3], rec, ids)
        elif op == "leave" or op == "continue":
            pass
        else:
            rec["effects"].append(op)


def _bind(rec, ids, iid, ty, line, param=False):
    info = ids.get(iid, {})
    name = info.get("name", iid)
    row = rec["names"].get(name)
    if row is None:
        row = _blank(ty, line)
        rec["names"][name] = row
    elif not row["decl"]:
        row["decl"] = line
    if param:
        row["param"] = True
    if ty and ty[0] == "enum":
        en = rec["_enums"][ty[1]]
        row["word"] = en["word"]
        row["width"] = en["width"]
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
        return ("tmp",)
    k = e.get("k")
    if k == "name":
        _touch(rec, ids, e["id"], write, line)
        name = ids.get(e["id"], {}).get("name", "?")
        op = _add_op(rec, "set" if write else "get", name, line, [])
        return ("name", name, op)
    if k == "call":
        args = e.get("args") or []
        if e["name"] in ("min", "max", "clamp", "abs"):
            for a in args:
                _expr(a, rec, ids, False, line)
            return ("tmp",)
        if e["name"] == "swap":
            for a in args:
                _expr(a, rec, ids, False, line)
            _add(rec, "swap")
            return ("tmp",)
        locs = [_expr(a, rec, ids, False, line) for a in args]
        if e["name"] in ("print", "loadbmp", "loadbytes", "savebmp"):
            _add(rec, e["name"])
            return _call_op(rec, ids, "call", e["name"], line, locs, "-", e["name"])
        if e["name"] not in rec["calls"]:
            rec["calls"].append(e["name"])
        return _call_op(rec, ids, "call", e["name"], line, locs, e["name"], None)
    if k == "method":
        name = e.get("name") or "?"
        decl = (e.get("obj") or "?") + "." + name
        if decl not in rec["calls"]:
            rec["calls"].append(decl)
        recv = _expr(e.get("a"), rec, ids, False, line)
        locs = [_expr(a, rec, ids, False, line) for a in (e.get("args") or [])]
        return _call_op(rec, ids, "method", name, line, locs, decl, None, recv=_loc_word(recv))
    if k == "start":
        _add(rec, "start")
        if e["name"] not in rec["calls"]:
            rec["calls"].append(e["name"])
        return _call_op(rec, ids, "start", e["name"], line, [], e["name"], None)
    if k == "run":
        _add(rec, "run")
        if e["name"] not in rec["calls"]:
            rec["calls"].append(e["name"])
        locs = [("lit", "int")]
        if e.get("extra"):
            cell = ids.get(e["extra"], {}).get("name", "?")
            g = _add_op(rec, "get", cell, line, [])
            locs.append(("name", cell, g))
        return _call_op(rec, ids, "run", e["name"], line, locs, e["name"], None)
    if k == "size":
        loc = _expr(e.get("a"), rec, ids, False, line)
        return _call_op(rec, ids, "call", "size", line, [loc], "-", "pure")
    if k == "upd":
        loc = _expr(e.get("a"), rec, ids, False, line)
        _expr(e.get("a"), rec, ids, True, line)
        return loc if loc[0] == "name" else ("tmp",)
    if k == "index":
        _expr(e.get("a"), rec, ids, write, line)
        _expr(e.get("i"), rec, ids, False, line)
        return ("tmp",)
    if k == "field":
        base = e.get("a")
        if write and isinstance(base, dict) and base.get("k") == "name":
            info = ids.get(base["id"], {})
            row = rec["names"].get(info.get("name"))
            if row and row.get("param"):
                rec["mutates"] = True
        _expr(base, rec, ids, write, line)
        return ("tmp",)
    if k == "deref":
        loc = _expr(e.get("a"), rec, ids, False, line)
        if write:
            rec["mutates"] = True
            pname = loc[1] if loc[0] == "name" else "*"
            _add_op(rec, "set", "*" + str(pname), line, [loc])
        return ("tmp",)
    if k == "loadbmp":
        _add(rec, "loadbmp")
        return _mem_call(rec, ids, "loadbmp", line, e.get("id"))
    if k == "savebmp":
        _add(rec, "savebmp")
        _expr(e.get("w"), rec, ids, False, line)
        _expr(e.get("h"), rec, ids, False, line)
        return _mem_call(rec, ids, "savebmp", line, e.get("id"))
    if k == "loadbytes":
        _add(rec, "loadbytes")
        return _mem_call(rec, ids, "loadbytes", line, e.get("id"))
    if k == "addr":
        return _expr(e.get("a"), rec, ids, False, line)
    if k == "int":
        return ("lit", "int")
    if k == "f64":
        return ("lit", "f64")
    if k == "bool":
        return ("lit", "bool")
    if k == "statecmp":
        return ("tmp",)
    if k == "bin" and e.get("op") == "&&":
        ranged = _range_name(e)
        if ranged:
            rec["ranges"].append({"line": line, "name": ranged})
    for key in ("a", "b", "i"):
        if isinstance(e.get(key), dict):
            _expr(e[key], rec, ids, False, line)
    for a in e.get("args") or []:
        _expr(a, rec, ids, False, line)
    return ("tmp",)


def _mem_call(rec, ids, name, line, iid):
    arg = ids.get(iid, {}).get("name", "?") if iid else None
    locs = []
    if arg:
        _touch(rec, ids, iid, True, line)
        sop = _add_op(rec, "set", arg, line, [])
        locs.append(("name", arg, sop))
    return _call_op(rec, ids, "call", name, line, locs, "-", name)


def _call_op(rec, ids, kind, name, line, locs, decl, effect, recv=None):
    site = {
        "kind": kind,
        "name": name,
        "line": line,
        "caller": rec["key"],
        "decl": decl,
        "recv": recv,
        "args": [_loc_word(a) for a in locs],
        "effect": effect,
    }
    rec["sites"].append(site)
    op = _add_op(rec, "call", name, line, locs, decl)
    op["kind"] = kind
    op["instance"] = site
    op["effect"] = effect
    for i, loc in enumerate(locs):
        _consume(loc, op, i)
    return ("op", op)


def _add_op(rec, atom, name, line, inn=None, decl="-"):
    op = {
        "atom": atom,
        "kind": atom,
        "name": name,
        "line": line,
        "fn": rec["key"],
        "instance": None,
        "parent": None,
        "inn": list(inn or []),
        "drop": False,
        "into": None,
        "decl": decl,
        "effect": None,
    }
    rec["ops"].append(op)
    return op


def _consume(loc, parent, index):
    if not loc:
        return
    if loc[0] == "name" and len(loc) > 2 and loc[2] is not None:
        loc[2]["parent"] = parent
    elif loc[0] == "op":
        _link_drop(loc, ("in", parent, index), parent)


def _link_drop(rloc, dest, parent):
    if not rloc or rloc[0] != "op":
        return
    child = rloc[1]
    child["drop"] = True
    child["into"] = dest
    child["parent"] = parent


def _loc_word(loc):
    if not loc:
        return "?"
    if loc[0] == "name":
        return loc[1]
    if loc[0] == "lit":
        return loc[1]
    if loc[0] == "op":
        return loc[1].get("id") or loc[1].get("name") or "?"
    return "tmp"


def _effect_word(rec):
    if rec.get("mutates") or rec.get("atom") == "mutable":
        extra = ",".join(rec["effects"])
        return "mutable" if not extra else "mutable," + extra
    if rec["effects"]:
        return ",".join(rec["effects"])
    if rec.get("atom") == "impure":
        return "impure"
    if rec["pure"] or rec.get("atom") == "pure":
        return "pure"
    return "calls"


def _number_sites(out):
    n = 1
    for rec in out.values():
        for site in rec["sites"]:
            site["id"] = f"ap{n}"
            site["instance"] = site["id"]
            n += 1
            drec = out.get(site["decl"]) if site["decl"] != "-" else None
            if drec is not None:
                site["decl_line"] = drec["line"]
                site["effect"] = _effect_word(drec)
                drec["invoked"].append(site["id"])
            else:
                site["decl_line"] = 0
                if not site.get("effect"):
                    site["effect"] = "-"


def _number_ops(out):
    n = 1
    for rec in out.values():
        for op in rec["ops"]:
            op["id"] = f"op{n}"
            n += 1
            if op["atom"] == "call":
                op["atom"] = _call_atom(op, out)
    for rec in out.values():
        for op in rec["ops"]:
            site = op.get("instance")
            if isinstance(site, dict):
                site["args"] = [_loc_word(a) for a in op["inn"]] or ["-"]
    for rec in out.values():
        _patterns(rec)


def _range_name(e):
    a, b = e.get("a"), e.get("b")
    if not isinstance(a, dict) or not isinstance(b, dict):
        return None
    if a.get("k") != "bin" or b.get("k") != "bin":
        return None
    if a.get("op") not in ("<", ">", "<=", ">=") or b.get("op") not in ("<", ">", "<=", ">="):
        return None
    left = _rel_name(a)
    right = _rel_name(b)
    if left and left == right:
        return left
    return None


def _rel_name(e):
    a, b = e.get("a"), e.get("b")
    name = other = None
    for side in (a, b):
        if not isinstance(side, dict):
            return None
        if side.get("k") == "name" and side.get("name"):
            name = side["name"]
        elif side.get("k") == "int":
            other = True
    if name and other:
        return name
    return None


def _patterns(rec):
    groups = {}

    def add(kind, line, what, save):
        groups[(kind, line, what)] = groups.get((kind, line, what), 0) + save

    for op in rec["ops"]:
        if op.get("drop") and op.get("kind") in ("call", "method", "start", "run"):
            add("drop", op["line"], op["name"], 1)
    by = {}
    for op in rec["ops"]:
        by.setdefault(op["line"], []).append(op)
    for line, ops in by.items():
        live = {}
        extra = {}
        for op in ops:
            if op["atom"] == "set":
                live[op["name"]] = False
            elif op["atom"] == "get":
                if live.get(op["name"]):
                    extra[op["name"]] = extra.get(op["name"], 0) + 1
                else:
                    live[op["name"]] = True
        for name, n in extra.items():
            add("reuse", line, name, n)
    for line, name in rec.get("incs", []):
        add("inc", line, name, 1)
    for row in rec.get("ranges", []):
        add("range", row["line"], row["name"], 1)
    for row in rec["loops_at"]:
        if row["forward"] and row["cursor"]:
            what = " ".join(row["cursor"])
            if row["bound"]:
                what += " < " + " ".join(row["bound"])
            add("forward", row["line"], what, 0)
    rec["pats"] = [
        {"name": kind, "line": line, "what": what, "save": save}
        for (kind, line, what), save in groups.items()
    ]
    rec["pats"].sort(key=lambda p: (p["line"], p["name"], p["what"]))
    rec["saved"] = sum(
        p["save"] for p in rec["pats"] if p["name"] in ("drop", "reuse", "inc")
    )


def _reduce_record(key, rec):
    forward = sum(1 for p in rec["pats"] if p["name"] == "forward")
    return [
        "reduce {",
        f"  fn {key}",
        f"  ops {len(rec['ops'])}",
        f"  drop {sum(p['save'] for p in rec['pats'] if p['name'] == 'drop')}",
        f"  reuse {sum(p['save'] for p in rec['pats'] if p['name'] == 'reuse')}",
        f"  inc {sum(p['save'] for p in rec['pats'] if p['name'] == 'inc')}",
        f"  range {sum(p['save'] for p in rec['pats'] if p['name'] == 'range')}",
        f"  forward {forward}",
        f"  static {rec['saved']}",
        "}",
    ]


def _pat_record(fn, pat):
    return [
        "pattern {",
        f"  name {pat['name']}",
        f"  fn {fn}",
        f"  line {pat['line']}",
        f"  what {pat['what']}",
        f"  save {pat['save']}",
        "}",
    ]


def _call_atom(op, out):
    if op.get("kind") in ("start", "run"):
        return "impure"
    if op.get("decl") in ("-", None):
        if op.get("effect") in ("print", "loadbytes", "loadbmp", "savebmp"):
            return "impure"
        return "pure"
    drec = out.get(op["decl"])
    if drec is None or drec.get("atom") is None:
        return "impure"
    return drec["atom"]


def _dest_word(op):
    if op["atom"] == "get":
        return op["name"], "-"
    if op["atom"] == "set":
        return op["name"], op["name"]
    into = op.get("into")
    if not op.get("drop") or not into:
        return "tmp", "-"
    if into[0] == "name":
        return into[1], into[1]
    if into[0] == "out":
        return "out", "out"
    if into[0] == "in":
        parent, index = into[1], into[2]
        return "here", f"{parent.get('id', '?')}.in{index}"
    return "tmp", "-"


def _op_record(op):
    out, into = _dest_word(op)
    parent = op.get("parent")
    pid = parent.get("id") if isinstance(parent, dict) else "-"
    inst = op.get("instance")
    iid = inst.get("id") if isinstance(inst, dict) else "-"
    inns = [_loc_word(a) for a in op["inn"]]
    return [
        "op {",
        f"  id {op['id']}",
        f"  atom {op['atom']}",
        f"  kind {op['kind']}",
        f"  fn {op['fn']}",
        f"  name {op['name']}",
        f"  line {op['line']}",
        f"  instance {iid}",
        f"  parent {pid}",
        f"  in {_words(inns)}",
        f"  out {out}",
        f"  into {into}",
        f"  drop {'yes' if op['drop'] else 'no'}",
        "}",
    ]


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
    if any(x in rec["effects"] for x in ("print", "loadbmp", "loadbytes", "savebmp")):
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
    if ty[0] == "enum":
        return "enum " + ty[1]
    return ty[0]
