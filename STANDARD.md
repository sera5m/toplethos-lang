# Toplethos BASE standard

Backend is the C++ VM (`rs_vm`) on watch and desktop. Python `toplethos_run.py` is the desktop reference for the same language.

## Must

- `.vul` → bytecode (`RSV2`) via `rsvm_compile` / `rsvm_eval`
- LUT `sin` `cos` `tan` `sin_amp` (degrees, wrap) as **C-style** `sin(90)` and as `in[]` calls
- `include "rel.vul";` / `import`
- Arrays: `arr_new(n)` / `arr_new(d0,d1)` and C-style `i32 a[2][3]` (row-major, max 4 dims)
- Index `a[i]` / `a[i,j]`
- `@memory_hard` on a variable: persist array on disk (desktop) / property bit on watch
- `native("strlen", s)` / `py("math.sqrt", x)` / `ccall` (desktop); watch needs `host.native_call`
- `native_seq(native("wave", …), native("delay", ms), …)` — trapdoor: interned nid array walked in C (one UART blob, not N source translates). Runtime form: packed `i32` array stride 7 (`nid, nargs, a0..a4`)
- `@latex_internal` rewrite
- Parse errors carry **line** (C++ `rsvm_parse_err_t`; Python `RunError.line`)

Image header magic `RSV2` — see BYTECODE.md.
