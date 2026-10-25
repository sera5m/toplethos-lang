Host or runner note. Not language 1.0. [USE.md](USE.md) is the spec.

# Parallel batch execution (desktop backend)

Toplethos can enqueue function calls with per-item parameters and run them as a data-parallel batch.

## Annotations / native entry points

- `@load_into_parallel_exec_queue` → native `pbatch_enqueue`
  - Args: `fn_id, attr_word, n_in, in_slot0 … in_slotN, out_slot`
  - Each call carries its own attributes and parameter slots.
  - Queue auto-flushes when it reaches the fill trigger (default 64).

- `@exec_parallel` → native `pbatch_flush`
  - Forces immediate execution of everything currently queued.
  - Returns the number of items executed.

## Queue behaviour

- One shared queue in the desktop host (`g_pbatch`).
- Items with the same `fn_id` are natural candidates for a single polymorphic kernel launch.
- `attr_word` selects the variant (f32 vs i32, saturating, etc.).
- Current flush is sequential placeholder; replace the body of `rsvm_pbatch_flush` with a thread-pool or GPU grid launch that consumes the item array.

## Example (conceptual)

```
// enqueue many magnitude-compares with different slices
native("pbatch_enqueue", fn_magcmp, ATTR_F32, 2, slot_a, slot_b, out1);
native("pbatch_enqueue", fn_magcmp, ATTR_F32, 2, slot_c, slot_d, out2);
// … more …
native("pbatch_flush");   // or wait for fill trigger
```

CPU remains source of truth. The queue only ever sees flat, regular work.
