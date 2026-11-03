# Fixes needed

Not built. v1.0 stays the two hosts that exist. Scores are complexity, then impact, from 1 to 5. Complexity is new machinery. Impact is programs blocked, or the two hosts disagreeing.

A mark that is only in USE.md is still rejected by both hosts until a row here is built. Do not start a row from this file without saying which one.

## Done in the hosts

| gap | what both hosts do now |
|---|---|
| Uninitialized scalar | A scalar with no store stays absent. A read is a failure on both hosts. |
| Use tracking | The reference table records where. The symbol row records inputs, outputs, variables, functions, and invokes. The interpreter adds `calls` and `edges` while the program runs. |
| `+=` `a = b = c` `$=` `^=` | `+=` is `+` then store. `a = b = c` stores right to left. `$=` is false until a `flow` names the pair. `!$=` is the opposite. `^=` stores through a convert row that already exists. |
| `if[state[name]]` | `if[state[name]]` is present. `==` and `!=` also take `absent`, `present`, and `empty`. The look does not read the value, so an `@ephemeral` cell is not consumed. After its one read the state is `empty`. |
| `hold` and mutex paths | `hold[cell] { }` locks and unlocks on every exit. A read or a store of an `@mutex` cell outside `lock` or `hold` is a compile error. A path that locks and does not unlock is a compile error. The creating initializer is not that read. |
| `@parallel[n]` | The function is a CPU worker of n threads. `fork name;` uses that n. A different `fork name in[k]` is a compile error. |
| `include "file.tpth_s"` | The file is loaded once. A `@private` function in it is not visible here. A header is rejected. `include lib` is still rejected. |
| `min` `max` `clamp` `abs` `swap` | The utilities. `abs` of the lowest signed value is a failure. |
| `leave[name]` `@name` | `@name` on `loop` and `do`. `leave[name]` ends that one. |

## Hosts disagree

None of the old scalar-initialization row. The cell is still 16 bytes, so a C struct of the same fields would not match. That is the object-layout row below.

## Asked for 1.1

| gap | note | c | i |
|---|---|---:|---:|
| Pointers, wider than today | Today a pointer is `&cell`, `T*`, and `*p`. What "expanded" adds is not named yet. Do not invent the list. `window` and `gather` are already rejected below. | 2 | 3 |
| Static analysis | The table counts `drop`, `reuse`, `inc`, `range`, and `forward`. Only the constant range `if` is lowered. `inc`, `reuse`, and `forward` are not. It is not a cycle score. Cost on the symbol row is `unknown`. | 3 | 4 |
| Active analysis | Call counts and caller edges are recorded on the symbol row while the interpreter runs. The rest of the shape in [GOALS.md](GOALS.md) — the other page, the dirty flag, the snapshot — is not a v1.0 host. | 4 | 4 |

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
| Namespaces and `::` | 3 | 3 |
| `enum`, `match`, `@total` | 3 | 3 |
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
