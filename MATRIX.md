# Feature matrix

Decisions from 2026-10-23. This is the target language, not what `toplethos_run.py` implements today. `STANDARD.md` stays the current runner until these land.

Already locked, before this matrix:

- `()` is math grouping only. Calls are `name in[…]` and `name in[…] out[…]`.
- Assignment is `name = expr`. `expr = name` is a parse error.
- `[]` stays for `if`, `do n`, calls, and indexing.

## Types

| decision | rule |
|---|---|
| Declare before assign | `i32 y; y = expr;`. A name on the left of `=` must already exist. |
| Immutable unless `@mut` | An unmarked binding cannot be written. `@mut` is what allows a later store. |
| Option | No missing-value hole. Absence is `Option`. |
| Result | No exceptions. Failure is a `Result` the caller handles. |
| Distinct newtypes | `Hz` and `Pin` do not match just because both store an integer. |
| Structural match is not the default | Nominal types stay distinct. `^=` is true only when a flow map says the layouts correspond. |

A flow map names the field correspondence, including a different order and a partial set of fields. No flow map means not structurally equal, even when the field names look the same.

One operator: `^=`. Not a second spelling.

Conversion is always a call, never an implicit coercion:

```text
vec4 color = formatconvert in[src] out[here];
vec4 color = formatconvert in[src] out[new];
```

`out[here]` fills the binding on the left, which was declared. `out[new]` allocates that value on the heap and the left-hand name points at it.

`@ignore_if_structure_matches` is an effect on a fn. While it is present, a parameter may accept a `^=` source without `formatconvert`. It is never the default.

## Duration, separate from `@mut`

The old unmarked behavior (mutable, global, freed by hand) is retired. Unmarked means immutable and `normal`.

| mark | lifetime |
|---|---|
| `ephemeral` | Lives for one use inside a call, then the cell is emptied. For a password, not for general scratch. |
| `normal` | Unmarked duration. Lives in the enclosing fn. `normaldur` is the same word, not a second mode. |
| `scoped` | Until this fn returns. `local` is the same word. |
| `static` | Program lifetime. C++ static. Writable at runtime only if also `@mut`. |
| `config` | Program lifetime, written at load. A running fn cannot store to it. `@mut` on `config` is a compile error. |

`@mut` and a duration mark compose. `static @mut` is a mutable static. `ephemeral` without `@mut` can be read once and is then empty.

## Where `@` sits

Attributes sit immediately after the fn or the binding, before `=` or `{`. Arguments use `[]`.

```text
fn tone in[t] out[here] @latex_internal @sig_gen_graph_preview {
fn burst in[] out[] @parallel[4] {
i32 tape[4] @memory_hard = {1, 2, 3, 4};
```

The attribute list is also the effect list: it is the complete set of what that fn or binding is allowed to do. `@require` and `@ensure` are contracts in that list, not a second syntax.

Comptime is in: a fn may run at compile time when every input is a constant.

Traits are not in. Add a specific trait only when it has a name and a job. No open-ended typeclass system yet.

## Reference capabilities

Not a Rust borrow checker. The capability is composed onto the binding.

- `@val` is a fixed cell (vault-shaped). Writes are rejected.
- `@ref` is the ordinary mutable cell, and still needs `@mut` to store.
- `@xor` is light shared-xor-mutable for that binding only. Not a whole-program lifetime analysis.
- `@mutex` is the explicit lock when a binding is shared and written.
- The object frees itself when its duration ends. Manual dealloc is not the default.

## Includes

Libraries are components with their own dependencies. The compiler gathers every include in the project into one set, then checks that set the way `yay` checks a transaction: dedupe, order by dependency, do not paste the same file twice.

A cycle means "already loaded", not an error and not a second copy.

No explicit export list in this pass. No ban on top-level statements in this pass.

## Parallelism

All four, chosen by the effect list. The set of backends must be able to grow.

| mark | meaning |
|---|---|
| `@parallel[n]` | Explicit CPU width. No surprise fork. |
| pure fn, no `@mut` | May fork like Bend. |
| data-parallel array op | One op over the whole array. This is the batch, not a loop of calls. |
| `await` | Host wait. The caller is not blocked. |

The scheduler reads that composition and tries GPU or CPU. Explicit `@parallel[n]` stays a CPU width. It does not silently become a GPU launch.

## Also in

- Multiple dispatch on nominal types. Structural dispatch only through a flow map.
- Macros. They may not invent a `()` call form or a backwards assignment.

## Out

- Lazy evaluation. Nothing runs until a fn is called.
- Hindley-Milner inference. It fights mandatory declarations.
- Rust-style borrow checking.
- A trait system with no named traits yet.
