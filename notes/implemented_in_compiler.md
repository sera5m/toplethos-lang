# Implemented in the compiler

UMMES, release 1.2 tripar. Pronounced "yu-mees": the unified multimodal execution system. This file is what `tpcc --asm` and `tpcc --target amd64` actually emit. The front end is shared with the interpreter. The interpreter is the oracle: [implemented_in_enterpreter.md](implemented_in_enterpreter.md). A row that is present and wrong is [FIXES_NEEDED.md](FIXES_NEEDED.md), not here.

`yes` means the amd64 listing runs it. `no` means this backend does not. Bind rejects some rows before either host runs; those say `bind`.

## Pipeline

Tokenize, bind against the type map, lower to typed three-address bytecode, const-fold and drop unread writes, then emit. The `.s` is not a second source of truth. Constants are already folded. `gcc` only assembles that text.

## Types and arithmetic

| feature | amd64 |
|---|---|
| `i32` `i64` arithmetic, compare, shift, bitwise, `~`, unary minus | yes |
| `bool`, `if`, `&&` `\|\|` | yes |
| `f64` add, sub, mul, div, neg, compare, `as` from `i32` or `i64`, print, return | yes |
| `f32` and the smaller floats, `bf16`, unsigned float, ternary | no |
| `i4` `i8` `i16` `ui4` `ui8` `ui16` add or mul | no. An immediate, a move, and a return of those widths are slot traffic only, so `saveArrayAsbmp` can read the cells. |
| `ui32` `ui64` | no. A folded `ui32` immediate is still `no amd64 row for ui32`. |
| `char` `string` `size` as a value | no |
| integer `/` `0`, and the lowest signed value divided by -1 or negated | yes, the process exits |
| `f64 / 0` | emits `divsd`. See FIXES_NEEDED. |
| `**` and `/_/` | no |

## Names, control, calls

| feature | amd64 |
|---|---|
| functions, `return`, calls, SysV: 6 integer registers and 8 `f64` registers | yes |
| arrays, `cell` pointers, `&`, `*`, load and store | yes |
| a local name ends at the brace | yes. The same name in that same brace is still `already declared`. |
| `do n[k]`, index `i` read-only and local to that `do`, including a nested `do` and `onFinished` | yes |
| `loop`, `leave`, `leave[name]`, `@name` | yes, when the body is otherwise legal here |
| `continue` | yes. The innermost `loop` jumps to its head. Inside a `do`, it jumps to the increment, so `i` still advances. |
| `if` `else` | yes |
| objects, fields, methods, `HERE` as a cell address | yes, when every atom in the body has a row here |
| `enum`, `match`, `@total` | yes. `match` is lowered to `if`. |
| `include "file.tpth_s"`, `@private` stays in that file | yes. `include lib` is rejected at bind. |
| `namespace` | bind |
| `@NoFailTolerance`, `@gpuparallel` | bind |
| `onfail` `defer` `retry` `STOP` `fatal` | no |
| `select` `pause` `resume` `await` `next` | no |

## Host operations this listing prints

| feature | amd64 |
|---|---|
| `print` | yes. `printf`. |
| `threads`, `freemem` | yes. `sysconf`. A bad answer exits. |
| `@time["name"] watch <f64> { }` | yes. `clock_gettime` of `CLOCK_MONOTONIC`, then `name seconds value`. |
| `saveArrayAsbmp` | yes. 24-bit uncompressed bottom-up BMP. Signal in `%eax` is 1 or 0. Width and height must be 1..1048576. 24 bits or more is `r * 65536 + g * 256 + b`. 8..23 bits is grayscale of the low 8. Fewer than 8 bits is `v * 255 / ((1 << bits) - 1)`. Element `i` is at `ptr - i*8`, because the frame grows down. No check that the count fits. |
| `savebmp` | yes. Same writer, `i32` pixels only. No result. A 0 from the writer exits. |
| `loadbmp` | yes. 24-bit uncompressed BMP into an `i32` array. `width * height` must equal the array length. A pixel is `r * 65536 + g * 256 + b`. Negative height is top-down. A bad file exits. |
| `loadbytes` | yes. One file byte per `i32` cell. The result is the count. A missing file exits. A file longer than the array exits. Cells past the file are left as they were. |
| `loadfile` | yes. Same read as `loadbytes`. A longer file is cut to the array length, and that length is the result. |
| `rng` `rand` `randf` | no |
| `min` `max` `clamp` `abs` `swap` | yes, lowered to compare and move |

## Runs, locks, static

| feature | amd64 |
|---|---|
| `fork`, `start`, `run`, `@parallel[n]` | yes. `run` is fork then start. Lanes run one after another on this thread. The `f64` results are summed. Not an OS thread. USE Runs still says threads. |
| `finish` | empty. The lanes already finished inside `start`. |
| a second `fork` before `finish` | not a failure. It overwrites the pending fork. |
| `lock` `unlock` `hold` `@mutex` | `Lock` and `Unlock` are empty. |
| `@static` `@config` | The cells are in `.bss`, not the frame. Element 0 is the high address, so `ptr - i*8` still names element `i`. A guard byte skips the initializer after the first entry. An uninitialized array is not a million zero moves. The bytes are already 0. |

## Other targets, and the JIT

| target | what it prints |
|---|---|
| arm64 | `i32` `i64` `bool` rows from the older battery. No `f64`, no fork, no `@time`, no `saveArrayAsbmp`. |
| C++ | `i32` `i64` `bool` `ui32` `ui64`. No `f64`, no fork, no `@time`, no `saveArrayAsbmp`. |
| SPIR-V | an operation listing. Not a module a Vulkan loader can run. No entry point, no bindings, no merge instructions. |
| `--jit` | the interpreter asks this same amd64 emitter for an `@inlined` function, links it, checks it against the oracle, then flips the dispatch slot. Not a second IR. |

`@inlined` is rejected at lower time when the body calls, prints, watches, saves or loads a file, takes a pointer, or is not a pure `i32` / `i64` / `bool` scalar.

## What still stops the task batteries

`sh aot/mix.sh`, the Horner rows, dict, table, glitch, blur, words, and rdp compile. The checksums match the C++ row: blur `3330989145`, rdp `97` / `54.055720805`, words `1169608076370`, glitch `14192147618921`. colorsort assembles. Running it writes `examples/colorsort.bmp`.

| program | why it is not a language row |
|---|---|
| gputwo, gpubench | a process spawn, not a language form |
| walk | no directory calls. C++ and Rust only. |
