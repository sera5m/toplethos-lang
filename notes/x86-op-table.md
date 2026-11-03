# x86 op table

This is the dependency named in [OPTIMIZER.dep](OPTIMIZER.dep). A phase-3 rule may exist only for a row in this table. `7etsuo/x86` is a reference, not a library, and it is not vendored. `sera5m/ikitaku` is not a dependency either: that repository is a reasoner with a read-only graph, not a code-pattern optimizer.

## What 7etsuo/x86 actually is

[7etsuo/x86](https://github.com/7etsuo/x86) is one README plus a Linux syscall sheet. The README is 16-bit teaching material (`CX`, `MOV` of a byte or a word). It is alphabetical, not grouped by function, and it has no SSE and no x86-64.

The integer ops we already emit are in that sheet: `ADD`, `SUB`, `IMUL`, `IDIV`, `CMP`, `MOV`, `LEA`, `CALL`, `RET`, `JMP`, `Jcc`. The f64 ops are not: `addsd`, `subsd`, `mulsd`, `divsd`, `movsd`, `ucomisd`, `cvtsi2sd`.

`LOOP label` is in the sheet. It decrements `CX` and jumps if the count is not zero. The displacement is 8 bits. GCC and LLVM do not emit it. A counted loop here stays `cmpl` / `jge` / `incl` / `jmp`, which is what `do n[k]` already is. Phase 3 must not rewrite that to `LOOP`.

## Scores

Similarity 5 means one opcode is the operation. Ambiguity 5 means the source form can lower many ways. Local simplicity 5 means this compiler already emits the short form. "Contained" means Toplethos has the operation.

| toplethos | closest x86 | sim | amb | local | contained | one row |
|---|---|---:|---:|---:|---|---|
| i32 add / sub | `addl` / `subl` | 5 | 2 | 2 | yes | yes |
| i32 mul | `imull` | 5 | 2 | 2 | yes | yes |
| i32 div | `cdq`, then `idiv` | 3 | 4 | 2 | yes | yes |
| i32 mod | same `idiv`, remainder in `edx` | 3 | 4 | 2 | specified, not emitted | yes |
| f64 add / sub / mul / div | `addsd` `subsd` `mulsd` `divsd` | 5 | 2 | 1 | yes | yes |
| cmp to bool | `cmpl` or `ucomisd`, then `setcc` | 3 | 4 | 1 | yes | yes |
| `&&` / `||` | `test` + `jcc` around the right side | 2 | 4 | 3 | yes | yes |
| `!` | `xor $1`, not the `not` opcode | 4 | 2 | 3 | yes | yes |
| `as[f64, i32]` | `cvtsi2sd` | 5 | 2 | 2 | yes | yes |
| `as[i32, f64]` | `cvttsd2si` | 4 | 4 | 2 | yes | yes |
| `do n[k]` | `cmpl` / `jge` / `incl` / `jmp` | 3 | 2 | 3 | yes | yes |
| `jmp` (`leave`, `continue`, loop back-edge) | `jmp` | 5 | 1 | 5 | yes | yes |
| `if` | `test` + `jz` | 4 | 2 | 4 | yes | yes |
| load of a named cell | `mov`/`movsd` plus a present-flag `cmpb` | 2 | 5 | 1 | yes | no |
| store of a named cell | `mov`/`movsd`, then `movb $1` on the flag | 2 | 3 | 2 | yes | yes |
| call / return | `call` / `ret` with the System V registers | 4 | 2 | 3 | yes | yes |
| present-flag check | `cmpb` + `je` abort. Not a source operation | 3 | 2 | 2 | compiler | no |
| `c + x * y` | `mulsd` then `addsd`. Not `vfmadd213sd` | 4 | 5 | 3 | pattern | pattern |
| array index | bounds check, then `* 16`. Scale 16 is not a SIB scale | 3 | 4 | 1 | yes | no |
| x86 `LOOP` | do not emit | 2 | 1 | 0 | no | no |

Local simplicity is low on f64 arithmetic because each operand used to be a flag check and a stack spill. Phase 1 now marks a cell `fast` once a store has made it present, and the load is then one `movsd`. Phase 3 fuses two or more `y = c + x * y` steps into one chain so `y` stays in `xmm0`.

`vfmadd213sd` is one rounding. `mulsd` then `addsd` is two. The C++ battery is built with `-ffp-contract=off`, so the chain keeps both roundings. Contracting is forbidden in the dep file. A field, or an index whose index is a constant, may be the added term. Those loads can fail, so `y` is stored after each of those steps. A constant term cannot fail, and `y` stays in `xmm0` until the chain ends.

A load stays off the one-row table. The flag check is still required when phase 1 cannot see a previous store, and an ephemeral load must wipe the cell.

## Phase 4, not implemented

These are real, and they belong on the tree or on the compiler's own instruction list. A pass over the finished `.s` text cannot see that a present flag is dead.

| technique | where it already exists | use it on |
|---|---|---|
| promote stack slots to registers | LLVM `mem2reg` | the tree, before emit. This is the pass that matches the cell protocol |
| algebraic combine | LLVM `InstCombine` | the tree. Phase 1 is the small version |
| induction / strength reduction | LLVM `LoopStrengthReduce`, GCC ivopts | the tree. `i * 0.000001` can become an add of the step |
| peephole | GCC `define_peephole2`, LLVM `PeepholeOptimizer` | instructions, after lowering, not the `.s` file |
| register allocation | LLVM greedy RA, GCC IRA | instructions, before emit |
| scheduling | LLVM MachineScheduler | instructions. Needs flag and alias dependencies |
| post-link layout | LLVM BOLT | the finished binary. It does not delete the flag checks |

`LOOP` is not on this list. Neither is a software prefetch of `i`. Once `i`, `x`, and `y` are registers, they are already in L1. The cost was the flag checks and the reloads, not a cache miss.
