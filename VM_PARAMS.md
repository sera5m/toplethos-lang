# VM parameters (both interpreters)

| Directive | Python desktop | C++ / watch |
|-----------|----------------|-------------|
| `set_max_run n;` (alias `set_step_depth`) | statement cap | `vm->step_limit` |
| `set_ram n;` | max array cells | `vm->ram_limit` (heap bytes, default `RSVM_HEAP_BYTES`) |
| `set_storage n;` | max bytes under `.toplethos_hard/` | `vm->storage_limit` (0 = no disk) |
| `set_threads n;` | thread pool cap | `thread_cap` — **1 on watch** |
| `@parallel(n)` / `@threaded` | `do n[…]` uses a pool of min(n, cap) | sequential fallback (`thread_ifsingle`) |

Env overrides on desktop: `TOPLETHOS_MAX_RUN`, `TOPLETHOS_RAM_CELLS`, `TOPLETHOS_STORAGE`, `TOPLETHOS_THREADS`.
