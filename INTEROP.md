# Live memory interop (NOTE — not implemented)

Do **not** wire TensorFlow C / shared C++ heaps in this pass.

## Intent (later)

A running Toplethos image should be able to **see the same bytes** as a live
C/C++ or Python object: a TF tensor, a NumPy array, a `std::vector`, a
struct in the host process.

That is **not** `native("strlen")` (copy args in, copy result out). It is
borrowing a pointer for the lifetime of a call:

| Host | Wanted |
|------|--------|
| Python | `memoryview` / `numpy.ndarray.__array_interface__` / buffer protocol |
| C++ | `rsvm_val_t` with `TY_PTR` pointing at host memory, no copy |
| TensorFlow C | `TF_TensorData()` + dims, wrap as Toplethos array view |
| Watch | too small; never map TF. GPIO/LCD buffers only if host installs them |

## Why it is deferred

- Lifetime: who frees the tensor if Toplethos stores the pointer in a slot?
- Alignment / dtype: TF is float32 NHWC, Toplethos heap is `rsvm_val_t` tagged i32
- GIL vs `@parallel(n)` on desktop
- Watch has no TF and a 2 KB heap

## What desktop **does** today (and should keep doing)

Desktop is the powerful host. Call out, don't share heaps yet:

```toplethos
print(py("math.sqrt", 9));
print(native("libc.so.6:strlen", "hi"));
```

`py("module.fn", …)` imports Python. `native("lib:sym", …)` is ctypes.
That is enough to **drive** TensorFlow from Python (`py("tensorflow.function", …)`
is a future experiment, not a promise). Sharing a live `TF_Tensor*` into
`ARR_LD` is explicitly out of scope until a buffer-protocol RFC exists.

Watch: `host.native_call` is still NULL (`vm_failures.md`).
