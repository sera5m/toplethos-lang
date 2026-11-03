# JIT notes

A short record of the long pass. Not a spec. The running pieces are in USE.md.

## What was said

The AI is not the JIT. It chooses. The JIT knows where the function is. Half the features the AI wants already live in the runtime. The AI uses them.

Snapshots are hashes. A hash already tried is not tried again. Effects are pure, reversible, or irreversible. Only the first two roll back. An irreversible effect waits for a gate. Check a function, not the whole program: same inputs, then a shadow run, then the pointer flip. Salvage one pure function and check it again in the merged state. Cost is a lower bound, an average, an upper bound, or unknown. Counts are sampled and they decay. The useful count is who called whom. Save the tree, not the machine code. The verifier, the scorer, the effect gate, and the snapshot store are not rewritten by a mutation.

Not chosen: a solver on every function, equality saturation, capability pointers, simulated annealing.

## The annotation the table has to expose

For each name: where it is written, where it is read, and how many times. For each loop: whether the index only moves forward. For each call: the callee. For the function: a shape, an effect, and a complexity of `O(n)` or unknown.

The word-count example is one forward scan. The index is not reset, so the nested loop is still linear in the length of the text. The map costs the same bound, because the slices add up to that length. Space is the text plus the unique keys. That is the kind of fact the table is for. It is not a proof of a hash-map cost the language does not have yet.

`python3 aot/tpc.py --analyze file.tpth_s` prints that table and does not compile. At level `03` the same row is also the tag.

## Levels

`@globaloptimize[01]` is the default passes. `02` selects `01`. `03` tags each function from the locked ikitaku verb list, then runs those passes, then turns an integer multiply by a positive power of two into a shift.

`@inlined` on a pure scalar function: the interpreter compiles it and calls that symbol. A function with a side effect, a pointer, or a method is rejected. The compiler already emits a normal call.
