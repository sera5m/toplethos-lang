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
| Immutable unless `@mut` | An unmarked binding cannot be written. `@mut` is what allows a later store. This is not a lifetime. |
| Option | No missing-value hole. Absence is `Option`. |
| Result | No exceptions. A failure is an explicit `Result`, handled by `onfail` / `onwarn` below. |
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

## Two axes that are not both called static

`static` was doing two jobs. It does not anymore.

| axis | words | means |
|---|---|---|
| Write | unmarked, or `@mut` | Unmarked cannot be stored to. `@mut` can. Says nothing about when the cell is freed. |
| Deallocate | `ephemeral`, `normal`, `scoped`, `static`, `config` | When the cell is freed. Says nothing about whether stores are legal. |

`static` means only "do not deallocate for the life of the program". A `static` binding is still immutable unless it also says `@mut`. `@mut` on a `normal` binding does not keep it alive.

| mark | when it is freed |
|---|---|
| `ephemeral` | After one use inside a call, the cell is emptied. For a password, not for general scratch. |
| `normal` | Unmarked duration. Freed with the enclosing fn. `normaldur` is the same word. |
| `scoped` | Freed when this fn returns. `local` is the same word. |
| `static` | Not freed for the life of the program. |
| `config` | Not freed for the life of the program, and not writable after load. `@mut` on `config` is a compile error. |

`config` is the one duration that also refuses writes. Every other duration composes with `@mut` freely. `static @mut` lives for the program and can be stored to. `static` without `@mut` lives for the program and cannot.

## Failure is explicit

No exceptions. The fn names its failure handler next to the body. The failure value is a `Result`, not a hidden channel.

```text
fn foo in[] out[] {
  body
} onfail {
  if[fatal] { STOP[THIS]; }
} onwarn {
  stuff
};
```

| form | what it does |
|---|---|
| no `onfail` | Halt. Same as `STOP[THIS]`. |
| `onfail { … }` | Graceful path. The failure does not halt unless this block says so. |
| `STOP[THIS]` | Halt this failure. Used inside `onfail` when the stop is conditional. |
| `onwarn { … }` | Warning path. Does not halt. |

`STOP[THIS]` is the default halt, not a second error type. `THIS` is the failure in hand.

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

- `@val` is a fixed cell (vault-shaped). Writes are rejected. This is immutability, not `static`.
- `@ref` is the ordinary cell, and still needs `@mut` to store.
- `@xor` is light shared-xor-mutable for that binding only. Not a whole-program lifetime analysis.
- `@mutex` is the explicit lock when a binding is shared and written.
- The object frees itself when its duration ends. Manual dealloc is not the default. `static` is the mark that says not to.

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
- Using `static` to mean immutable.
