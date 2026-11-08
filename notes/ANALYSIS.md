# Reference table

Short version of the long note. This is the static table, built before emit. It is not a rewrite, and it is not ikitaku.

The arithmetic, the `@globaloptimize` levels, and `@inlined` are already in the hosts. `@globaloptimize[02]` still selects `01`. Level `03` tags a function with a verb and may turn `* 2^k` into a shift. Ikitaku, if it is asked at all, is asked after a rewrite: proof, counterexample, or unknown. It does not fill this table.

## What the table is

One record per function, one per name in that function, one per loop, one annotation pointer per call site, and one `op` per read, write, or call. The compiler already runs this from the parse, before the passes. `tpc --analyze file.tpth_s` prints it and does not compile.

Counts are source sites, not runs. `i = i + 1` once in the text is one write, however many times the loop runs. A dynamic total is a later cost field. It is not this table.

A name record holds the type, the decl line, every read line, every write line, and the function it sits in. A function record holds the shape, the calls, and the callers. A loop record says whether it has a cursor.

A cursor is a name whose every store in that loop is `n = n + 1` or `++n`. A `do` counts, and its cursor is `i`. A bound is an integer name compared with that cursor (`i < n`) and not written in the loop. The count of a `do` is a bound when it is a name or a constant. `slot = slot + 1` followed by `slot = 0` is not a cursor. A forward loop with no bound is still not a class: pass 4 leaves it `unknown`.

Pass 3 (`depend`) writes `flow`, `ctrl`, and `swap`. It does not know trip counts. Pass 4 (`profile`) is the O() scan. It runs once, callees first, and it walks the calls in the pass-3 order. It does not search for a bound the text does not name, and it does not decide the halting problem. `issue` is written at the end of pass 4, because `sooner` needs both the edges and the trips.

| shape | class |
|---|---|
| No loop, or only `do n[constant]` | `O(1)`. The constant stays on `trip`, so `do n[2249868]` is `O(1)` with trip `2249868`. |
| `i < n`, or `do n[n]`, and `n` is not written in the loop | `O(n)`. The trip is `n`. |
| Nested loops, each with its own cursor | The degrees multiply. Two cursors both bounded by `n` are `O(n^2)`. Different names stay written: `O(m*n)`. |
| An inner loop that only advances a cursor an outer loop already counts | It does not multiply. The shared cursor still hits its bound once. |
| A call whose callee already has a class | The callee's degree is added to the loops around the call. |
| No named bound, a `start` or `run`, a callee that is `unknown`, or a call cycle | `unknown`. `why` says which. |

An early `leave` does not change the class. The bound is a ceiling, so the loop is still `O(n)`, and the exact trip is a range this pass does not write. Sequential loops take the heavier class, not the sum. `time` on the symbol row is this class. `cost` stays `unknown`: this is not a cycle count.

## Two views of the same records

The machine view is the braced records. One field per line, values split on spaces, `-` when empty. A parser stops at the line `--- source ---`.

The human view is the source with a `<<<>>>` note on the lines the table mentioned. The note on a declaration is the whole record. The note on a use is only what that line reads or writes. These comments are printed. They are not part of the program, and feeding them back in is not a goal.

## Annotation pointers

A call, a method call, `start`, and `run` each get one `annptr` record. The id (`ap1`, `ap2`, …) is that invocation. Two calls to the same function are two ids. The record holds:

| field | what it is |
|---|---|
| `id`, `instance` | This invocation. The two fields are the same id. |
| `kind` | `call`, `method`, `start`, or `run`. |
| `name`, `line`, `caller` | The name as written, the line, and the function that contains the site. |
| `decl`, `decl_line` | The symbol-table slot of the declaration, and that declaration's line. A builtin has `decl -`. |
| `recv` | The receiver, for a method. `-` otherwise. |
| `args` | The argument names. A literal is its kind (`int`, `f64`), not a made-up name. |
| `effect` | The callee's effect from its own record: `pure`, a builtin name, or the effect list. |

The declaration lists those ids on `invoked`. A variable stays on its `name` record. This is still not a runtime pointer, and it still does not delete a check.

## Call table

Every read, write, and call is also an `op` in one master table. The atom is one of five:

| atom | when |
|---|---|
| `get` | A read of a name. |
| `set` | A write of a name, including the initializer. |
| `pure` | A call whose body has no effect and no write through a pointer, `HERE`, or other parameter, and whose own calls are pure. |
| `impure` | A call that prints, loads, starts, runs, or calls something impure. `start` and `run` are impure even when the worker is pure. |
| `mutable` | A call that writes through a pointer or a parameter, or that calls a mutable function. |

`in` is the locations the op reads. `out` is where the result goes. `instance` is the `annptr` for a call, and `-` for a get or a set.

`drop yes` means the result is already the next location, so a later pass does not need a temporary and a copy. `out here` means that location is an input of the parent call (`into op5.in0`). `out` a name means the result is stored straight into that name. `into out` means the result is the function's own return. Anything else stays `tmp`.

```text
var = a in[b in[param]];
```

`b` is `atom pure`, `out here`, `into` the input of `a`. `a` is `atom pure`, `out var`. The return register of `b` is the argument register of `a`, and the return register of `a` is `var`. The table records that. It does not rewrite the assembly yet.

## Flow

`flow`, `ctrl`, `swap`, and `issue` are the map of how a function runs. They do not reorder it, and they do not invent a function to stand in for another.

| record | what it says |
|---|---|
| `flow value` | This op produces a value that op reads. A call dropped into the next input is one of these. |
| `flow order` | Same name, or a mutable call: the later op waits. A write waits for the reads of the value it replaces. |
| `flow back` | The dependence crosses a loop iteration. |
| `ctrl` | `then`, `else`, `enter`, `back`, `leave`, `merge`. Lines, not blocks. |
| `swap` | A closed pipe of pure calls. Each result is the only call input of the next. A later rule may replace the pipe if it can prove the pipe acts as one function. `closed yes` is the permission to consider that. It is not the proof. |
| `issue` | A call that needs two or more calls. `chain` is a pipe into one input (`a` flows into `b`). `free` is an input with no path to that chain, so it may run while the chain runs. `sooner yes` means every free trip is a known number smaller than the chain's tail, so the free call is done when the tail finishes if it starts with the tail. `sooner unknown` means a trip is not a number. `sooner no` means the free call is not shorter. |

An impure call is never `free`. Two prints stay in source order. A name written by one call and read by another is not free, even when both calls are pure.


## Patterns

`reduce` counts source operations a common pattern can delete. `static` is only those. It is `drop` plus `reuse` plus `inc`.

| pattern | what it deletes |
|---|---|
| `drop` | The copy of a call result into a temporary. The result is already the next location. |
| `reuse` | A second `get` of a name on the same line, with no `set` of it in between. The value is already in a register. |
| `inc` | The `get` of a cursor that is only `n = n + 1`. The store is an increment of that register. |
| `range` | `x >= A && x <= B` with constant bounds. One unsigned compare, not two compares and an `and`. Counted on its own. It is not added into `static`, because that `and` is not one of the table's ops. When this test is the condition of an `if`, the compiler emits `sub` / `cmp` / `ja` instead of the `setcc` chains. A stored bool is left as written. |
| `forward` | A loop whose cursor only increments. A pointer walk, not an index recalculated from zero. A shape, not a deleted op. |

On the Cicero scan this is a few dozen source ops, not half the table. The larger cut is the lowering under each of those ops: a compare becomes a chain of boolean stores, and an index becomes a multiply, a bounds check, and a present check. Those are the phase-4 rewrites. This table is what tells them the pattern is legal.



## What it is not

Not the scored symbol table in [GOALS.md](GOALS.md). No lower, average, or upper cycle count. The `profile` pass is only the O() ceiling above. It is not a measured run, and a loop written without a bound is not given one.

Not a pass that deletes a bounds check. The cursor and the bound are the fact that pass would read. The phase-4 reductions in [x86-op-table.md](x86-op-table.md) are still not built.
