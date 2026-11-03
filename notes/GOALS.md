# Goals

Not 1.1. Not a change to the v1.0 hosts. The 1.1 list is [FIXES_NEEDED.md](FIXES_NEEDED.md). This file is the later machine: a JIT, a symbol table, and self-modifying code that is analysis, then a write, then a replacement.

## Compile and JIT

The brain watches what common neurons do and flattens some of them into pure functions that can run on the GPU. That watcher is a Java-like JIT. It is not a new pointer type.

Self-modifying code is generated. It is not made by chasing pointers. The steps are analysis, write, in-place replacement. A function used often is compiled from a segment of interpreter bytecode. After the run, the JIT may compile again, and a transform may reduce a recursion.

v1.0 has no bytecode. The interpreter walks the parse tree. The compiler prints amd64 and stops. A JIT that slices bytecode is a new representation. It is not a pass on the tree that exists.

## Symbol table

Bake functions and symbols to a table at compile time. A program runs without the table. The table is optional and cheap to keep, and the JIT is blind without it.

One row, scored:

| field | what it is |
|---|---|
| cost | Cycles. |
| time | Time complexity of the body. |
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
- Side effects need a formal check, and that check needs a stateful machine. The machine is a branching save with diffs, the way metacognition would keep a thought it might abandon. Git can be that store: a submodule on the machine that runs the system, treated as a service. Each mutation is a commit on a branch. The history is a diff chain.
- If a mutation fails, look back, revert, or branch. A SALVAGE subroutine may take code that passes verification from any branch, when that code does not break the rest. A branch can do an unrelated thing right and still be wrong for this program.
- Mutable and immutable techniques both apply. Neither is chosen here.
- The table needs benchmarks.
- The watcher needs to see a local minimum and a local maximum, and not treat either as finished.
- [sera5m/ikitaku](https://github.com/sera5m/ikitaku) is parked. It is the back component for verification and for leaving some local minima. It is a reasoner with a read-only graph. It is not a compiler pass, and [x86-op-table.md](x86-op-table.md) already says it is not a dependency. Using it behind the JIT does not make it one until this goal is built.
- The symbol table may need a wider API than the row above. Ikitaku, or a later IR pass, may hand the JIT a hint. Hints are not the table, and they are not required for a program to run.

## What this is not

Not pointer chasing. Not a rewrite of a live object in the Pages sense. Not required for `fn main` to run. Not v1.0, and not a reason to put bytecode back into the 1.0 compiler.
