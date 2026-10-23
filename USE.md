# Use guide

Future language. The Halloween runner does not implement this yet. If a mark is absent, the behavior in the **default** line is what you get.

`()` groups. It never calls. `[]` is the call, the index, the `if`, the `do`, the attribute argument, and the `hold`.

## Defaults

| if you write nothing else | what happens |
|---|---|
| a declared name | Owns its cell. Not an alias. Immutable. Freed when the enclosing fn returns. |
| `i32 y;` | The cell starts **absent**. A read is a failure. |
| `i32 y = 1;` | The cell starts **present**. That `=` is the initial store, and it is allowed once. |
| a later `y = 2` | Rejected. The name was not `@mut`. |
| a call with no `out[]` | Results are discarded when the call is a statement. When the call is the right-hand side of `=`, the result is that value. |
| a fn with no `return` | The call yields nothing. Reading that result is absent. |
| no `onfail` | The failure walks to the caller. |
| no `onfail` on the whole walk | Halt. Same as `STOP[THIS]`. |
| no `@require` / `@ensure` | No guard. |
| no `@parallel` | The fn does not fork. |
| a shared store with no resolution | The program is wrong. There is no default join. |
| no `@comptime` | The fn runs when it is called, even if every input is a constant. |
| `include` | Loaded once. A cycle means already loaded. |

## Names

```text
i32 y;
i32 x = 1;
x = 2;
```

Declare before assign. `2 = x` is a parse error.

| mark | default role | store |
|---|---|---|
| unmarked | Owns the cell. Not an alias. | Only the initial `=` on the declaration. |
| `@mut` | Same ownership. Write permission. Not a lifetime. | Later stores are legal. |
| `@val` | Owns the cell. Fixed after the initial store. | Never again. `@mut @val` is a compile error. |
| `@ref` | Names a cell that already exists. No copy. | Only if this name also says `@mut`. |
| `@xor` | One live name. Giving the cell to a new name ends this one. | Only with `@mut`. |

`@mut` is not a kind of name. It is the permission to store, composed onto an owned cell or onto a `@ref`.

An unmarked `i32 key = 1` is not writable because `@mut` was left off. Adding `@mut` later is legal. `i32 key @val = 1` stays fixed even if `@mut` is added. That is the whole difference.

`i32 b @ref = key` does not copy `key`. A store through `b` writes `key`, and only if `b` says `@mut`.

## States

A cell is **absent**, **present**, or **empty**. A read is legal only while it is present. `if[state]` is the transition. There is no unwrap.

| state | how you get there |
|---|---|
| absent | `i32 y;` or `@absent` on a declaration. Also the result of a fn that did not `return`. |
| present | The initial `=`, or a later store through `@mut`. |
| empty | `@ephemeral` after its one read. The machine empties it. That empty is not a user store and does not need `@mut`. |

## Duration

Duration is an attribute. It does not grant `@mut`.

| mark | when the cell is freed |
|---|---|
| unmarked, `@normal` | When the enclosing fn returns. `@scoped` and `@local` are the same mark. |
| `@ephemeral` | After one read. Then the cell is empty. |
| `@static` | Not freed for the life of the program. |
| `@config` | Not freed, and not writable after load. `@mut` here is a compile error. |

## Sharing a store

Default: the cell is not shared. Two runs storing one cell with none of the marks below is an error. There is no join order to fall back on.

| mark | resolution |
|---|---|
| `@mutex` | A store is legal only inside `hold`. |
| `@atomic` | Each store locks and unlocks itself. No span across two stores. |
| `@volatile` | Two reads are not the same value. A register may not keep it. Does not make the store safe. |
| accumulator, or an explicit `out[]` | The join is a named result, not a second name for the same cell. |

```text
i32 tape[4] @static @mut @mutex = {1, 2, 3, 4};

hold[tape] {
  tape[0] = tape[0] + 1;
}
```

Entering `hold[tape]` locks. The end of the block unlocks. There is no `unlock`. A store outside the block is a compile error. If a failure leaves the block, the hold ends before the failure walks outward. A nested `hold` of the same cell is a compile error. Two different cells lock in the order the blocks are written. Opposite orders in two runs can deadlock. The language does not invent a lock order.

## Calls

```text
fn add in[i32 a] out[] {
  return a + 1;
}
i32 n = add in[4];
add in[4];
```

`add(4)` is a parse error. `()` only groups, as in `a * (b + c)`.

A parameter is a binding. Its attributes sit after the name, inside the brackets.

```text
fn bump in[i32 a @mut] out[] {
  a = a + 1;
}
```

Default for a parameter: owned, immutable, `@normal`, present for the call. The caller's argument is copied in unless the parameter says `@ref`.

Several fns may share a name. The call is chosen by the nominal types of the parameters. Attributes do not select a second body.

## Failure

A failure is a state. It is not an exception channel and not an ignored code.

```text
fn boom in[] out[] @require[n > 0] {
  return 1;
} onfail {
  if[fatal] { STOP[THIS]; }
} onwarn {
  print[1];
};
```

| form | default effect |
|---|---|
| `@require[…]` | Entry guard. A false predicate is the failure state. Omitted: no entry guard. |
| `@ensure[…]` | Exit guard. Same failure state, after the body. Omitted: no exit guard. |
| `onfail` | Resolves the failure here. Omitted: the state is handed to the caller. |
| `STOP[THIS]` | Halt this failure now. `THIS` is the failure in hand. |
| `onwarn` | A warning state. Does not halt, and does not replace `onfail`. Omitted: no warning path. |

There is no `guard` keyword. `if[state]` inside the body is an ordinary transition, not a guard.

## Attributes

Attributes sit immediately after the fn or the binding, before `=` or `{`. Arguments use `[]`. The list is the whole effect list.

```text
fn burst in[] out[] @parallel[4] @require[n > 0] {
i32 key @ephemeral @val = secret;
```

A long list can be a macro. The use site stays one `@name`. The body holds only attributes and `when[cond] { … }`. `when` is comptime. A false `when` adds nothing. It is not a runtime branch.

```text
macro cell[where] {
  @val
  when[where ^= static] { @static }
  when[where ^= ephemeral] { @ephemeral }
}
i32 key @cell[ephemeral] = secret;
```

## Parallel

| mark | default |
|---|---|
| omitted | No fork. One run. |
| `@parallel[n]` | `n` CPU runs. This does not become a GPU launch. |
| `await` | Host wait is a state. The caller is not blocked. Omitted: the call is synchronous. |

`@atomic` covers one store. `@mutex` plus `hold` covers a span. An explicit `out[]` names the joined result.

## Types

`i32`, `f32`, `bool`, and `string` are representations. A newtype is nominal: `Hz` and `Pin` do not match because both store an integer.

Default conversion: none. `^=` is true only when a flow map says the layouts correspond, including a different field order or a partial field set. No flow map means not structurally equal.

```text
vec4 color = formatconvert in[src] out[here];
vec4 color = formatconvert in[src] out[new];
```

`out[here]` fills the declared binding. `out[new]` allocates it. `@ignore_if_structure_matches` on a fn lets a parameter accept a `^=` source without `formatconvert`. Omitted: it does not.

## Include

```text
include "lib.tpth_s";
```

Every include in the project is one set. It is deduped and ordered by dependency. A cycle means already loaded, not a second copy and not an error. There is no export list. A library's names are visible.

## Comments

`//` to the end of the line is a comment. A graph marker is a comment:

```text
//<<<node id=1 name=add>>>
//<<<link from=1 to=2>>>
```

The marker is not code. A macro does not expand it.

## Compile errors

These are rejected, not run.

- `expr = name`
- `name()` as a call
- a store to a name without `@mut`, after the initial `=`
- `@mut` with `@val`
- `@mut` on `@config`
- a store to a `@mutex` cell outside `hold`
- a nested `hold` of the same cell
- a read of an absent or empty cell
- two runs storing one cell with no `@mutex`, no `@atomic`, and no explicit `out[]`
- a runtime condition inside an attribute macro

## Not in the language

Lazy evaluation. A fn runs when called, unless it says `@comptime` and every input is a constant.

No inferred types. No borrow checker. No trait system. No `guard` keyword. No `unlock`. No default reduction order. No unwrap. Attributes do not select which fn body runs.
