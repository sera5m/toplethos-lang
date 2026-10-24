# Use guide

This revision is the language. [MATRIX.md](MATRIX.md) is older and does not win where they differ. The Halloween runner does not implement this yet.

`()` groups. It never calls and it never takes an attribute argument. `[]` does.

An ordinary variable is mutable. You do not write `@mut` to get one. `@mut` is that ordinary case, written out. It changes nothing, and it is not a scope.

## Defaults

| if you write nothing else | what happens |
|---|---|
| a declared name | A new mutable cell. Private to the function. Not a pointer and not a reference. Freed when the function returns. |
| `i32 y = 1` or `i32 y; y = 1` | Both store. Later stores are also legal. |
| `@val` or `@config` | A later store is rejected. |
| no visibility mark on a variable | `@private`. Hidden outside the function that created it. |
| no visibility mark on a fn | `@public`. An include can call it. |
| no `@comptime` and no `@compeval` | The fn runs when called. |
| no `@parallel`, `@gpuparallel`, `@npuparallel` | One CPU run. The scheduler does not pick a device. |
| no `@structuredConcurrency` | A failed sibling keeps going. The run may outlive the caller. |
| `@topology[fanin]` or `[barrier]` | Wait for every participant. Not for the first. A failure does not cancel that wait. |
| no `@stream` | The fn runs to one result. It is not a producer. |
| no `@total`, and the project switch is off | A missing `else` or match arm is a no-op. |
| an absent cell | The name has no value. A read is a failure. This is not an array, and not a math matrix. |
| `@ephemeral` used twice | Compile error. One read or one mov, then the cell is wiped. |
| `@NoFailTolerance` | Compile error. It is not a mode. |

## Names, pointers, references

```text
i32 y;
y = 1;
y = 2;
i32 x = 1;
```

`2 = x` is a parse error. `@mut` is optional on an ordinary cell. `@mut` together with `@val` or `@config` is a compile error, because those two refuse a later store.

These three are not the same word.

| form | what it is |
|---|---|
| unmarked, or `@mut` | The cell itself. Passed by value: the callee gets a copy. Stores hit that copy. |
| `i32*` | A pointer. `var*`. It is an address. It may be absent. `&x` aims it. `*p` is the cell. `*p = 1` stores into that cell. |
| `@ref` | A reference to anything: a cell, a function, or a sector. It does not point. It is not an address, you cannot reseat it, and you cannot turn it into a pointer. You may pass it and, when it refers to a cell, you may read that cell. You may not store through it. |
| `@xor` | Rust-like ownership, for security. One owner. Handing the cell to a new `@xor` name ends the previous one. While it is held, no other name may read or store that cell. A pointer to it is a compile error. A `@ref` does not take this ownership. |
| `@val` | The cell is fixed after the first store. Not a lifetime. |

```text
i32 x = 1;
i32* p = &x;
*p = 2;
fn bump in[i32* a] out[] {
  *a = *a + 1;
}
bump in[&x];
```

`i32* q @ref = x` is a compile error. A reference is not a pointer.

```text
i32 key @xor = secret;
i32 moved @xor = key;
```

After that handoff, `key` is ended. `moved` is the only owner.

## Visibility

This is the scope that `@mut` is not. A variable is created in a function and hidden outside it, unless a visibility mark says otherwise. There is no namespace and no `@global`. There is no class, so `@protected` is the file, not a derived class.

| mark | who can name it |
|---|---|
| `@private` | Only the function that created it. Default for a variable. |
| `@protected` | Any function in the same file. |
| `@public` | Any file that included this one. |

`@static` does not publish the name. Inside a function it is a C++ static: one cell, for the life of the program, still `@private` unless you say otherwise. The cell is created on the first call and is absent before that.

```text
fn counter in[] out[] {
  i32 n @static @public = 0;
  n = n + 1;
  return n;
}
```

After `counter` has run, other files can name `n`. Two `@public` variables with the same name are a compile error.

A function defaults to `@public`. `@private` on a function hides it from other files. `@protected` keeps it in this file.

## Scope

These say when the cell is freed. They are not visibility, and they are not `@mut`.

| mark | when it is freed |
|---|---|
| unmarked, `@normal` | When the enclosing fn returns. |
| `@ephemeral` | After its one read or its one mov. The cell is wiped. A second use is a compile error. If the compiler cannot prove there is one use, that is also a compile error. |
| `@static` | Not freed for the life of the program. Hidden unless visibility says otherwise. |
| `@config` | Not freed, and not writable after the first store. |

`@scoped`, `@local`, and bare duration words are not spellings. `@static` on a fn is not a duration. Duration sits on a cell.

Passing an `@ephemeral` is that one use. The caller is done with it. What the callee does with its own parameter is the callee's marks, not a second rule.

`@config` and `@val` both refuse a later store. They are not the same: `@config` is a lifetime, `@val` is a freeze on a cell that still dies with its scope.

## States

A cell is **absent**, **present**, or **empty**.

| state | meaning |
|---|---|
| absent | No value yet. `i32 y;` starts here. A lane in an array can be absent too. Same word, same rule. |
| present | A store has happened. |
| empty | An `@ephemeral` cell after its one use. |

`if[state[y]]` looks at the state. It does not read the value. A read of the value while absent or empty is a failure. For `@ephemeral`, a second use does not get that far: it is a compile error.

## Mutex

`@mutex` means this cell is the thing being guarded. `lock` and `unlock` are calls. This is not `@xor`. A mutex is shared and locked. `xor` has one owner and no unlock.

```text
i32 tape[4] @static @mutex = {1, 2, 3, 4};
lock[tape];
tape[0] = tape[0] + 1;
unlock[tape];
```

A store to a `@mutex` cell outside `lock` / `unlock` is a compile error. Every path that locks must unlock. `hold` is sugar for that pair, including on failure:

```text
hold[tape] {
  tape[0] = tape[0] + 1;
}
```

Leaving a block, the order is fixed.

| how you leave | order |
|---|---|
| success | `defer`, then `unlock`. |
| failure | `faildefer`, then `defer`, then `unlock`, then `onfail`. |

Cleanup still sees the cell locked. `onfail` runs after the unlock. Several `defer` or `faildefer` blocks run in reverse order of writing.

## Devices

| mark | device |
|---|---|
| `@parallel[n]` | n CPU threads. Not a GPU launch. |
| `@gpuparallel` | A GPU launch. The geometry is the device's. There is no thread count. |
| `@npuparallel` | An NPU launch. Same rule. The geometry is the device's. |

The scheduler does not choose among these. You write the one you mean.

## Failure

A failure walks until an `onfail` resolves it. No `try`. No `?`.

```text
fn boom in[] out[] @require[n > 0] @retry[3] {
  return 1;
} onfail {
  print[1];
} onwarn {
  print[2];
};
add in[4] onfail { leave; };
```

| form | effect |
|---|---|
| `@require[…]` | Entry guard. False is a failure. Omitted: no entry guard. |
| `@ensure[…]` | Exit guard. Omitted: no exit guard. |
| `onfail` on the fn | Resolves it here, unless it already left this fn. |
| `onfail` on the call | Used when the fn did not resolve it. |
| `@retry[k]` | Run again, up to k times, then the failure walks. Omitted: no retry. |
| `STOP[THIS]` | Halt this failure. It can be conditional, inside `onfail`. |
| `fatal` | Not recoverable. `onfail` does not run. The host does an emergency shutdown. |
| `onwarn` | Does not halt and does not replace `onfail`. |

`@NoFailTolerance` is a compile error wherever it is written. The opt-in for "a failure cancels the siblings" is only `@structuredConcurrency`.

## Branches

A missing `else`, and a missing `match` arm, are no-ops. Arms do not fall through. A threshold is an `if`.

```text
if[n < 10] { small; }
match[color] {
  red { print[1]; }
}
```

If `color` is `blue`, nothing in the `match` runs. `@total` on that `match`, or the project switch, makes a missing arm a compile error. Off, unless you turn it on.

## Loops

`do n[k]` runs k times. `i` is 0, 1, …, k - 1. That is `i < k`, not `i <= k`. `i` is read-only. The loop stores it. `i = 3` is a compile error. There is no form for walking `i` backward.

```text
do n[4] {
  if[i == 1] { continue; }
  print[i];
} onFinished {
  print[9];
}
```

`onFinished` runs once, after the last iteration. An inner `do` has its own `i`.

`continue` starts the next iteration of the innermost `do` or `loop`. `leave` ends the innermost one. `leave[name]` ends the `loop` or `do` that says `@name[name]`. That `@name` is allowed on a `loop` or a `do`. It is not only for fns and bindings.

```text
loop @name[outer] {
  do n[4] {
    if[done] { leave[outer]; }
  }
}
```

`leave` is not `STOP[THIS]` and not `fatal`.

## Fans, races, streams

| mark | waits for |
|---|---|
| `@topology[fanin]` | Every participant, then one continuation. |
| `@topology[barrier]` | Every participant, then they all continue. |
| `@topology[race]`, `select` | The first present result. The other arms of that race are cancelled. |
| `@topology[fanout]` | Does not wait by itself. The join is still explicit. |
| `@topology[chain]` | The default. One statement, then the next. |

"Any result" is `race` or `select`. It is not what `fanin` or `barrier` mean. Under the default, a failure does not cancel the remaining participants and does not end the wait. `@structuredConcurrency` is what makes one failure cancel the siblings and end the wait.

```text
select {
  next[steps in[]];
  tick in[];
}
```

The `@stream` fn is the producer. Calling it starts it.

```text
fn steps in[] out[] @stream[4] {
  return x;
}
steps in[];
i32 item = next[steps in[]];
```

It may run ahead until n items are waiting. Then it waits. Omitted n means 1. `next` is the consumer. `leave` inside the producer ends it, and the next `next` is absent. A pipeline is this queue between two fns. It is not its own keyword.

A detached run must not keep a pointer to a cell that dies when the caller returns. That is a compile error. A `@public @static` cell is how a value survives the function and stays nameable.

## Coroutines

`initialize` starts a stackful coroutine. `create` does not. `create` allocates a new cell at run time, a different cell each time the statement runs. A declaration allocates one cell for the scope.

```text
initialize steps in[];
resume[steps];
```

`pause` inside the coroutine keeps the stack. `resume` continues it. `await` is a host wait on that same stack. It is not a second, stackless, kind of coroutine.

If a wait has already lasted 15 minutes, the stack may leave live memory. The host picks disk or other memory. A shorter wait stays in live memory.

## Large cells

An absent lane is skipped. It is not zero. That is the only mask. No warp vote or shuffle in the language.

```text
i32* span = window[buf, start, n];
i32 picked = gather in[table, ids];
i32 sector @paged @mutex;
hold[sector] {
  sector[0] = 1;
}
```

| form | behavior |
|---|---|
| `window[buf, start, n]` | A pointer over n elements. No copy. |
| `@paged` | Only the sector named by the current lock is present. |
| `gather in[table, ids]` | Copies the present lanes named by `ids`, in order. |
| `@dataparallel` on a fn | One call applies the body per lane. Absent lanes are skipped. The result is a new array, not a store into a shared cell. |
| `@accum` on an out cell | Many runs may add into that cell. The add is the join. |
| `@memory_hard` | The cell is a file. The location is fixed at program start. |

```text
fn lane in[i32 a] out[] @dataparallel {
  return a + 1;
}
i32 sum @accum = 0;
```

A window is a pointer because it is a span of addresses. A `@ref` to a sector does not give those addresses.

## Flow

`Hz` and `Pin` do not match because both store an integer. No conversion is implicit.

```text
flow rgb565 to vec4 {
  r = r;
  g = g;
  b = b;
}
vec4 color = formatconvert in[src] out[here];
```

`flow` is the map `^=` consults. A field not named on the right is dropped. No `flow` means not structurally equal. `out[here]` fills a declared name. `out[new]` is the same job as `create` for that result. `@ignore_if_structure_matches` on a fn lets a parameter accept a `^=` source without `formatconvert`. Omitted: it does not.

## Attributes

`@` sits after a fn, after a binding, or after `loop` and `do` when the mark is `@name`. Arguments use `[]`. An unknown `@` is a compile error.

```text
macro cell[where] {
  @val
  when[where ^= ephemeral] { @ephemeral }
}
i32 key @cell[ephemeral] = secret;
```

`when` is comptime, inside the macro only. A false `when` adds nothing. `@comptime` on a fn means it may run at compile time when every input is a constant, like `constexpr`. `@compeval` means it must. A non-constant input to `@compeval` is a compile error, like `consteval`.

## Include and comments

```text
include "lib.tpth_s";
```

Loaded once. A cycle means already loaded. No export list. Visibility is what hides a name. An include does not override `@private`.

`//` to the end of the line is a comment, including a graph marker. A marker is not code.

## Compile errors

- `expr = name`
- `name()` as a call
- `@mut` with `@val`, or `@mut` on `@config`
- a later store to `@val` or `@config`
- a store through a `@ref`
- turning a `@ref` into a pointer
- a pointer to an `@xor` cell, or a second reader of one
- `@static` used as a duration on a fn
- a second use of `@ephemeral`, or a use the compiler cannot prove is the only one
- a store to a `@mutex` cell outside `lock` and `unlock`
- a path that locks and does not unlock
- writing `i` in a `do`
- `leave[name]` with no matching `@name`
- `@NoFailTolerance`
- an unknown `@`
- `@ptr` as a mark. The spelling is `i32*`
- a detached pointer to a cell that dies with the caller
- two `@public` variables with the same name
- `@compeval` with an input that is not a constant
- a missing match arm when `@total` or the project switch is on

## Not in the language

No inferred types. No borrow checker. No trait system. No class and no inheritance. No namespace and no `@global`. No `guard`. No `goto`, `longjmp`, `throw`, or callback registry. No default reduction. No unwrap. No gradient. No warp syntax. No C `switch` fall-through. No silent GPU launch. `@mut` is not a scope and not required. A missing arm is not an error unless `@total` or the project switch says so.
