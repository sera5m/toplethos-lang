"""Both hosts, one meaning. Run from the repo root: python3 conformance/check.py"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lang"))
from front import CompileError, Failure
from host import compile_text, run_binary, run_text

OK = [
    ("arith", "7\n", """
fn main in[] out[i32] {
  print in[1 + 2 * 3];
  return 0;
}
"""),
    ("call", "5\n", """
fn add in[i32 a] out[i32] { return a + 1; }
fn main in[] out[i32] {
  print in[add in[4]];
  return 0;
}
"""),
    ("object", "2\n", """
object Point {
  i32 x;
  fn bump in[] out[] { HERE.x = HERE.x + 1; }
}
fn main in[] out[i32] {
  Point p;
  p.x = 1;
  p.bump in[];
  print in[p.x];
  return 0;
}
"""),
    ("do", "16\n", """
fn main in[] out[i32] {
  i32 s = 0;
  do n[4] { s = s + i; } onFinished { s = s + 10; }
  print in[s];
  return 0;
}
"""),
    ("pointer", "2\n", """
fn bump in[i32* a] out[] { *a = *a + 1; }
fn main in[] out[i32] {
  i32 x = 1;
  bump in[&x];
  print in[x];
  return 0;
}
"""),
    ("bool", "false\n", """
fn main in[] out[i32] {
  bool t = true;
  bool ran = false;
  if t && false { ran = true; }
  if !t || true { ran = false; }
  print in[ran];
  return 0;
}
"""),
    ("array", "1\n0\n12\n", """
fn main in[] out[i32] {
  i32 tape[4] = {1, 2, 3};
  print in[tape[0]];
  print in[tape[3]];
  tape[1] = tape[1] + 10;
  print in[tape[1]];
  return 0;
}
"""),
    ("float", "6.500000000\n", """
fn horner in[f64 x] out[f64] {
  f64 y = 8.0;
  y = 7.0 + x * y;
  y = 1.0 + x * y;
  return y;
}
fn main in[] out[i32] {
  print in[horner in[0.5]];
  return 0;
}
"""),
]

BAD = [
    ("return in out[]", "no designated type out[]", """
fn add in[i32 a] out[] { return a + 1; }
fn main in[] out[i32] { return 0; }
"""),
    ("write i", "writing i in a do", """
fn main in[] out[i32] {
  do n[3] { i = 1; }
  return 0;
}
"""),
    ("if not bool", "not bool", """
fn main in[] out[i32] {
  if 1 { return 1; }
  return 0;
}
"""),
    ("call parens", "name() is not a call", """
fn main in[] out[i32] {
  i32 x = 1;
  x();
  return 0;
}
"""),
    ("div0", "division by zero", """
fn main in[] out[i32] {
  i32 z = 1 / 0;
  return z;
}
"""),
    ("namespace", "not implemented on either host", """
namespace Draw { }
fn main in[] out[i32] { return 0; }
"""),
]


def both(src):
    inter = compiled = None
    ie = ce = None
    try:
        inter = run_text(src)
    except (CompileError, Failure) as e:
        ie = str(e)
    try:
        compile_text(src, "/tmp/conform_one")
        try:
            compiled = run_binary("/tmp/conform_one")
        except Failure as e:
            ce = str(e)
    except CompileError as e:
        ce = str(e)
    return inter, ie, compiled, ce


def main():
    bad = 0
    for name, expect, src in OK:
        inter, ie, compiled, ce = both(src)
        if ie or ce or inter != expect or compiled != expect:
            bad += 1
            print(f"FAIL {name}\n  interp {ie or inter!r}\n  compile {ce or compiled!r}")
        else:
            print(f"ok   {name}")
    for name, part, src in BAD:
        inter, ie, compiled, ce = both(src)
        if ie and ce and part in ie and part in ce and ie == ce:
            print(f"ok   {name}")
        else:
            bad += 1
            print(f"FAIL {name}\n  interp {ie}\n  compile {ce}")
    print("match" if bad == 0 else f"{bad} failed")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
