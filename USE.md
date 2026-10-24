# Use guide

Future language. The Halloween runner does not implement this yet. If a mark is absent, the behavior in the **default** line is what you get.

`()` groups. It never calls. `[]` is the call, the index, the `if`, the `do`, the attribute argument, the `hold`, the `window`, the `gather`, the `next`, and the `leave`.

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
| no `@topology` | A chain. One statement, then the next. |
| no `@stream` | The fn runs to one result. |
| `@stream` with no `n` | A queue of one item. The producer waits when that slot is full. |
| no `@paged` | The whole value is present. |
| an absent lane | Skipped. It is not zero, and it is not a second syntax. |
| a missing `else` or match arm | No-op. Not a compile error. |
| no `@structuredConcurrency` | A failed run does not cancel its siblings. The run may outlive the caller. |
| no `@retry` | A failure walks. It is not tried again. |
| a wait under 15 minutes | The coroutine stack stays in live memory. |

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

A handler written on the call is the context for that invocation. It runs only if the fn did not already resolve the failure.

```text
add in[4] onfail { leave; };
```

## Failure

A failure is a state. It is not an exception channel and not an ignored code. It walks until some `onfail` resolves it. There is no `try` mark and no `?`.

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
| `onfail` on the fn | Resolves the failure inside the fn. Omitted: the state is handed outward. |
| `onfail` on the call | The next handler, if the fn did not resolve it. |
| `STOP[THIS]` | Halt this failure now. `THIS` is the failure in hand. |
| `onwarn` | A warning state. Does not halt, and does not replace `onfail`. Omitted: no warning path. |
| `@retry[k]` | Run the fn again, up to k times, then the failure walks. Omitted: no retry. |

There is no `guard` keyword. `if[state]` inside the body is an ordinary transition, not a guard.

## Cleanup

Duration frees the cell. `hold` unlocks. These two also exist, and they are not the same as `onfail`.

```text
defer { print[1]; }
faildefer { print[2]; }
```

`defer` runs on `leave`, on `return`, and on failure. `faildefer` runs only when a failure leaves the block. On a failure both run. On success only `defer` runs. Several of them run in reverse order of writing, before the failure walks to the caller. `onfail` is still the handler that resolves the state. It is not a cleanup block.

## Branches

`if[cond]` tests an open condition. A missing `else` is a no-op. Nothing warns about it, because there is no closed set of conditions to finish.

`match` is only for a closed set: the states of a nominal value, or an enum. A missing arm is a no-op. It is not a compile error, and arms do not fall through. A project warning may say that an arm is missing. That warning is off unless the project turns it on. This is the same split as C++: an `if` without `else` is silent, and `-Wswitch` warns on an enum that does not name every enumerator. It does not reject the program.

A threshold is an `if`, not a `match`.

```text
if[n < 10] { small; }
if[n < 100] { mid; }
match[color] {
  red { print[1]; }
  blue { print[2]; }
}
```

If `color` is `green` and there is no `green` arm, the `match` does nothing.

## Control flow

Default shape is a chain. `do n[k]` runs the body k times. The index is `i`, from 0 up to k - 1. An inner `do` shadows `i`.

```text
do n[4] {
  if[i == 1] { continue; }
  print[i];
}
loop @name[outer] {
  do n[4] {
    if[done] { leave[outer]; }
  }
}
```

`continue` starts the next iteration of the innermost `do` or `loop`. `leave` ends the innermost one. `leave[name]` ends the loop or `do` that carries `@name[name]`. Neither one is `STOP[THIS]`.

`loop` does not end on its own. A failure still walks outward, and it leaves the loop on the way.

`@topology` names a shape that is not a chain. Omitted, the fn is a chain.

| mark | what the runs do |
|---|---|
| `@topology[fanout]` | One value, n runs. The join is still explicit. |
| `@topology[fanin]` | The next statement waits until every run has finished, or one has failed. |
| `@topology[barrier]` | The runs wait until all have arrived, then continue. |
| `@topology[race]` | The first present result wins. The other arms of this race are cancelled. |
| `@topology[chain]` | The default. |

`select` is `race` over different calls, not n copies of one fn. The first call that becomes present wins. The other calls in that `select` are cancelled.

```text
select {
  next[steps in[]];
  tick in[];
}
```

A pipeline is not a keyword. It is `@stream[n]` between two fns, plus `next` or `select`.

A jump is a state transition or a failure walk. `goto`, `longjmp`, `throw`, and a callback registry are not primitives.

## When one run of many stops

Default: Erlang plus a detached run. If one run fails, its siblings keep going. The failed run's failure walks on its own. The run may still be going after the caller returns. That is the fire-and-forget case.

`@structuredConcurrency` is the other rule, and it is opt-in. One failure cancels the siblings, their holds end, and the run may not outlive the caller. `@NoFailTolerance` is not a second mark. It means this one.

`@topology[race]` and `select` still cancel the arms that lost, because that is what a race is. They do not cancel unrelated siblings.

## Coroutines

A coroutine is stackful. `pause` keeps the stack. `resume` continues it. The caller can do either by hand.

If a wait lasts 15 minutes or more, the stack may leave live memory and sit on disk or on other memory until `resume`. The host picks where. The language only requires that the stack is not stuck in live RAM for that wait. A shorter wait stays in live memory.

An accumulator is still how several paused calls join. `await` remains the host-wait state. It does not allocate a second, stackless, kind of coroutine.

## Large cells

Default: the whole value is present, and a present lane is dense. An absent lane is not a zero and it is not visited. That absent lane is the mask. There is no `mask` keyword and no warp vote or shuffle in the language. A GPU backend may lower an absent lane to a hardware predicate. That lowering is not source syntax.

```text
i32 span @ref = window[buf, start, n];
i32 picked = gather in[table, ids];
i32 sector @paged @mut;
hold[sector] {
  sector[0] = 1;
}
```

| form | behavior |
|---|---|
| `window[buf, start, n]` | A `@ref` over n elements. No copy. Read-only unless the window's name also says `@mut`. Freed with the enclosing fn. |
| `@paged` | Only the sector named by the current `hold` is present. A read of another sector is a failure. |
| `gather in[table, ids]` | Copies the present lanes named by `ids`, in that order. Absent lanes are skipped. |
| a parallel op over an array | Skips absent lanes. |

A store through a window is a store through `@ref`. It needs `@mut`, and `@mutex` or `@atomic` if the cell is shared.

## Streams

```text
fn steps in[] out[] @stream[4] {
  return x;
}
i32 item = next[steps in[]];
```

`@stream[n]` runs ahead into a queue of n items. When n items are waiting unread, the producer waits. `next` takes one. Omitted n means 1. Mentioning the fn does not by itself push. The queue does not grow past n.

`leave` inside the stream ends it. The following `next` is absent.

This is not lazy evaluation. The bound is the only run-ahead, and it is written.

## What a large run does not get for free

A reduction is still a named `out[]`, an `@atomic` store, or a `@mutex` span. A sum, a score, and a top-k are fns you write. The language does not insert them.

There is no gradient and no tape of operations. `@checkpoint` on a fn saves the present cells, and only at a barrier or at the end of a `hold`. Omitted: nothing is saved. The language does not choose a file format.

## Attributes

Attributes sit immediately after the fn or the binding, before `=` or `{`. Arguments use `[]`. The list is the whole effect list.

```text
fn burst in[] out[] @parallel[4] @structuredConcurrency @retry[3] {
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
| `@parallel[n]` | `n` CPU runs. This does not become a GPU launch. Siblings are not cancelled when one fails. |
| `@structuredConcurrency` | Those n runs cancel on a failure, and they may not outlive the caller. |
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
- a read of a sector that is not the held page
- using an absent lane as zero
- two runs storing one cell with no `@mutex`, no `@atomic`, and no explicit `out[]`
- a runtime condition inside an attribute macro
- `leave[name]` when no loop carries that name
- a second mark `@NoFailTolerance` beside `@structuredConcurrency`

## Not in the language

Lazy evaluation. A fn runs when called, unless it says `@comptime` and every input is a constant. A stream runs ahead only up to its written bound.

No inferred types. No borrow checker. No trait system. No `guard` keyword. No `unlock`. No `goto`, `longjmp`, `throw`, or callback registry. No default reduction. No unwrap. No gradient and no operation tape. No warp vote or shuffle. No requirement that every `match` arm be written. No C `switch` fall-through. Attributes do not select which fn body runs.
