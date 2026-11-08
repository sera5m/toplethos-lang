# 1.2 tripar

Not this tree. The name is reserved for one compiler, `compiler/tpcc`, on the day `python3 conformance/check.py` prints `1.2 tripar`.

That program already has the three modes. `--run` interprets the bytecode. `--asm` and `--target` compile that same bytecode. `--jit` compiles an `@inlined` function from it while the interpreter is still running, checks the native result against that oracle, and flips the dispatch slot. There is no second compiler.

The Python compiler is gone. `lang/` no longer has a parser, an interpreter, or an emitter. `aot/tpc.py` is gone. Those programs do not come back.

The suite is the 1.0 texts in `conformance/check.py`, including the rows that must be rejected. This run is `not 1.2 tripar: 63 gaps`. Four rows already match: an unclosed comment, division by zero, a shift count past the width, and a scalar call that returns 3.

What the other rows stop on:

| programs | first reason |
|---:|---|
| 19 | `print` is not declared |
| 9 | `expected =` |
| 6 | `expected fn` |
| 5 | `:` is not a token |
| 3 | `.` is not a token |
| 3 | `do` is not lowered |
| 2 | arrays are not lowered |
| 2 | `@require` is not in this stage |
| 2 | `@divzero` is not in this stage |
| 1 | `@parallel` is not in this stage |

Objects, enums, `include`, and `@mutex` are inside those rows. They are not a separate compiler. They are the rest of this one.

The 1.1 rows are not a pass by themselves. Pointer `cell`, the static counts, active call edges, `threads`, and `freemem` are in `tpcc`. `ffi`, `jit-fn`, `neuron`, and `manager` are records, not addresses a program can load. `inc`, `reuse`, and `forward` are counted, not lowered. None of that turns this tree into 1.2.
