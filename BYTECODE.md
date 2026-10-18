# Toplethos bytecode vs Java / C#

Toplethos compiles `.vul` to a **small custom image** (`RSV2`), not JVM classfiles or .NET IL.

## Image (`rsvm_image_hdr_t`)

```
magic RSV2 | version | flags | code_len | entry | step_limit
slot_count | func_count | struct_count | enum_count
then: opcode stream, name table, func table, structs, enums
```

Opcodes are one byte + immediates (see `rs_vm_opcodes.h`): `PUSHI`, `ADD`, `CALL`, `ARR_NEWD`, `SIN`, `SYS_LS`, …

The machine is a **stack VM** with a fixed slot file (locals), a tiny heap for arrays/structs, and a step limit. `rsvm_run` is a `switch (op)` loop. There is **no JIT** on the watch and no class loader.

## vs Java / C#

| | Java / C# | Toplethos |
|--|-----------|--------|
| Unit | class + methods, GC objects | one image, slots + small heap |
| Bytecode | JVMS / CIL, huge spec | ~100 opcodes, MCU-sized |
| Types | objects, generics | i32/f32/bool/str/arr/struct |
| Runtime | JVM / CLR, JIT, threads | interpreter; trapdoor loops **and** `native_seq` are C `for` inside the VM |
| Native | JNI / P/Invoke | `native()` host callback; `native_seq` is a packed nid array (`OP_NATIVE_SEQ` 0xF2), not N JNI hops |
| Verify | bytecode verifier | step limit + type tags on values |

Same **idea** as Java (compile once, interpret/run everywhere). Not compatible with `javac` or `dotnet`.
