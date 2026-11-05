"""Shared entry for the interpreter and the compiler."""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from emit import assemble
from front import CompileError, Failure, parse
from interp import execute


def run_text(src):
    prog = parse(src)
    return execute(prog)


def compile_text(src, binary):
    prog = parse(src)
    asm = assemble(prog)
    path = Path(binary)
    asm_path = path.with_suffix(".s")
    asm_path.write_text(asm)
    rt = Path(__file__).resolve().parent / "desktop.c"
    cmd = ["g++", "-pthread", "-lm", "-o", str(path), str(asm_path), str(rt)]
    g = subprocess.run(cmd, text=True, capture_output=True)
    if g.returncode != 0:
        sys.stderr.write(g.stderr)
        raise SystemExit(g.returncode or 1)
    return asm_path


def run_binary(binary):
    r = subprocess.run([binary], text=True, capture_output=True)
    if r.stderr.startswith("failure:"):
        raise Failure(r.stderr.strip().removeprefix("failure: ").strip())
    return r.stdout


def main_interp(src):
    try:
        sys.stdout.write(run_text(src))
        return 0
    except CompileError as e:
        sys.stderr.write(str(e) + "\n")
        return 1
    except Failure as e:
        sys.stderr.write(str(e) + "\n")
        return 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write("usage: python3 lang/host.py file.tpth_s\n")
        sys.exit(2)
    try:
        text = open(sys.argv[1], encoding="utf-8").read()
    except OSError as e:
        sys.stderr.write(f"{e}\n")
        sys.exit(1)
    sys.exit(main_interp(text))
