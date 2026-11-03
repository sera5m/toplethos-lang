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
    ("nest", "14\n", """
fn lum in[i32 p] out[i32] { return p / 256; }
fn twice in[i32 a] out[i32] {
  i32 x = lum in[a];
  return x + lum in[a];
}
fn main in[] out[i32] {
  print in[twice in[7 * 256]];
  return 0;
}
"""),
    ("early", "1\n0\n", """
fn less in[i32 a, i32 b] out[bool] {
  if a != b { return a < b; }
  return false;
}
fn main in[] out[i32] {
  if less in[1, 2] { print in[1]; } else { print in[0]; }
  if less in[3, 3] { print in[1]; } else { print in[0]; }
  return 0;
}
"""),
    ("pow2", "-2\n-2\n2\n0\n-1\n390\n-1073741824\n", """
fn main in[] out[i32] {
  i32 a = 0 - 5;
  i32 b = 0 - 4;
  i32 c = 5;
  i32 d = 0 - 1;
  i32 e = 0 - 65536;
  i32 f = 100000;
  i32 g = 0 - 2147483647 - 1;
  print in[a / 2];
  print in[b / 2];
  print in[c / 2];
  print in[d / 2];
  print in[e / 65536];
  print in[f / 256];
  print in[g / 2];
  return 0;
}
"""),
    ("and skip", "0\n", """
fn die in[] out[i32] { return 1 / 0; }
fn main in[] out[i32] {
  bool t = false;
  if t && (die in[] > 0) { return 1; }
  print in[0];
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
    ("divzero", "0\n", """
fn z in[i32 a] out[i32] @divzero[0] { return a / 0; }
fn main in[] out[i32] {
  print in[z in[5]];
  return 0;
}
"""),
    ("divzero max", "2147483647\n", """
fn z in[i32 a] out[i32] @divzero[max] { return a / 0; }
fn main in[] out[i32] {
  print in[z in[5]];
  return 0;
}
"""),
    ("static", "1\n2\n", """
fn tick in[] out[i32] {
  i32 n @static = 0;
  n = n + 1;
  return n;
}
fn main in[] out[i32] {
  print in[tick in[]];
  print in[tick in[]];
  return 0;
}
"""),
    ("config", "7\n7\n", """
fn tick in[] out[i32] {
  i32 n @config = 7;
  return n;
}
fn main in[] out[i32] {
  print in[tick in[]];
  print in[tick in[]];
  return 0;
}
"""),
    ("xor", "5\n", """
fn main in[] out[i32] {
  i32 a @xor = 5;
  i32 b @xor = a;
  print in[b];
  return 0;
}
"""),
    ("child", "4\n0\n", """
object Point {
  i32 x;
  fn move in[i32 dx] out[] { HERE.x = HERE.x + dx; }
}
object Colored @childof[Point] {
  i32 rgb;
  fn move in[i32 dx] out[] @ismutationof[getfuncidbyname["Point.move"]] {
    HERE.x = HERE.x + dx;
    HERE.rgb = 0;
  }
}
fn main in[] out[i32] {
  Colored c;
  c.x = 1;
  c.rgb = 2;
  c.move in[3];
  print in[c.x];
  print in[c.rgb];
  return 0;
}
"""),
    ("require", "3\n", """
fn f in[i32 a] out[i32] @require[a > 0] { return a; }
fn main in[] out[i32] {
  print in[f in[3]];
  return 0;
}
"""),
    ("threads", "10.000000000\n", """
fn lane in[i32 lane] out[f64] { return as[f64, lane] + 1.0; }
fn main in[] out[i32] {
  fork lane in[4];
  f64 s = start lane;
  finish lane;
  print in[s];
  return 0;
}
"""),
    ("mutex", "40.000000000\n", """
fn add in[i32 lane, f64* p] out[f64] {
  do n[10] {
    lock[p];
    *p = *p + 1.0;
    unlock[p];
  }
  return 0.0;
}
fn main in[] out[i32] {
  f64 shared @mutex = 0.0;
  fork add in[4] with[shared];
  f64 s = start add;
  finish add;
  hold[shared] {
    print in[shared];
  }
  return 0;
}
"""),
    ("ops", "1\n3\n2\n-3\n4\n1\n8\n3\n-3\n-2\n4\n-4\n2\n7\n5\n", """
fn main in[] out[i32] {
  i32 n = 1;
  print in[n++];
  print in[++n];
  n--;
  print in[n];
  print in[-5/_/2];
  print in[-2 ** 2];
  print in[0 ** 0];
  print in[2 ** 3];
  print in[7 % 4];
  print in[-7 % 4];
  print in[~1];
  print in[1 << 2];
  print in[-8 >> 1];
  print in[6 & 3];
  print in[6 | 3];
  print in[6 ^ 3];
  return 0;
}
"""),
    ("opsf", "8.000000000\n-3.000000000\n", """
fn main in[] out[i32] {
  print in[2.0 ** 3.0];
  print in[-5.0/_/2.0];
  return 0;
}
"""),
    ("opsarr", "4\n5\n8\n", """
fn main in[] out[i32] {
  i32 a[2] = {4, 9};
  print in[a[0]++];
  print in[a[0]];
  print in[--a[1]];
  return 0;
}
"""),
    ("inline", "36\n", """
fn sq in[i32 a] out[i32] @inlined { return a * a; }
fn main in[] out[i32] {
  print in[sq in[6]];
  return 0;
}
"""),
    ("opt3", "40\n", """
@globaloptimize[03];
fn main in[] out[i32] {
  i32 x = 5;
  print in[x * 8];
  return 0;
}
"""),
    ("inline2", "6\n", """
fn sum in[i32 n] out[i32] @inlined {
  i32 s = 0;
  i32 i = 0;
  loop {
    if !(i < n) { leave; }
    s = s + i;
    i++;
  }
  return s;
}
fn main in[] out[i32] {
  print in[sum in[4]];
  return 0;
}
"""),
    ("opsi64", "1\n2\n-3\n", """
fn main in[] out[i32] {
  i64 n = 1;
  print in[n++];
  print in[n];
  i64 a = 5;
  a = -a;
  i64 two = 2;
  print in[a/_/two];
  return 0;
}
"""),
    ("widen", "-5\n", """
fn main in[] out[i32] {
  i32 n = -5;
  print in[as[i64, n]];
  return 0;
}
"""),
    ("comments", "4\n-3\n2\n", """
fn main in[] out[i32] {
  //comment with no space, and this whole line is not code
  i32 n = 1 //2
  + 3;
  /* block
     comment */
  print in[n];
  print in[-5/_/2];
  print in[10 /* still */ /_/ 4];
  return 0;
}
"""),
    ("range", "3\ntrue\n", """
fn main in[] out[i32] {
  i32 n = 0;
  i32 c = 64;
  if (c >= 65) && (c <= 90) { n = n + 1; }
  c = 65;
  if (c >= 65) && (c <= 90) { n = n + 1; }
  c = 90;
  if (65 <= c) && (90 >= c) { n = n + 1; }
  c = 91;
  if (c >= 65) && (c <= 90) { n = n + 1; }
  c = -1;
  if (c >= -2) && (c <= 0) { n = n + 1; }
  c = -3;
  if (c >= -2) && (c <= 0) { n = n + 1; }
  print in[n];
  c = 65;
  bool stored = (c >= 65) && (c <= 90);
  print in[stored];
  return 0;
}
"""),
    ("assign", "6\n5\nfalse\ntrue\n", """
fn main in[] out[i32] {
  i32 a = 1;
  i32 b = 2;
  a = b = 5;
  a += 1;
  print in[a];
  print in[b];
  bool same = 1 $= 2;
  bool nots = 1 !$= 2;
  print in[same];
  print in[nots];
  return 0;
}
"""),
    ("util", "1\n3\n10\n4\n2\n1\n", """
fn main in[] out[i32] {
  print in[min in[3, 1]];
  print in[max in[3, 1]];
  print in[clamp in[20, 0, 10]];
  print in[abs in[-4]];
  i32 x = 1;
  i32 y = 2;
  swap in[&x, &y];
  print in[x];
  print in[y];
  return 0;
}
"""),
    ("state", "2\n1\n", """
fn main in[] out[i32] {
  i32 n;
  i32 hit = 0;
  if[state[n] == absent] { hit = hit + 1; }
  n = 4;
  if[state[n]] { hit = hit + 1; }
  print in[hit];
  i32 once @ephemeral = 7;
  i32 got = once;
  i32 empty = 0;
  if[state[once] == empty] { empty = 1; }
  print in[empty];
  return 0;
}
"""),
    ("named", "3\n", """
fn main in[] out[i32] {
  i32 m = 0;
  loop @name[outer] {
    loop @name[inner] {
      m = m + 1;
      if m == 3 { leave[outer]; }
    }
  }
  print in[m];
  return 0;
}
"""),
    ("parallel", "10.000000000\n", """
fn lane in[i32 lane] out[f64] @parallel[4] { return as[f64, lane] + 1.0; }
fn main in[] out[i32] {
  fork lane;
  f64 s = start lane;
  finish lane;
  print in[s];
  return 0;
}
"""),
    ("enum", "2\n2\n1\n0\n-1\n3000000000\n", """
enum Color e16 {
  red,
  yellow = 2,
  green
}
enum Ink e8 {
  black = -1,
  white = 1
}
enum Big e64 {
  huge = 3000000000
}
object Wrap {
  Color c;
}
fn id in[Color c] out[Color] { return c; }
fn main in[] out[i32] {
  Color c = Color::yellow;
  i32 n = 0;
  match @total [c] {
    Color::red { n = 1; }
    Color::yellow { n = 2; }
    Color::green { n = 3; }
  }
  print in[n];
  print in[as[i32, id in[c]]];
  Color z = as[Color, 0];
  if z == Color::red { print in[1]; } else { print in[0]; }
  Color u = as[Color, 9];
  if u == Color::red { print in[1]; } else { print in[0]; }
  print in[as[i32, Ink::black]];
  print in[as[i64, Big::huge]];
  Wrap w;
  w.c = Color::green;
  if w.c == Color::green { return 0; }
  return 1;
}
"""),
]

BAD = [
    ("unclosed", "unclosed comment", """
fn main in[] out[i32] {
  i32 n = 1 /* never ends
  return n;
}
"""),
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
    ("val", "a later store", """
fn main in[] out[i32] {
  i32 y @val = 1;
  y = 2;
  return y;
}
"""),
    ("mut val", "@mut written on @val", """
fn main in[] out[i32] {
  i32 y @mut @val = 1;
  return y;
}
"""),
    ("ephemeral", "a second use of @ephemeral", """
fn main in[] out[i32] {
  i32 p @ephemeral = 4;
  print in[p];
  print in[p];
  return 0;
}
"""),
    ("nofail", "@NoFailTolerance", """
fn main in[] out[i32] @NoFailTolerance { return 0; }
"""),
    ("gpu", "@gpuparallel", """
fn main in[] out[i32] @gpuparallel { return 0; }
"""),
    ("reuse", "without @ismutationof", """
object Point {
  i32 x;
  fn move in[i32 dx] out[] { HERE.x = HERE.x + dx; }
}
object Colored @childof[Point] {
  i32 rgb;
  fn move in[i32 dx] out[] { HERE.rgb = 0; }
}
fn main in[] out[i32] { return 0; }
"""),
    ("require fail", "require", """
fn f in[i32 a] out[i32] @require[a > 0] { return a; }
fn main in[] out[i32] {
  print in[f in[0]];
  return 0;
}
"""),
    ("negpow", "negative power", """
fn main in[] out[i32] {
  i32 n = 2 ** -1;
  return n;
}
"""),
    ("shiftc", "shift count", """
fn main in[] out[i32] {
  i32 n = 1 << 32;
  return n;
}
"""),
    ("impure", "not pure", """
fn bad in[i32 a] out[i32] @inlined {
  print in[a];
  return a;
}
fn main in[] out[i32] { return 0; }
"""),
    ("shift", "shift", """
fn main in[] out[i32] {
  i32 n = 40;
  print in[1 << n];
  return 0;
}
"""),
    ("powbig", "power overflow", """
fn main in[] out[i32] {
  i32 n = 2 ** 31;
  return n;
}
"""),
    ("absent", "absent", """
fn main in[] out[i32] {
  i32 n;
  print in[n];
  return 0;
}
"""),
    ("mutex out", "outside lock or hold", """
fn main in[] out[i32] {
  i32 n @mutex = 1;
  print in[n];
  return 0;
}
"""),
    ("enum same", "two enumerators with the same value", """
enum Color { red = 1, blue = 1 }
fn main in[] out[i32] { return 0; }
"""),
    ("enum int", "cannot store", """
enum Color { red }
fn main in[] out[i32] {
  i32 n = Color::red;
  return n;
}
"""),
    ("enum total", "match is not total", """
enum Color { red, blue }
fn main in[] out[i32] {
  Color c = Color::red;
  match @total [c] { Color::red { } }
  return 0;
}
"""),
    ("enum fit", "enumerator value does not fit", """
enum Ink e8 { hot = 200 }
fn main in[] out[i32] { return 0; }
"""),
    ("enum row", "no row", """
enum Color { red }
fn main in[] out[i32] {
  Color c = Color::red;
  i32 n = c + 1;
  return n;
}
"""),
    ("enum wide", "as with no row", """
enum Big e64 { huge = 3000000000 }
fn main in[] out[i32] {
  i32 n = as[i32, Big::huge];
  return n;
}
"""),
    ("enum runtime", "enum value does not fit", """
enum Ink e8 { black }
fn main in[] out[i32] {
  i32 n = 200;
  Ink a = as[Ink, n];
  print in[as[i32, a]];
  return 0;
}
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
    open("/tmp/tp_lib.tpth_s", "w").write("fn twice in[i32 a] out[i32] { return a + a; }\n")
    inter, ie, compiled, ce = both('include "/tmp/tp_lib.tpth_s";\nfn main in[] out[i32] {\n  print in[twice in[3]];\n  return 0;\n}\n')
    if ie or ce or inter != "6\n" or compiled != "6\n":
        bad += 1
        print(f"FAIL include\n  interp {ie or inter!r}\n  compile {ce or compiled!r}")
    else:
        print("ok   include")
    from front import parse as parse_src
    from interp import execute
    prog = parse_src("fn inc in[i32 a] out[i32] { return a + 1; }\nfn main in[] out[i32] {\n  i32 n = inc in[1];\n  n = inc in[n];\n  print in[n];\n  return 0;\n}\n")
    out = execute(prog)
    row = prog["symbols"]["inc"]
    if out != "3\n" or row["calls"] != 2 or row["edges"].get("main") != 2 or row["cost"] != "unknown":
        bad += 1
        print(f"FAIL symbol {out!r} {row}")
    else:
        print("ok   symbol")
    print("match" if bad == 0 else f"{bad} failed")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
