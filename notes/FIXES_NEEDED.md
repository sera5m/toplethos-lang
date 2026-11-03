# Fixes needed

Not built. v1.0 stays the two hosts that exist. Scores are complexity, then impact, from 1 to 5. Complexity is new machinery. Impact is programs blocked, or the two hosts disagreeing.

A mark that is only in USE.md is still rejected by both hosts until a row here is built. Do not start a row from this file without saying which one.

## Hosts disagree

| gap | spec | what the compiler does | c | i |
|---|---|---|---:|---:|
| Uninitialized scalar | No store means absent. A read is a failure. | [lang/passes.py](../lang/passes.py) marks every scalar declaration present, even with no initializer, then skips the check. The interpreter still fails. | 1 | 5 |

## Asked for 1.1

| gap | note | c | i |
|---|---|---:|---:|
| Use tracking | Count uses of functions, variables, and every other name, and record where. How often, and in which body. This is the cheap half of the symbol table in [GOALS.md](GOALS.md). | 2 | 3 |
| Bitwise | `&` `\|` `^` `~` `<<` `>>`. Both hosts implement the row. Unary `&` is still the address. `&&` and `\|\|` stay `bool`. Same width, two's complement bits. A shift count outside `0 .. width-1` is a failure. | 2 | 3 |
| Modulo | `%`. Both hosts implement the row. `a % b` is `a - (a / b) * b`. Zero and the lowest signed value with `-1` are the same failures as `/`. No float row. | 1 | 3 |
| Pointers, wider than today | Today a pointer is `&cell`, `T*`, and `*p`. What "expanded" adds is not named yet. Do not invent the list. `window` and `gather` are already rejected below. | 2 | 3 |
| Static analysis | [lang/analyze.py](../lang/analyze.py) prints the reference table: reads, writes, calls, loops, a shape, and `O(n)` or unknown. `tpc --analyze` shows it. It is not the scored symbol table. | 3 | 4 |
| Active analysis | Facts gathered while a program runs. Not a v1.0 host. The shape is in [GOALS.md](GOALS.md). | 4 | 4 |

## Half built

| gap | spec | hosts | c | i |
|---|---|---|---:|---:|
| Mutex paths | A read or a store outside `lock` or `hold` is a compile error. Every path that locks unlocks. | `lock` and `unlock` run. `hold` is not accepted. No path check. | 3 | 3 |
| `@parallel[n]` | The mark is n CPU threads. | Threads exist as `fork` and `finish`. The mark is rejected. | 2 | 2 |
| Scope across files | `@public` is any file that included this one. | Marks work in one file. `include` is rejected. | 3 | 3 |
| Object layout | Fields use the target's C++ alignment. Pages also say to count the present flag. | Every cell is 16 bytes. A C struct would not match. | 4 | 3 |
| `+=` `a = b = c` `$=` `^=` | In the precedence table. | Not parsed, or rejected as a bad statement. | 2 | 2 |
| `if[state[name]]` | Look at absent or empty without reading the value. | `if[` fails with "if takes a bool condition." Wrong message. | 2 | 2 |

## Rejected by both

| feature | c | i |
|---|---:|---:|
| `include` of another `.tpth_s`, and `include lib` / `foreign` | 4 | 5 |
| Page placement, `@paged`, `@blockinit`, `@blockpack`, `@blockpush`, `@blockpop`, `blockid` | 5 | 4 |
| `@memory_hard` | 3 | 3 |
| `@gpuparallel`, `@npuparallel` | 5 | 2 |
| Namespaces and `::` | 3 | 3 |
| `enum`, `match`, `@total` | 3 | 3 |
| `string`, `char`, `s[i]` | 3 | 3 |
| `f32`, `i8`, `i16`, the `ui` widths, `size` as a type | 3 | 3 |
| `onfail`, `defer`, `faildefer`, `@retry`, `STOP[THIS]`, `fatal`, `onwarn`, `@structuredConcurrency` | 3 | 3 |
| `min`, `max`, `clamp`, `abs`, `swap` | 1 | 2 |
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
| `leave[name]`, `@name` on a loop | 1 | 1 |

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
