# Toplethos BASE standard

Backend is the C++ VM (`rs_vm`) on watch and desktop. Python `toplethos_run.py` is the desktop reference for the same language. [MATRIX.md](MATRIX.md) is the target language. This file is what the targets implement.

## Landed from the matrix

Python reference:

- Assignment is `name = expr`. `expr = name` is an error.
- A name is declared before it is assigned. Unmarked bindings are immutable. `@mut` allows a later store.
- `@mut` with `@val`, and `@mut` on `config`, are errors.
- A user call is `name in[]`. `name()` is an error. `()` is grouping.
- `sin[90]` and the same bracket form for `cos`, `tan`, `sin_amp`, `native`, `py`, `ccall`. The old `sin(90)` form still runs.
- `@parallel[n]` and `@parallel(n)`.
- `@require[expr]` fails into `onfail`. No `onfail` walks the failure out. `STOP[THIS]` halts. `onwarn` is parsed and does not halt.
- An include is loaded once. A cycle means already loaded.

C parser, shared by the watch and the desktop (`OIRIA_OS_espIDF` `os_code/core/rs_vm/vm/rs_vm_parse.cpp`):

- `expr = name` is a parse error.
- `@parallel[n]` and `@autounroll[n]` are accepted beside the parenthesis form.
- User calls were already `name in[]` only.

Not in this slice: flow maps, `formatconvert`, attribute macros, multiple dispatch, comptime, and the GPU or CPU scheduler. Those stay in the matrix until they have a spelling the parser can read.

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
