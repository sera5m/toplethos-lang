# Implemented in the enterpreter

UMMES, release 1.2 tripar. Pronounced "yu-mees": the unified multimodal execution system. This file is what `tpcc --run` actually does. It is the oracle. The amd64 listing is [implemented_in_compiler.md](implemented_in_compiler.md). A row that is present and wrong is [FIXES_NEEDED.md](FIXES_NEEDED.md), not here.

`yes` means the interpreter runs it. `no` means it does not. `bind` means both hosts reject it with the same message before either runs.

The front end is the same one the compiler uses: tokenize, bind against the type map, lower to bytecode, then const-fold and drop unread writes. The interpreter then steps that bytecode. A cell is one `uint64`. A pointer kind `cell` is an index into that vector. Index 0 is empty.

## Types and arithmetic

| feature | enterpreter |
|---|---|
| `i4` `i8` `i16` `i32` `i64` and `ui4` `ui8` `ui16` `ui32` `ui64` add, sub, mul, and, or, xor, shift, compare | yes. The result is masked to the width. |
| `bool`, `if`, `&&` `\|\|`, `!` | yes |
| `f64` add, sub, mul, div, neg, `**`, `/_/` | yes. Another float width is `no row for that atom yet`. |
| `f32`, `f16`, `f8`, `bf16`, unsigned float, ternary | no |
| `char` `string`, `s[i]`, `size in[array]` | no |
| `as` between integer widths that fit, integer to `f64`, enum to integer | yes. A narrowing `as` with no row is rejected. |
| `**` on integers, `/_/` | yes |
| integer `/` `0`, shift count outside the width, unary minus of the lowest signed value | yes, a failure. `@divzero[0]` and `@divzero[max]` stand in. |
| `f64 / 0` | a failure. The amd64 listing does not match. |

## The 1.0 suite

`python3 conformance/check.py` is this host. A green run prints `1.2 tripar`. These rows run:

| feature | enterpreter |
|---|---|
| `print` | yes. If anything printed, there is no `result` line. |
| calls, early `return`, `&&` skipping a call | yes |
| objects, fields, methods, `@childof`, `@ismutationof` | yes. `HERE` is the cell address of the instance. |
| arrays, a short brace zero-fills the tail | yes |
| `do`, `onFinished`, `loop`, `leave`, `leave[name]` | yes |
| `do`'s `i` | yes. Read-only, 0 .. k-1, and the name exists only inside that `do`, including a nested `do` and `onFinished`. |
| `enum`, `match @total` | yes. Lowered to `if`. |
| `@static` `@config` scalar | yes, for the process. A second call skips the initializer. An array does not. See FIXES_NEEDED. |
| `@val` `@config` later store, `@mut` on `@val` | bind |
| `@xor` | a copy |
| `@ephemeral`, `state[name]`, `absent` `present` `empty` | yes. The state look does not consume an ephemeral. |
| an uninitialized tracked scalar | a read is `absent` |
| `@require` | a trap at run time |
| `min` `max` `clamp` `abs` `swap` | yes. `abs` of the lowest signed value is a failure. |
| `+=`, `a = b = c` | yes |
| `$=` `!$=` | the tokens exist. They lower to constant false and constant true. No `flow` can be declared, so nothing else can happen. |
| `^=` | no. The lexer emits `^` and then `=`. It is not the resolve-store in USE. |
| `include "file.tpth_s"` | yes, once. `@private` in that file is not visible here. A header is rejected. `include lib` is rejected. |
| `fork` `start` `finish` `run` `@parallel[n]` | yes, in order, on this thread. `start` sums `f64`. Not an OS thread. |
| `@mutex`, `lock`, `unlock`, `hold` | yes. The lock is a count. A use outside it fails at run time, not at bind. USE says compile error. |
| `threads` `freemem` | yes. The OS count, and available RAM in bytes. If the host cannot ask, it is a failure. It does not invent a 0. |
| `@time["name"] watch <f64> { }` | yes. `CLOCK_MONOTONIC`. Prints `name seconds value`. |
| `saveArrayAsbmp` | yes. Same pixel rule as the amd64 listing. The signal is `i32` 1 or 0, not a thrown failure. The pointer is a cell index. A null index, an empty path, a width or height outside 1..1048576, a bit width outside 1..64, or a count that runs past the allocated cells returns 0. The declared length is not stored on the pointer. |
| `--active` | yes. Call counts and caller names. `cost=unknown`. |
| `--jit` on `@inlined` | yes. Compiles that function's bytecode with the amd64 emitter while this interpreter is still running, checks the native result, then calls the native symbol. A pointer, a call, a print, a watch, or a save is not pure and is rejected. |

## Not implemented

These are rejected, or they are not keywords. They are not bugs in a row above.

| feature | where it stops |
|---|---|
| `continue` | lex. Not a keyword. The next iteration of a `do` is not reachable that way. |
| a name that ends at the brace | bind. Locals are the whole function. |
| `loadbmp` `loadbytes` `savebmp` | bind. `'loadbmp' is not declared`. The write that exists is `saveArrayAsbmp`. |
| `rng` `rand` `randf` | bind |
| `namespace` | bind, `not implemented on either host` |
| `@NoFailTolerance` `@gpuparallel` | bind, rejected on purpose |
| an unknown `@` | bind, `is not in this stage` |
| `onfail` `defer` `faildefer` `@retry` `STOP[THIS]` `fatal` `onwarn` | not parsed |
| `flow` `formatconvert` | not declared. `$=` cannot see a flow. |
| `string` `char` indexing | no |
| multi-dimensional arrays | the second `[` is an index, not a shape |
| `pool` `pipe` `push` `pop` `flush` `@accum` | no |
| `fnid` `instance` `compose` `bind` `transform` `out[here]` | no |
| `@comptime` `@compeval` `@stream` `@dataparallel` `@checkpoint` `@topology` `select` | no |
| `window` `gather` `macro` `when` | no |
| page placement, `@paged`, `@blockinit`, `@blockpack`, `@blockpush`, `@blockpop`, `blockid` | no |
| `@memory_hard` `@npuparallel` | no |
| coroutines, the 15-minute stack move | no |
| SPIR-V / Vulkan as a device the interpreter can launch | no. The listing is a separate backend, and it is not a loadable module. |
| OS threads for `fork` | no. Lanes run in order. That is the checksum the batteries match. |

## Limits of this oracle

The step counter stops at 10000000. Frames are not freed. Both are in FIXES_NEEDED. A program the amd64 listing finishes can still be stopped here.

`@static` on an array keeps element 0 only. That is also in FIXES_NEEDED.
