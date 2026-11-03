# Goals

Not 1.1. Not a change to the v1.0 hosts. The 1.1 list is [FIXES_NEEDED.md](FIXES_NEEDED.md). This file is the later machine: a JIT, a symbol table, and self-modifying code that is analysis, then a write, then a replacement.

## Compile and JIT

The brain watches what common neurons do and flattens some of them into pure functions that can run on the GPU. That watcher is a Java-like JIT. It is not a new pointer type. The GPU is for one shape only: the same instruction over many uniform items. A data-dependent branch, such as `if` on `x % 2`, splits the lockstep. The hardware then runs the sides one after another, and the flatten was a loss. The JIT tracks that divergence and refuses the GPU for it. Scalar control flow stays on the CPU. Copying the data and compiling a kernel is worth it only when the run is large and uniform. A short function sent across the bus mid-execution is not.

Self-modifying code is generated. It is not made by chasing pointers, and it is not made by writing new bytes under the instruction pointer. A CPU keeps instructions in a different cache from data. A store into a live function is invisible until that instruction cache is invalidated, and doing it on every jit step stalls the pipeline. The replacement is a dispatch slot. The interpreter currently calls the body. A dirty flag says a compiled form is ready. The next call is an ordinary call to that function. The new bytes are written on another page, checked, and then one pointer flips. A virtual call is acceptable. The first runs are a warmup. The compiled form is saved, so the next process does not warm up again. That calculation is worth doing.

v1.0 has no bytecode. The interpreter walks the parse tree. The compiler prints amd64 and stops. A JIT that slices bytecode is a new representation. It is not a pass on the tree that exists.

## Symbol table

Bake functions and symbols to a table at compile time. A program runs without the table. The table is optional and cheap to keep, and the JIT is blind without it.

One row, scored:

| field | what it is |
|---|---|
| cost | A heuristic, not a cycle counter. Lower bound, average, upper bound, and unknown. Exact cycles of a loop with a dynamic bound are not a field. A snapshot may set a reasonable upper bound. A later profile may replace the average. The processor changes the real cost of the same bytes from moment to moment. |
| time | Time complexity of the body, or unknown. |
| resources | What the body needs. |
| inputs | Parameters. |
| outputs | Results. |
| variables | Pointers to the variables named in the body. |
| functions | Functions named in the body. |
| invokes | Every call. |

The JIT reads this while the program runs. New code from a self-modify step is written under `selfmodcode/generated`. That directory is the record. It is how an AI system writes and changes its own code without a C++ pile of pointers, and how the result is still there after the process exits.

This is dynamic inlining plus runtime profiling, with a symbol table that already carries the heuristics. Use tracking in the 1.1 list is the "how often, and where" column. It is not the whole table.

## Snapshot, branch, salvage

The problems, and the approach that was named for each:

- Snapshots need metadata. That is acceptable.
- Rollback and roll-forward can stick the system in a loop.
- A diff reverts memory. It does not revert a packet, a hardware log, a database write, or a deleted file. Once an effect crosses that line, rolling the registers back leaves the system believing a thing that the world has already done. The branch chain is for internal state. The system has to know which effects it is allowed to undo, and it does not undo the others. When downtime is acceptable, it pauses and flushes before the snapshot. An AI can tolerate that pause. A person sleeping is the same shape. Do not invent a second history for the outside world.
- Side effects inside that boundary still need a check. The store is a branching save with diffs. Git can be that store: a submodule on the machine that runs the system, treated as a service. Each mutation is a commit on a branch.
- If a mutation fails, look back, revert, or branch. A SALVAGE subroutine may take code that passes verification from any branch, when that code does not break the rest. This is a real risk. A block from one branch can depend on a padding or an alignment that another branch no longer has. Two pieces that each pass can fail together, and the number of combinations grows faster than the branches. Salvage stays a checked subroutine, not an automatic merge. The symbol row has to carry the layout assumptions, or the check has nothing to read.
- Mutable and immutable techniques both apply. Neither is chosen here.
- The table needs benchmarks.
- The watcher needs to see a local minimum and a local maximum, and not treat either as finished.
- [sera5m/ikitaku](https://github.com/sera5m/ikitaku) is parked. It is the back component for verification and for leaving some local minima. It is a reasoner with a read-only graph. It is not a compiler pass, and [x86-op-table.md](x86-op-table.md) already says it is not a dependency. Using it behind the JIT does not make it one until this goal is built.
- The symbol table may need a wider API than the row above. Ikitaku, or a later IR pass, may hand the JIT a hint. Hints are not the table, and they are not required for a program to run.

## What this is not

Not pointer chasing. Not a rewrite of a live object in the Pages sense. Not required for `fn main` to run. Not v1.0, and not a reason to put bytecode back into the 1.0 compiler.
