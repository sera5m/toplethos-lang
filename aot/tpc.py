#!/usr/bin/env python3
"""Compile a 1.0 program: parse, passes, amd64 text, then g++.

g++ assembles the .s and links lang/desktop.c. It does not optimize the .s.
A file with no fn main is not a program.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "lang"))

from emit import assemble
from front import CompileError, parse


def compile_source(src):
    return assemble(parse(src))


def main(argv):
    ap = argparse.ArgumentParser(description="Compile a Toplethos 1.0 program and link it with g++.")
    ap.add_argument("source")
    ap.add_argument("-o", default="a.out", help="binary to write (assembly is written beside it as .s)")
    ap.add_argument("--asm-only", action="store_true", help="write the .s file and do not call g++")
    ap.add_argument("--dump", action="store_true", help="print the pipeline tag")
    ap.add_argument("--analyze", action="store_true", help="print the reference table and stop")
    ap.add_argument("--run", action="store_true", help="run the binary after g++ links it")
    args = ap.parse_args(argv)
    try:
        src = open(args.source, encoding="utf-8").read()
    except OSError as e:
        print(f"tpc: {e}", file=sys.stderr)
        return 1
    try:
        from front import parse
        from analyze import report
        if args.analyze:
            prog = parse(src)
            sys.stdout.write(report(prog))
            return 0
        text = compile_source(src)
    except CompileError as e:
        print(f"tpc: {args.source}: {e}", file=sys.stderr)
        return 1
    if args.dump:
        print("USE.md core", file=sys.stderr)
    asm_path = args.o + ".s"
    try:
        with open(asm_path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError as e:
        print(f"tpc: {e}", file=sys.stderr)
        return 1
    if args.asm_only:
        print(asm_path)
        return 0
    cmd = ["g++", "-pthread", "-lm", "-o", args.o, asm_path, str(ROOT / "lang" / "desktop.c")]
    print(" ".join(cmd), file=sys.stderr)
    g = subprocess.run(cmd, text=True, capture_output=True)
    if g.returncode != 0:
        sys.stderr.write(g.stderr)
        return g.returncode or 1
    if not args.run:
        return 0
    r = subprocess.run([args.o])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
