# Feature matrix

Superseded by [USE.md](USE.md). Where this file disagrees with that one, USE.md wins. Kept as the older decision log only.

Later reversals live only in USE.md. `^=` in this log is the old structural test. 1.0 uses `$=` for that and `^=` to resolve and store. An ordinary cell is mutable. A result type is written in `out[]`.

Decisions from 2026-10-23. This is the target language, not what `toplethos_run.py` implements today. `STANDARD.md` stays the current runner until these land.

Already locked, before this matrix:

- `()` is math grouping only. Calls are `name in[…]` and `name in[…] out[…]`.
- Assignment is `name = expr`. `expr = name` is a parse error.
- `[]` stays for `if`, `do n`, calls, and indexing.

A running program is a state machine. Failure, absence, and a parallel join are states of that machine. They are not three extra control-flow forms. An undesirable state has to be resolved by composition. Leaving it unresolved is the error.

## Types

| decision | rule |
|---|---|
| Declare before assign | `i32 y; y = expr;`. A name on the left of `=` must already exist. |
| Immutable unless `@mut` | An unmarked binding cannot be written. `@mut` is what allows a later store. This is not a lifetime. |
| Absence | No missing-value hole and no unwrap syntax. Present and absent are states of the binding. The value is readable only in the present state. `if[state]` is the transition. |
| Failure | No exceptions. A failure is a state, not a hidden channel. Handled as below. |
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

## Failure walks outward

C++ does not halt inside the function that fails. A thrown exception walks the callers. `std::terminate` runs only when the whole walk has no handler. An ignored error code does not halt at all. Toplethos follows the exception walk, not the ignored code and not a halt inside the callee.

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
| `onfail` on this fn | Resolves the failure state here. |
| no `onfail` here | The failure state is handed to the caller. It does not halt in this fn. |
| no `onfail` on the whole walk | Halt. Same as `STOP[THIS]`. |
| `STOP[THIS]` | Halt this failure now. The explicit terminate, used when the stop is conditional. |
| `onwarn` | A warning state. Does not halt, and does not replace `onfail`. |

`THIS` is the failure in hand. `STOP[THIS]` is that halt state, not a second error type.

## Guards are already on the fn

There is no `guard` keyword. The function form already has the two halves.

| piece | job |
|---|---|
| `@require[…]` | Entry guard. A failed predicate is the failure state. |
| `@ensure[…]` | Exit guard. Same failure state, checked after the body. |
| `onfail` | Where that state is resolved. Not a second guard. |
| `when[…]` | Comptime only, inside an attribute macro. It does not test a runtime value. |

`if[state]` inside the body is an ordinary transition, not a guard. A guard is a predicate on entry or exit that produces the failure state. `onfail` is what runs when one fires.

## Parallel states are resolved by composition

There is no default join order. A fork is not "pure, so it is fine." Whether it is fine depends on the effects composed onto the bindings it touches.

An undesirable parallel state is two runs storing the same cell with nothing to resolve the store. Resolve it by composition, or the program is wrong:

| resolution | what it does |
|---|---|
| `@mutex` | One run holds the cell. |
| atomic | The store itself is the resolution. |
| accumulator | Many runs add into one declared out cell. |
| explicit `out[]` or an output flow | The join is a named result, not a race on a shared binding. |

Same machine as failure. Running, joined, and failed are states. An unresolved one is the error.

## Where `@` sits

Attributes sit immediately after the fn or the binding, before `=` or `{`. Arguments use `[]`.

```text
fn tone in[t] out[here] @latex_internal @sig_gen_graph_preview {
fn burst in[] out[] @parallel[4] @require[n > 0] {
i32 tape[4] @memory_hard = {1, 2, 3, 4};
```

The attribute list is also the effect list: it is the complete set of what that fn or binding is allowed to do. `@require` and `@ensure` are the guards in that list, not a second syntax.

Comptime is in: a fn may run at compile time when every input is a constant.

Traits are not in. Add a specific trait only when it has a name and a job. No open-ended typeclass system yet.

## Attribute macros

A long composition lives in one macro. The use site stays a single `@name` in the attribute position.

```text
macro cell[where] {
  @val
  when[where ^= static] { @static }
  when[where ^= ephemeral] { @ephemeral }
}

i32 key @cell[ephemeral] = secret;
fn burst in[] out[] @cell[static] @parallel[4] {
```

`@cell[ephemeral]` expands in place, then composition continues left to right. For that example the effect list is `@val @ephemeral` on the binding, and `@val @static @parallel[4]` on the fn.

Rules:

- The body of this macro is only attributes and `when[cond] { attributes }`. No statements, no `onfail`, no second call syntax.
- `when` is comptime. The condition is a constant or a `^=` on the macro argument. The effect list is known before the fn runs.
- A `when` that is false contributes nothing. It is not a runtime branch.
- The macro does not hide a different position. Expansion still sits where the `@name` was written, after the fn or the binding.

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

The backend is chosen by the effect list. The set of backends must be able to grow. A fork is legal only when every shared store is resolved by the table above.

| mark | meaning |
|---|---|
| `@parallel[n]` | Explicit CPU width. No surprise fork. Does not silently become a GPU launch. |
| data-parallel array op | One op over the whole array. The join is an explicit `out[]`, an accumulator, or an atomic. Not a loop of calls. |
| `await` | Host wait is a state. The caller is not blocked. |

The scheduler reads that composition and tries GPU or CPU.

## Also in

- Multiple dispatch on nominal types. Structural dispatch only through a flow map.
- Macros. Attribute macros are the form above. A general macro may not invent a `()` call form or a backwards assignment.

## Out

- Lazy evaluation. Nothing runs until a fn is called.
- Hindley-Milner inference. It fights mandatory declarations.
- Rust-style borrow checking.
- A trait system with no named traits yet.
- Using `static` to mean immutable.
- A runtime condition inside an attribute macro. That would make the effect list depend on a value that does not exist yet.
- A default reduction order for parallel joins.
- Halting inside the fn that failed when a caller still has `onfail`.
- Unwrapping an absent value. Absence is a state, not a type with a match form.
- A `guard` keyword. Entry is `@require`, exit is `@ensure`, and `onfail` already receives the failure.
