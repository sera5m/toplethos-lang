"""Compile-time @latex_internal: LaTeX body → Toplethos statements."""
from __future__ import annotations
import re


def latex_to_toplethos(src: str) -> str:
    s = src
    for a, b in (
        ("$$", " "), ("$", " "),
        (r"\[", " "), (r"\]", " "),
        (r"\left", ""), (r"\right", ""),
        (r"\cdot", "*"), (r"\times", "*"), (r"\div", "/"),
        (r"\pi", "pi"), (r"\theta", "theta"),
        (r"\alpha", "alpha"), (r"\beta", "beta"), (r"\omega", "omega"),
        (r"\\", "\n"),
    ):
        s = s.replace(a, b)

    def frac_once(t: str):
        i = t.find(r"\frac")
        if i < 0:
            return t, False
        q = i + 5
        while q < len(t) and t[q].isspace():
            q += 1
        if q >= len(t) or t[q] != "{":
            return t, False

        def grab(start):
            d, j = 1, start + 1
            inner = start + 1
            while j < len(t) and d:
                if t[j] == "{":
                    d += 1
                elif t[j] == "}":
                    d -= 1
                    if d == 0:
                        return t[inner:j], j + 1
                j += 1
            return None, start

        num, q = grab(q)
        if num is None:
            return t, False
        while q < len(t) and t[q].isspace():
            q += 1
        if q >= len(t) or t[q] != "{":
            return t, False
        den, q2 = grab(q)
        if den is None:
            return t, False
        return t[:i] + "((%s)/(%s))" % (num, den) + t[q2:], True

    for _ in range(32):
        s, hit = frac_once(s)
        if not hit:
            break
    s = s.replace(r"\sin", " sin").replace(r"\cos", " cos").replace(r"\tan", " tan")

    def wrap_trig(t: str) -> str:
        out = []
        i = 0
        names = ("sin", "cos", "tan")
        while i < len(t):
            hit = None
            for n in names:
                if t.startswith(n, i) and (i == 0 or not t[i - 1].isalnum()):
                    nxt = i + len(n)
                    if nxt < len(t) and t[nxt] != "(" and not t[nxt].isalnum():
                        hit = n
                        break
            if hit:
                i += len(hit)
                while i < len(t) and t[i].isspace():
                    i += 1
                out.append(hit + "(")
                if i < len(t) and t[i] == "{":
                    d, i = 1, i + 1
                    while i < len(t) and d:
                        if t[i] == "{":
                            d += 1
                        elif t[i] == "}":
                            d -= 1
                            if d == 0:
                                i += 1
                                break
                        if d:
                            out.append(t[i])
                            i += 1
                elif i < len(t) and t[i] == "(":
                    d = 0
                    while i < len(t):
                        if t[i] == "(":
                            d += 1
                        elif t[i] == ")":
                            d -= 1
                        out.append(t[i])
                        i += 1
                        if d == 0:
                            break
                    continue
                else:
                    while i < len(t) and (t[i].isalnum() or t[i] == "_"):
                        out.append(t[i])
                        i += 1
                out.append(")")
                continue
            out.append(t[i])
            i += 1
        return "".join(out)

    s = wrap_trig(s)
    s = re.sub(r"\^\{([^}]+)\}", r"**(\1)", s)
    s = re.sub(r"\^(\d+)", r"**\1", s)
    s = s.replace("{", "(").replace("}", ")")
    s = re.sub(r"(\d)\s*([A-Za-z(])", r"\1*\2", s)
    lines = []
    for raw in re.split(r"[\n;]+", s):
        line = raw.strip()
        if line:
            lines.append(line)
    if not lines:
        return "return 0;\n"
    body = []
    for line in lines[:-1]:
        body.append(line + ";")
    last = lines[-1]
    if "=" in last:
        body.append(last + ";")
    else:
        body.append("return " + last + ";")
    return "\n".join(body) + "\n"


def preprocess(src: str) -> str:
    """Rewrite @latex_internal { ... } bodies in a full .vul file."""
    out = []
    i = 0
    key = "@latex_internal"
    while i < len(src):
        j = src.find(key, i)
        if j < 0:
            out.append(src[i:])
            break
        out.append(src[i:j + len(key)])
        k = j + len(key)
        while k < len(src) and src[k].isspace():
            out.append(src[k])
            k += 1
        if k >= len(src) or src[k] != "{":
            i = k
            continue
        out.append("{")
        k += 1
        d, start = 1, k
        while k < len(src) and d:
            if src[k] == "{":
                d += 1
            elif src[k] == "}":
                d -= 1
                if d == 0:
                    break
            k += 1
        inner = src[start:k]
        out.append("\n" + latex_to_toplethos(inner))
        if k < len(src) and src[k] == "}":
            out.append("}")
            k += 1
        i = k
    return "".join(out)
