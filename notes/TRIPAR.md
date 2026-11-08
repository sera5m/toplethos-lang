# 1.2 tripar

This tree. `python3 conformance/check.py` prints `1.2 tripar`. The compiler is `compiler/tpcc`. There is no second compiler.

`--run` interprets the bytecode. That interpreter is the oracle. `--asm` and `--target` compile that same bytecode. `--jit` compiles an `@inlined` function from it while the interpreter is still running, checks the native result against the oracle, and flips the dispatch slot. A native result that disagrees is dropped.

The Python compiler is gone. `lang/` has no parser, interpreter, or emitter. `aot/tpc.py` is gone. Those programs do not come back.

The suite is the 1.0 texts in `conformance/check.py`, including the rows that must be rejected. `make -C compiler test` is still the older battery and stays green. Spec text is still 1.0 in [USE.md](../USE.md). 1.2 is the compiler release, not a new spec.

What this release runs, and only on the interpreter:

- Arithmetic the suite asks for: `**`, `/_/`, `~`, `+=`, `$=`, `!$=`, and `as`.
- `print`. If the program printed, stdout is the prints and there is no `result` line.
- Objects, fields, methods, `@childof`, and `@ismutationof`. A method receives `HERE` as an address. The program still loads and stores only pointer kind `cell`.
- Arrays, `do`, and named `leave`.
- Enums and `match @total`, desugared to `if`.
- `@static` and `@config` stick for the process. `@val` and `@config` reject a later store. `@xor` is a copy.
- `@ephemeral`, `state`, and absent slots.
- `@divzero` stand-in. A bare `1/0` is still division by zero.
- `@require`.
- `fork` / `start` / `finish`, `@mutex`, `lock`, `unlock`, and `hold`. Workers run one after another on the interpreter. The lock is a count the interpreter checks. It is not an OS thread and not a GPU dispatch.
- `include`. The suite writes `/tmp/tp_lib.tpth_s` before that row.

`@NoFailTolerance`, `@gpuparallel`, and `namespace` are rejected on purpose.

`tpcc --asm` (amd64, then gcc) also prints the cross-language kernels: `sh aot/mix.sh`, `sh aot/battery.sh`, and the dict and table rows of `sh aot/tasks.sh`. That listing has f64 arithmetic, `as`, `print`, `@time["name"] watch`, and `do`. The index `i` belongs to that `do`, including a nested one. `run worker in[n]` is fork then start. The lanes still run one after another. Lock and unlock are empty there, so the mutex checksum matches and the time does not show contention. It is not an OS thread. `@static` in that listing is initialized on entry; those programs call `main` once. The interpreter is still the oracle, and it still keeps a real lock count and a sticky static. `sh aot/battery.sh` also runs `tpcc --run` on the same N=2000000 program. That interpreter stops after 10000000 steps. The numbers next to C++ and Rust are the `--asm` rows.

Not done: SPIR-V is still a listing. It is not a Vulkan module. arm64 and C++ do not print f64, fork, or `@time`. Scalar control stays on the CPU. `sh aot/tasks.sh` still stops after table: glitch, blur, words, and rdp need host file calls this listing does not have.
