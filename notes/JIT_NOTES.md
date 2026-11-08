# JIT notes

A short record of the long pass. Not a spec. The running pieces are in USE.md.

## What was said

The AI is not the JIT. It chooses. The JIT knows where the function is. Half the features the AI wants already live in the runtime. The AI uses them.

Snapshots are hashes. A hash already tried is not tried again. Effects are pure, reversible, or irreversible. Only the first two roll back. An irreversible effect waits for a gate. Check a function, not the whole program: same inputs, then a shadow run, then the pointer flip. Salvage one pure function and check it again in the merged state. Cost is a lower bound, an average, an upper bound, or unknown. Counts are sampled and they decay. The useful count is who called whom. Save the tree, not the machine code. The verifier, the scorer, the effect gate, and the snapshot store are not rewritten by a mutation.

Not chosen: a solver on every function, equality saturation, capability pointers, simulated annealing.

## The annotation the table has to expose

For each name: the decl line, every read line, every write line, and the function it sits in. For each loop: whether it has a cursor that only increments, and the integer names it only reads. For each function: the shape, the calls, and the callers. A call, a method, `start`, and `run` also get an `annptr`: the site, the declaration it points at, and an id for that one invocation. Counts are source sites, not runs. Pass 3 is the dependence map. Pass 4 writes an `O()` only for a loop whose bound is already in the text, walking calls in that map's order. A nested loop on the same cursor does not multiply. The records and the `<<<>>>` view are in [ANALYSIS.md](ANALYSIS.md).

The word-count example is one forward scan. The index is not reset, so the nested loop is still linear in the length of the text. The map costs the same bound, because the slices add up to that length. Space is the text plus the unique keys. That is the kind of fact the table is for. It is not a proof of a hash-map cost the language does not have yet.

`compiler/tpcc --active file.tpth_s` records call counts and caller edges while the interpreter runs. The old `python3 aot/tpc.py --analyze` command is gone with the Python compiler.

## Levels

`@globaloptimize[01]` is the default passes. `02` selects `01`. `03` tags each function from the locked ikitaku verb list, then runs those passes, then turns an integer multiply by a positive power of two into a shift.

`@inlined` on a pure scalar function: the interpreter compiles it and calls that symbol. A function with a side effect, a pointer, or a method is rejected. The compiler already emits a normal call.

## C++ pipeline, 2026-11-07

The compiler is `compiler/tpcc`. The Python compiler has been removed. This is not a 1.2 tripar claim. That name waits on the conformance suite.

In the working tree, and covered by `make test`: the state-machine lexer, bind against the type map, typed three-address bytecode as the source of truth after bind, const-fold and DCE before any backend, the arch permission table, amd64 and arm64 listings, C++ transpilation, the interpreter as the CPU oracle, control flow, the shift/div/neg failures, `threads` and `freemem`, the static counts, active call edges, the pointer-kind list, and `cell` (`T*`, `&x`, `*p`). `@inlined` on a pure scalar function compiles that function's bytecode to an amd64 listing while the interpreter is still running, checks the native result against that oracle, then flips the dispatch slot. A scalar loop stays on the CPU even when every opcode is one SPIR-V could print.

Not written: a SPIR-V/Vulkan backend that emits a module a driver can load. `compiler/src/emit_spirv.cpp` is still an operation listing. It has no entry point, no buffer bindings, and no `OpSelectionMerge` / `OpLoopMerge` a loader would accept. Vulkan is still the runtime. OpenGL is not a target. Deal with the real emit next. The JIT must not grow a second IR to get there. Bytecode in, one backend out.

The amd64 JIT does turn bytecode into assembly. `emit(Target::Amd64)` prints the `.s`. `gcc -shared` only assembles and links that text. It does not compile C, and it does not see the Toplethos source. Turning that assembly back into bytecode is not implemented. It would be a decompiler of this backend's spelling, and it would not recover the bytecode: constants are already folded, widths are only `eax` versus `rax`, and a `BrRange` is a `sub`/`cmp`/`ja`, not the opcode. The `.s` is not a second source of truth.
