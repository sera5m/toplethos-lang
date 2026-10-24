# Use guide

This revision is the language. [MATRIX.md](MATRIX.md) is older and does not win where they differ. The Halloween runner does not implement this yet.

`()` groups. It never calls and it never takes an attribute argument. `[]` does.

`$` is structural equality and nothing else. It is not a variable sigil. `^` is free. `goto` is not in this version. Later, if it exists, it takes a pointer to a program position, not a text label.

## What a variable is

A variable is one cell. Every mark on it belongs to one of four groups. A mark does not do the job of another group.

| group | question | marks |
|---|---|---|
| scope | Who can name it? | `@private` `@protected` `@public` |
| time | When does the cell exist? | unmarked `@ephemeral` `@static` `@config` |
| type and value | What are the bits, and how do you name them? | `i32` `f32` `bool` `string` `object` `T*` `@val` `@xor` |
| geometry | Where do the bytes live, and where does a fn run? | `@paged` `@memory_hard` `@parallel[n]` `@gpuparallel` `@npuparallel` |

Ordinary cell: mutable, `@private`, freed when the function returns, on the CPU. `@mut` is that ordinary cell written out. It changes nothing. It is not a scope.

### Scope

| mark | who can name it |
|---|---|
| `@private` | Only the function that created it. Default for a variable. |
| `@protected` | Any function in the same file. Not inheritance. There is no class. |
| `@public` | Any file that included this one. |

A function defaults to `@public`. `@private` hides it from other files. There is no namespace and no `@global`. Two `@public` variables with the same name are a compile error.

### Time

| mark | when it is freed | store |
|---|---|---|
| unmarked | When the function returns. | Any number of stores. |
| `@ephemeral` | After one read or one mov. Then wiped. | A second use is a compile error. |
| `@static` | Life of the program. Created on the first call. Absent before that. | Stores are legal. Still hidden unless visibility says otherwise. C++ function-local static. |
| `@config` | Life of the program. | First store only. |

`@val` is not a time. It freezes the cell after the first store, and the cell still dies with its time mark. `@config` is the time mark that also refuses a later store.

States sit on the cell: **absent** (no value), **present** (a store has happened), **empty** (an ephemeral after its one use). `if[state[name]]` looks at the state and does not read the value. A read of an absent or empty value is a failure.

### Type and value

You name a cell in four ways. There is no fifth.

| spelling | meaning |
|---|---|
| `val` | The value. Passing it copies. |
| `&val` | The address of that cell. |
| `val*` | A pointer. It may be absent. `*p` is the cell it aims at. `*p = 1` stores there. |
| `inst.val` | A field of an object instance. |

```text
i32 x = 1;
i32* p = &x;
*p = 2;
fn bump in[i32* a] out[] {
  *a = *a + 1;
}
bump in[&x];
```

`@xor` is ownership for security, not a way to spell a reference. One owner. Handing it to a new `@xor` name ends the previous name. While it is held, no other name may read or store it, and a pointer to it is a compile error.

### Geometry

Geometry is not a type. On a function it chooses the device. On a large cell it chooses where the bytes live.

| mark | where |
|---|---|
| unmarked | CPU. One run. |
| `@parallel[n]` | n CPU threads. Not a GPU launch. |
| `@gpuparallel` | GPU. The geometry is the device's. No thread count. |
| `@npuparallel` | NPU. Same rule. |
| `@paged` | Only the locked sector is present. |
| `@memory_hard` | A file. The location is fixed at program start. |

The scheduler does not pick a device. You write the mark.

## Objects

An object is a C++ standard-layout struct in memory. Fields are in the order written, with that target's C++ alignment. A C++ translation unit can read the same bytes. There is no base class, no subclass, no virtual, and no vptr.

```text
object Point {
  i32 x;
  i32 y;
}
Point p;
p.x = 1;
Point* q = &p;
```

`p` passed as `Point` is a copy. `Point*` is the shared instance. A function that takes `Point*` is the operation. The object does not carry methods, because a method list becomes a subclass table. FFI is the bytes, not C++ name mangling.

## Flow

`Hz` and `Pin` do not match because both store an integer. Nothing converts implicitly.

```text
flow rgb565 to vec4 {
  r = r;
  g = g;
  b = b;
}
```

`a $= b` is true only when a `flow` says the layouts correspond. A field not named is dropped. No `flow` means not structurally equal, even when the field names look the same. `!$=` is the opposite.

`formatconvert in[src] out[here]` applies one flow. `out[here]` fills a declared name. `out[new]` allocates, the same job as `create`. The runner does not do this yet. The rule is only the spelling above: one direction, the fields you name, no guessed conversion.

`@ignore_if_structure_matches` on a function lets a parameter accept a `$=` source without `formatconvert`. Omitted: it does not.

## Defaults that are not a group

| if you write nothing else | what happens |
|---|---|
| `i32 y = 1` or `i32 y; y = 1` | Both store. Later stores are legal unless the cell is `@val` or `@config`. |
| no `@comptime` and no `@compeval` | The function runs when called. |
| no `@structuredConcurrency` | A failed sibling keeps going. The run may outlive the caller. |
| `@topology[fanin]` or `[barrier]` | Wait for every participant. A failure does not cancel that wait. |
| `@topology[race]` or `select` | The first present result. The other arms of that race are cancelled. |
| no `@stream` | The function runs to one result. |
| `@stream` with no n | A queue of one. The producer waits when it is full. |
| a missing `else` or match arm | No-op, unless `@total` or the project switch is on. |
| `@NoFailTolerance` | Compile error. It is not a mode. |

`@comptime` may run at compile time when every input is a constant. `@compeval` must. A non-constant input to `@compeval` is a compile error.

## Shared store

`@mutex` guards a cell. `lock` and `unlock` are the calls. `@xor` is not a mutex: a mutex is shared, and xor has one owner.

```text
i32 tape[4] @static @mutex = {1, 2, 3, 4};
lock[tape];
tape[0] = tape[0] + 1;
unlock[tape];
hold[tape] {
  tape[0] = tape[0] + 1;
}
```

`hold` is `lock` plus `unlock` on every exit. A store outside the pair is a compile error. Every path that locks must unlock.

| how you leave | order |
|---|---|
| success | `defer`, then `unlock`. |
| failure | `faildefer`, then `defer`, then `unlock`, then `onfail`. |

`@accum` on a cell means many runs may add into it. The add is the join. No other reduction is implied.

## Failure

A failure walks until `onfail` resolves it.

| form | effect |
|---|---|
| `@require[…]` | Entry guard. False is a failure. |
| `@ensure[…]` | Exit guard. |
| `onfail` on the function | Resolves it here. |
| `onfail` on the call | Used when the function did not. |
| `@retry[k]` | Try again, up to k times, then the failure walks. |
| `STOP[THIS]` | Halt this failure. It may sit inside `onfail`. |
| `fatal` | Skip `onfail`. The host shuts down. |
| `onwarn` | Does not halt. |

`@structuredConcurrency` is the only mark that cancels siblings when one fails. A detached run must not keep a pointer to a cell that dies with the caller.

## Keywords

| keyword | what it does |
|---|---|
| `fn` | Names a function. The body runs when the function is called. |
| `in[ ]` | The parameters. A value is copied. A pointer is not. |
| `out[ ]` | The results. Empty means none. |
| `return` | Yields a value and leaves the function. No `return` yields nothing. |
| `include` | Loads a file once. A cycle means already loaded. Does not reveal `@private` names. |
| `object` | Declares a layout. No base clause is legal. |
| `flow` | Declares the only map `$=` will accept. |
| `macro` | A bundle of attributes. The body is attributes and `when` only. |
| `when` | Comptime, inside a macro. A false `when` adds nothing. |
| `create` | Allocates a new cell each time the statement runs. |
| `initialize` | Starts a stackful coroutine. Does not allocate a variable. |
| `if` | Runs the body when the condition is present and true. A missing `else` is a no-op. |
| `match` | One arm of a closed set. A missing arm is a no-op. Arms do not fall through. |
| `do n[k]` | Runs the body k times. `i` is 0 through k - 1, read-only. `i < k`, not `i <= k`. |
| `onFinished` | Runs once after that `do` finishes. |
| `loop` | Runs until `leave` or a failure. |
| `continue` | Starts the next iteration of the innermost `do` or `loop`. |
| `leave` | Ends the innermost `do` or `loop`. `leave[name]` ends the one with `@name[name]`. Not `fatal`. |
| `select` | Runs the arms until one is present. Cancels the other arms of this `select`. |
| `defer` | Runs on every exit from the block, before `unlock`. |
| `faildefer` | Runs only when a failure leaves the block, before `defer`. |
| `onfail` | Resolves the failure in hand. |
| `onwarn` | A warning path. The function continues. |
| `STOP[THIS]` | Halts the failure in hand. |
| `fatal` | Emergency shutdown. `onfail` does not run. |
| `lock` `unlock` | The mutex pair. |
| `hold` | That pair around a block. |
| `pause` | Keeps the coroutine stack. |
| `resume` | Continues a coroutine. |
| `await` | A host wait on that same stack. Not a second kind of coroutine. |
| `next` | Takes one item from a stream queue. |
| `window` | A pointer over n elements. No copy. |
| `gather` | Copies the present lanes named by the ids, in that order. Absent lanes are skipped, not stored as zero. |
| `formatconvert` | Applies a `flow`. |

`@stream[n]` on a function makes that function the producer. Calling it starts it. It may run ahead until n items are unread, then it waits. `leave` inside it ends it.

`@dataparallel` on a function applies the body once per lane. The result is a new array. Different lengths are a failure. Absent lanes are skipped.

`@checkpoint` saves the present cells at a barrier or at the end of a `hold`. Omitted: nothing is saved.

`@name` is allowed on `loop` and `do`. `@total` on a `match` makes a missing arm a compile error.

If a wait has already lasted 15 minutes, a coroutine stack may leave live memory. The host picks disk or other memory.

An unknown `@` is a compile error. `@` sits after a function, after a binding, or after `loop` and `do` when the mark is `@name`.

## Compile errors

- `expr = name`
- `name()` as a call
- a later store to `@val` or `@config`
- `@mut` written on `@val` or `@config`
- a pointer to an `@xor` cell, or a second reader of one
- a second use of `@ephemeral`
- a store to a `@mutex` cell outside `lock` and `unlock`
- a path that locks and does not unlock
- writing `i` in a `do`
- `leave[name]` with no matching `@name`
- `@NoFailTolerance`
- `@ptr` or `@ref` as a mark
- `^=` as structural equality. The operator is `$=`
- a base clause on an `object`
- an unknown `@`
- a detached pointer to a cell that dies with the caller
- two `@public` variables with the same name
- `@compeval` with an input that is not a constant
- `@dataparallel` arrays of different lengths
- a missing match arm when `@total` or the project switch is on

## Not in the language

No subclasses. No virtuals. No namespaces. No inferred types. No borrow checker. No traits. No `goto` in this version. No `guard`. No default reduction besides the add on `@accum`. No gradient. No warp syntax. No silent device choice. A missing arm is not an error unless `@total` or the project switch says so.
