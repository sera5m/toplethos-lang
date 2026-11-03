# Reference table

Short version of the long note. This is the static table, built before emit. It is not a rewrite, and it is not ikitaku.

The arithmetic, the `@globaloptimize` levels, and `@inlined` are already in the hosts. `@globaloptimize[02]` still selects `01`. Level `03` tags a function with a verb and may turn `* 2^k` into a shift. Ikitaku, if it is asked at all, is asked after a rewrite: proof, counterexample, or unknown. It does not fill this table.

## What the table is

One record per function, one per name in that function, one per loop. The compiler already runs this from the parse, before the passes. `tpc --analyze file.tpth_s` prints it and does not compile.

Counts are source sites, not runs. `i = i + 1` once in the text is one write, however many times the loop runs. A dynamic total is a later cost field. It is not this table.

A name record holds the type, the decl line, every read line, every write line, and the function it sits in. A function record holds the shape, the calls, and the callers. A loop record says whether it has a cursor.

A cursor is a name whose every store in that loop is `n = n + 1` or `++n`. A `do` counts, and its cursor is `i`. A bound is an integer name compared with that cursor (`i < n`) and not written in the loop. The count of a `do` is a bound when it is a name. `slot = slot + 1` followed by `slot = 0` is not a cursor. The function is `O(n)` only when every loop is one of these forward loops. Otherwise it is `unknown`, and the loop records still say which loops moved forward.

## Two views of the same records

The machine view is the braced records. One field per line, values split on spaces, `-` when empty. A parser stops at the line `--- source ---`.

The human view is the source with a `<<<>>>` note on the lines the table mentioned. The note on a declaration is the whole record. The note on a use is only what that line reads or writes. These comments are printed. They are not part of the program, and feeding them back in is not a goal.

## What it is not

Not the scored symbol table in [GOALS.md](GOALS.md). No lower, average, or upper bound. No profile.

Not a pass that deletes a bounds check. The cursor and the bound are the fact that pass would read. The phase-4 reductions in [x86-op-table.md](x86-op-table.md) are still not built.
