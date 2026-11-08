# Fixes needed

The compiler is `compiler/tpcc`. The Python compiler is gone. It does not come back.

1.2 "tripar" is the name for the day `python3 conformance/check.py` prints `1.2 tripar`. That is USE.md plus the 1.1 rows, one bytecode, interpreted and compiled and JIT-compiled by the same program. This tree is not that day.

Scores below are complexity, then impact, from 1 to 5. Complexity is new machinery. Impact is programs blocked. A mark that is only in USE.md is still rejected until a row here is built.

## Done in the hosts

| gap | what both hosts do now |
|---|---|
| Uninitialized scalar | A scalar with no store stays absent. A read is a failure on both hosts. |
| Use tracking | The reference table records where. The symbol row records inputs, outputs, variables, functions, and invokes. The interpreter adds `calls` and `edges` while the program runs. |
| `+=` `a = b = c` `$=` `^=` | `+=` is `+` then store. `a = b = c` stores right to left. `$=` is false until a `flow` names the pair. `!$=` is the opposite. `^=` stores through a convert row that already exists. |
| `if[state[name]]` | `if[state[name]]` is present. `==` and `!=` also take `absent`, `present`, and `empty`. The look does not read the value, so an `@ephemeral` cell is not consumed. After its one read the state is `empty`. |
| `hold` and mutex paths | `hold[cell] { }` locks and unlocks on every exit. A read or a store of an `@mutex` cell outside `lock` or `hold` is a compile error. A path that locks and does not unlock is a compile error. The creating initializer is not that read. |
| `@parallel[n]` | The function is a CPU worker of n threads. It returns `f64` and takes the lane as `i32`, plus an optional pointer only through `with` on an `@mutex` cell. `fork` readies the lanes. `start` waits for every lane and returns the sum. `finish` joins them. `run` is those three. A second `fork` before `finish` is a failure. A lane failure is raised after the others finish. USE.md section Runs is this contract. |
| `include "file.tpth_s"` | The file is loaded once. A `@private` function in it is not visible here. A header is rejected. `include lib` is still rejected. |
| `min` `max` `clamp` `abs` `swap` | The utilities. `abs` of the lowest signed value is a failure. |
| `leave[name]` `@name` | `@name` on `loop` and `do`. `leave[name]` ends that one. |
| `enum` `match` `@total` | A named signed integer. The width is `e8`, `e16`, `e32`, or `e64`, and the default is `e32`. The compiler word is two `i16`s: `1`, then the enum id. `match` compares one arm. `@total` requires every name. |

## Hosts disagree

None of the old scalar-initialization row. The cell is still 16 bytes, so a C struct of the same fields would not match. That is the object-layout row below.

## Asked for 1.1

These four rows are the 1.1 list. The run model is not one of them. `fork`, `start`, `finish`, and `run` are the hosts, and USE.md now says what they do. `@structuredConcurrency`, `@topology`, `pipe`, `push`, `pop`, and `@accum` stay in Rejected by both. They are not a second ownership story, and they are not how a run is parented.

| gap | note | c | i |
|---|---|---:|---:|
| Pointers | The list is `cell`, `ffi`, `inlined`, `jit-fn`, `jit-var`, `neuron`, `manager`. `cell` is `&cell`, `T*`, and `*p`. `ffi` is that address handed to a foreign call: valid for the call, and not kept unless the cell outlives the call. `inlined` is the compiled symbol of an `@inlined` function. `jit-fn` is the dispatch slot of a compiled function; the program names the function and does not load the address. `jit-var` is a variable the JIT placed. `neuron` is a watched function. `manager` is that neuron's local record: where it is, the dirty flag, the other page. The program loads and stores only `cell`. SPIR-V keeps none of them. `window` and `gather` stay rejected. Not capability pointers. | 2 | 3 |
| Static analysis | The table counts `drop`, `reuse`, `inc`, `range`, and `forward`. Only the constant range `if` is lowered. `inc`, `reuse`, and `forward` are not. It is not a cycle score. Cost on the symbol row is `unknown`. | 3 | 4 |
| Active analysis | Call counts and caller edges are recorded on the symbol row while the interpreter runs. The rest of the shape in [GOALS.md](GOALS.md) — the other page, the dirty flag, the snapshot — is not a v1.0 host. | 4 | 4 |
| Host facts | Two calls, same on both hosts. `threads` returns an `i32`: the OS count of logical CPUs it will schedule. `freemem` returns an `i64`: available RAM in bytes, not disk and not VRAM. If the host cannot ask, the call is a failure. It does not invent a 1 or a 0. `fork in[k]` stays a constant. This row does not let `k` be the value `threads` just returned, and it does not allocate an array from `freemem`. | 2 | 3 |

## Half built

| gap | spec | hosts | c | i |
|---|---|---|---:|---:|
| Scope across files | `@public` is any file that included this one. | `include "file.tpth_s"` loads it. `@private` stays in that file. `@protected` across a subclass in another file is not checked. `include lib` is rejected. | 3 | 3 |
| Object layout | Fields use the target's C++ alignment. Pages also say to count the present flag. | Every cell is 16 bytes. A C struct would not match. Packing the cell would drop the present flag the spec counts. | 4 | 3 |

## Rejected by both

| feature | c | i |
|---|---:|---:|
| `include lib` / `foreign` | 4 | 5 |
| Page placement, `@paged`, `@blockinit`, `@blockpack`, `@blockpush`, `@blockpop`, `blockid` | 5 | 4 |
| `@memory_hard` | 3 | 3 |
| `@gpuparallel`, `@npuparallel` | 5 | 2 |
| Namespaces | 3 | 3 |
| `string`, `char`, `s[i]` | 3 | 3 |
| `f32`, `i8`, `i16`, the `ui` widths, `size` as a type | 3 | 3 |
| `onfail`, `defer`, `faildefer`, `@retry`, `STOP[THIS]`, `fatal`, `onwarn`, `@structuredConcurrency` | 3 | 3 |
| `rng`, `rand`, `randf` | 2 | 2 |
| A user `row[addmagnitude]`, and convert rows past the three live widenings | 3 | 2 |
| `fnid`, `instance`, `compose`, `bind`, `transform`, `out[here]` | 4 | 2 |
| `flow`, `formatconvert` | 3 | 2 |
| `pool`, `pipe`, `push`, `pop`, `flush` | 4 | 2 |
| `@accum` | 2 | 2 |
| `@comptime`, `@compeval` | 3 | 2 |
| Multi-dimensional arrays | 2 | 2 |
| `context`, `links` | 3 | 1 |
| `@stream`, `@dataparallel`, `@checkpoint`, `@topology`, `select` | 4 | 2 |
| `window`, `gather` | 2 | 1 |
| `macro`, `when` | 2 | 1 |
| `create`, coroutines, the 15-minute stack move | 4 | 1 |
| `i4`, `f8`, `bf16`, unsigned floats, balanced ternary | 5 | 1 |

## Walls

These are not missing syntax. They are reasons a goal breaks if it is built the naive way. The decision is in [GOALS.md](GOALS.md).

| goal | wall | decision |
|---|---|---|
| Flatten a hot function onto the GPU | SIMD divergence, and driver latency | Only a data-parallel loop goes. Scalar control stays on the CPU. Divergence is tracked, not ignored. |
| Roll back a bad mutation | External I/O cannot be reverted | Undo memory only. The system must know what it is allowed to undo. Pause and flush first when downtime is acceptable. |
| Replace a running function | I-cache against D-cache | Do not patch bytes under the instruction pointer. A dirty flag, then a call to the compiled function. Flip one pointer after the other page is verified. |
| Salvage a good piece from another branch | Hidden coupling | The risk stays. Layout assumptions have to be in the symbol row, or the merge is not checked. |
| Score exact cycles | Undecidable for a dynamic loop | Lower bound, average, upper bound, and unknown. Not an exact cycle count. |

## Not a fix

Bitwise, `%`, `/_/`, `**`, and `++`/`--` are in USE.md and in both hosts. No overloads, no traits, no vptr, no downcast, no borrow checker, no `goto`. The interpreter does not run the passes. That is the spec. Packing the 16-byte cell would be faster and would drop the present flag the spec counts. A pass that names a whole region is not a row in Known rewrites. It is a goal, not a 1.0 bug. `@globaloptimize[03]` tags a function and may turn a multiply by a power of two into a shift. That does not change the value.

## C++ compiler, not the hosts

2026-11-07. `compiler/tpcc` is a separate pipeline. The two hosts above did not gain these rows. What that pipeline already runs is in [JIT_NOTES.md](JIT_NOTES.md): bytecode after bind, eval before emit, four listings, the interpreter, cell pointers, host facts, static and active counts, and an `@inlined` JIT that prints amd64 from the bytecode.

Still needed: the SPIR-V/Vulkan backend emit. The file that exists is a listing, not a module Vulkan can load.
