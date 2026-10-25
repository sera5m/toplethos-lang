# Use guide

This is the language spec. [MATRIX.md](MATRIX.md) is older and does not win where they differ. The runner does not implement this yet.

`()` groups. It never calls and it never takes an attribute argument. `[]` does.

`//` through the end of the line is a comment. A graph marker is a comment. It is not code.

`$` is structural equality and nothing else. It is not a variable sigil. `^` is free. `goto` is not in this version. Later, if it exists, it takes a pointer to a program position, not a text label.

## What a variable is

A variable is one cell. Every mark on it belongs to one of four groups. A mark does not do the job of another group.

| group | question | marks |
|---|---|---|
| scope | Who can name it? | `@private` `@protected` `@public` |
| time | When does the cell exist? | unmarked `@ephemeral` `@static` `@config` |
| type and value | What are the bits, and how do you name them? | `i8` `i16` `i32` `i64` `u8` `u16` `u32` `u64` `size` `f32` `f64` `bool` `string` `object` `T*` `fn in[] out[T]` `fnid` `@val` `@xor` |
| geometry | Where do the bytes live, and where does a fn run? | `@paged` `@memory_hard` `@parallel[n]` `@gpuparallel` `@npuparallel` |

Ordinary cell: mutable, `@private`, freed when the function returns, on the CPU. `@mut` on an ordinary cell changes nothing. It is not a scope. A function instance is not an ordinary cell. Its default is frozen. See Functions.

### Scope

| mark | who can name it |
|---|---|
| `@private` | Only the function that created it. Default for a variable. |
| `@protected` | Any function in the same file. On an object field, also a subclass. |
| `@public` | Any file that included this one. |

A function definition defaults to `@public`. `@private` hides it from other files. There is no `@global`. Two `@public` variables with the same short name in one namespace are a compile error.

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
| `inst.val` | A field or method of an instance. On a pointer, `p.val` is `(*p).val` when `p` is present. An absent pointer is a failure. |

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

A function's type is `fn in[i32] out[i32]` when it returns an `i32`, and `fn in[i32] out[]` when it returns nothing. It is never written as `i32 name(i32)`.

`f32` is a C `float`. `f64` is a C `double`. A foreign library may use either.

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

The scheduler does not pick a device. You write the mark. A call through an instance uses the instance's geometry when a transform has set one, and the definition's geometry otherwise.

## Objects
An object is a struct in memory. `struct` is not a second keyword. Fields are in the order written, with that target's C++ alignment. There is no virtual function and no vptr. `@ismutationof` does not make a subclass. A `flow` converts two layouts that are not parent and child.

The short name of an object is one name inside its namespace. A second object with that short name in the same namespace, or a function with that short name there, is a compile error. `Draw::Point` and `Math::Point` are two objects.

One parent is allowed. The mark is `@childof`, and it names that one object.

```text
object Point {
  i32 x;
  i32 y;
  fn move in[i32 dx, i32 dy] out[] {
    HERE.x = HERE.x + dx;
    HERE.y = HERE.y + dy;
  }
}
object Colored @childof[Point] {
  i32 rgb;
  fn move in[i32 dx, i32 dy] out[] @ismutationof[getfuncidbyname["Point.move"]] {
    HERE.x = HERE.x + dx;
    HERE.rgb = 0;
  }
}
Colored c;
c.x = 1;
c.rgb = 2;
Point* parent = as[Point, &c];
Colored* back = as[Colored, parent];
```

The parent is a prefix at offset 0. The child's own fields follow it. `c.x` names the parent's field. The parent is written the way you name that object, so another namespace is `@childof[Draw::Point]`. A second `@childof`, a cycle, or a parent that is not an object is a compile error. A child must not declare a field name the parent already has.

`as[Parent, &child]` is a pointer to that prefix. It is the only upcast. `as[Child, parentPtr]` is the downcast. It is legal only when `Child` is that parent or a descendant, and it is not checked at run time, because there is no vptr. `as` of a value copies the parent prefix and nothing else. A downcast of a value is a compile error: the child fields are not there. Passing a `Colored` where a `Point` is required does not convert. A function that takes `Point*` does not accept `Colored*` until the caller writes `as`.

There is still composition. An embedded field is not a parent.

```text
object Line {
  Point a;
  Point b;
}
Line ink;
ink.a.x = 1;
```

`ink.a.x` is the embedded field. `ink.x` is not. A `Line` is not a `Point`, and `as[Point, &ink]` is a compile error.

An object must not embed itself by value. A pointer field may aim at the same object type.

```text
object Node {
  i32 v;
  Node* next;
}
```

Copying an object copies the parent prefix, each embedded field, and each pointer as an address. It does not follow the pointer. The instance's time mark covers the prefix and the fields. A field cannot carry `@ephemeral`, `@static`, or `@config`. A `@val` instance freezes every field. A `@val` field freezes that field even when the instance is mutable.

Who may name the variable is the variable's scope. A field with no mark can be named by anyone who can name the instance. `@private` on a field is the file that defined the object, and a subclass cannot name it. `@protected` on a field is that file and any subclass. The bytes stay in the layout either way.

This layout matches a C struct written as the parent's fields, then the child's fields. It matches C++ only for a class with no virtual function, no second base, and no vptr. Do not describe it as a C++ class hierarchy and expect the compiler to invent the same bytes.

A function that takes `Point*` is still an operation. A method is that function written inside the object. It is not a field and it is not stored in the instance, so the layout does not grow a vptr.

```text
Point p;
p.move in[1, 2];
Point* q = &p;
q.move in[1, 2];
```

`HERE` inside a method is the instance. Stores go into that instance. A parameter or local with the same name is the bare name. `HERE.x` is still the field. The function's full name is `Point.move`, so another object may also have a method named `move`. A bare `fn move` and a method `move` are different names.

A child does not override. It may declare a mutation of the parent method. The parent method stays what it is, and a `Point*` call still runs `Point.move` even when the pointer aims at a child.

```text
Colored c;
c.move in[1, 2];
Point* parent = as[Point, &c];
parent.move in[1, 2];
```

`c.move` is `Colored.move` because the static type is `Colored`. `parent.move` is `Point.move`. Nothing is chosen at run time. If a child has no method of that short name, the search walks the parent chain and uses the first one it finds. That walk is fixed when compiling.

A child method may reuse the parent's short name only with `@ismutationof` of that parent method. Without the mark, the reused name is a compile error. A mutation with a different short name does not hide the parent method.

A method defaults to `@public`. `@private` is the file that defined it, and a subclass cannot call it or mutate it. `@protected` can be called and mutated by a subclass. A `foreign` method names its C symbol in the mark, `foreign[Point_move]`, because `Point.move` is not a C symbol.


## Namespaces

A namespace is a name prefix. It is not an object, not a value, and not a parent. It holds functions, objects, and variables. Those are its components. An object cannot hold a namespace, because a namespace is not a field and it has no bytes.

`::` is the namespace mark. `.` stays an instance field or a method.

```text
namespace Draw {
  object Point {
    i32 x;
    i32 y;
  }
  fn identity in[i32 a] out[i32] {
    return a;
  }
}
Draw::Point p;
p.x = 1;
Draw::identity in[1];
```

Inside `Draw`, the short names `Point` and `identity` are visible. Outside it, the short name is not. The qualified name always works. There is no `using` and no argument-dependent lookup. Both would make a short name ambiguous.

The same namespace may be opened again, in this file or another. The members are added to the one namespace. A repeated short name is still a compile error.

Namespaces nest. `Draw::Color::pack` is one function. Its id is that qualified name, so `getfuncidbyname["Draw::Point.move"]` is the method. The same short name in another namespace is a different function.

A `@public` member of another file is still named `Draw::identity`. `@private` stays in the file that defined it. `include` loads the file once and does not copy the other file's short names into this namespace.

`Draw` cannot be stored, passed, or used with `as`. `Draw::Point` is the object. `Draw` itself is only the prefix.

A variable written in a namespace, and not inside a function, lives for the program. It is not created on the first call. With no scope mark it is `@private` to the file that contains it. `@static` there changes nothing. `@static` inside a function is still the one created on the first call.

## Functions

A definition is code, not a cell. You cannot store into it.

```text
fn add in[i32 a] out[] {
  return a + 1;
}
```

This is a compile error. The message is: function "add" on line 1 has no designated type out[] in function definition and cannot return this statement.

```text
fn add in[i32 a] out[i32] {
  return a + 1;
}
```

`out[i32]` is the result type. You write it. It is not inferred. `out[]` means there is no result. A path out of an `out[T]` function with no `return` is a compile error. A result of a different type is a compile error.

Scope and geometry on that line apply to the definition. Time does not: the definition lives for the program. The type is `fn in[i32] out[i32]`. One short name is one function inside its namespace. A second definition with that short name in the same namespace is a compile error. Attributes do not choose a body.

A mutation is a different function with its own id and its own name. It is not an overload. The call is the name you wrote. The relationship is the parent's id, never the parent's name.

```text
fn add in[i32 a] out[i32] {
  return a + 1;
}
fn add_f in[f64 a] out[f64] @ismutationof[getfuncidbyname["add"]] {
  return a + 1.0;
}
```

`fnid` is the id of a definition. It is a value. It is not a pointer, not an instance, and not callable. An instance does not get its own id. Transforming an instance does not change the id or the name.

```text
fnid slot @static = getfuncidbyname["add"];
string who = getnamebyfuncid[slot];
fn(in[i32] out[i32]) f = getfuncbyid[slot];
f in[1];
```

`getfuncidbyname` runs at compile time. The string is the name you would call: `add`, or `Draw::Point.move`. A short name that is not exactly one function in that namespace is a compile error. The result is a `fnid`.

`getnamebyfuncid` returns the qualified name. A constant id is resolved at compile time. An id read from a cell, including a `@static` cell, is resolved when that call runs. One id has one name.

`getfuncbyid` returns that definition as a frozen function value. You may call it. `instance` of it makes a cell. It is not a `@mut` instance. A missing id is a compile error when the id is a constant, and a failure when the id comes from a cell.

`@ismutationof[id]` stores that id. A missing id is a compile error. A cycle of mutations is a compile error. A mutation of a mutation is allowed.

The mark does not change the call and it does not change the C symbol. `add_f` is its own foreign name if it is `foreign`. The C side does not see `@ismutationof`.

C++ equivalency is the type, the address, and the call. It is not the declarator syntax.

| C++ idea | here |
|---|---|
| function type | `fn in[i32] out[i32]` |
| address of the code | `&add` |
| call | `add in[4]` |
| overload | None. A different signature is another function. `@ismutationof` only records the lineage. |
| `std::function` / bind | `instance` and `bind` |

`&add` aims at the definition. It is not a cell.

An instance is the cell. Scope, time, `@val`, and `@xor` apply to it.

```text
fn(in[i32] out[i32]) mine = instance[add];
mine in[4];
fn(in[i32] out[i32])* slot = &mine;
```

The default instance is frozen. You may call it. You may not rewrite it. `@mut` on that instance is the condition that unlocks `transform`. `@mut` on ordinary data still does not rewrite code.

```text
fn(in[i32] out[i32]) mine @mut = instance[add];
transform[mine] @geometry {
  @gpuparallel
}
transform[mine] @body {
  return a + 2;
}
```

| operation | new cell or the same one | needs `@mut` |
|---|---|---|
| `compose in[outer, inner]` | New instance. `compose in[outer, inner] in[x]` is `outer in[inner in[x]]`. | no |
| `bind in[f, v]` | New instance. `v` fills the first free parameter. `bind in[f, n, v]` fills parameter n. A value is copied. | no |
| `transform[mine] @geometry` | Same instance. Device only. | yes |
| `transform[mine] @body` | Same instance. Body only. The `in` and `out` types stay. | yes |

A transform that names no group is a compile error. One transform may name `@body` and `@geometry` together. That is the higher-dimensional edit this version allows: one change across those axes, not a new axis language. Scope and time are not rewritten in place. A different scope or time is a new instance.

A transform is itself a function from an instance to an instance, so later it can be composed and bound like any other function. That is as far as the higher-dimensional goal goes here. No extra dimension type is added.

A call that has started keeps the body and geometry it started with. A transform does not change the definition, and it does not change any other instance. A `@compeval` definition cannot be made into a `@mut` instance. Binding a pointer to a cell that dies before the instance dies is the same detached-pointer error as anywhere else.

## HERE, THIS, and another function

These are three different things. A result being stored is not a context, and a failure is not a function.

| word | what it is |
|---|---|
| `HERE` | The context of the function that is running. `HERE.n` is that function's own cell `n`. |
| `THIS` | The failure in `onfail`, or the instance in `transform`. A failure has no payload. `THIS` is not a value and it has no fields. |
| `context[other]` | The published cells of another function. `@public` cells, plus `@protected` cells when the reader is in the same file. `@private` cells are not in it. |

```text
fn counter in[] out[i32] {
  i32 n @static @public = 0;
  HERE.n = HERE.n + 1;
  return HERE.n;
}
i32 seen = context[counter].n;
```

`STOP[THIS]` halts the failure in hand. A call used on the right of `=` stores into that name. `out[new]` still allocates.

You do not walk another function's stack to find a local. If it is not published, `context` does not have it.

## out[here]

`out[here]` is the chain. It is not the name on the left of `=`, and it is not `HERE`. The result type is the `out` type of the function taken as input. It is still designated. It is not inferred from the body.

This function returns a value to the function it took as an input. That input function then runs. What it returns is what this function returns. The next layer is that input. `HERE` does not switch to the callee.

```text
fn last in[i32 x] out[i32] {
  return x + 1;
}
fn layer in[fn(in[i32] out[i32]) next] out[here] {
  return 1;
}
layer in[last];
```

`return 1` on a function marked `out[here]` means `next in[1]`. `last` returns 2, and that is what `layer` returns.

`out[here] = v` does the same delivery and does not leave the function. The expression's value is what the input function returns.

If several parameters are functions, `out[here]` is the first. `out[here next]` names one. No function parameter is a compile error.

The callee may read `context` of this function only for published cells. A function that does not say `out[here]` returns only to its caller.

## Pipe and pool

These are the Oiria pipe and the Oiria pool, without a subclass for each kind. A pool is the memory. A pipe only moves bytes. The lock lives inside the pool. You do not add a second `@mutex` to it.

```text
pool buf = pool[4096];
hold[buf] @read { }
hold[buf] @write { }
hold[buf] @exclusive { }
```

| mode | who may be inside |
|---|---|
| `@read` | Many readers. No writer. |
| `@write` | One writer. Readers may be inside. |
| `@exclusive` | One holder. Nobody else. |

`hold` acquires on entry and releases on every exit, including failure. A store without a write or exclusive hold is a compile error. A read without a hold is a compile error.

`@ring` on a pool makes it a byte queue. `push` and `pop` on a pool use that queue. Full is a failure. Empty `pop` is absent.

A pipe connects functions or pools. Its kind and its shape are marks, not types.

| mark | Oiria kind | behavior |
|---|---|---|
| `@process` | InterProcess | A queue. Time is not critical. `pop` does not block. Default depth 16. |
| `@device` | InterDevice | A batch of 1 to 255 items. `flush` sends the batch and a CRC. |
| `@buffer` | InterBuffer | Passes a pointer. The pointer is present only during that `push`. Storing it is a compile error. |

| mark | who it connects |
|---|---|
| `@direct` | One source, one target. Default. |
| `@fan` | One source, many targets. |
| `@clone` | One source, many targets, the same bytes to each. |
| `@merge` | Many sources, one target. |
| `@cfan` | Fan, and each target may drop the item. |
| `@cmerge` | Merge, and each source may drop the item. |

```text
pipe link @process @direct;
push[link, item];
pop[link];
```

`push` is a failure when the pipe is full or the item is dropped. A filter on a pipe may only find, swap, replace, or remove bytes. It may not call a function. No filter means the bytes are copied unchanged. Unit conversion stays at `formatconvert`, not inside the pipe.

## Link table

Pointer chasing across functions is not required. Each function definition has an id. The key of two ids is the pair, lower id first, then the other. That key does not collide with a different pair.

```text
links[add, mine]
```

The record has two sides:

| side | what it holds |
|---|---|
| `add` | Pointers `add` holds that point into `mine`. |
| `mine` | Pointers `mine` holds that point into `add`. |

One lookup returns both directions. It does not follow those pointers onward. The compiler fills the record when one function's instance stores a pointer into the other's published cell, or when `bind` captures such a pointer. A private cell never appears in the record.

Calculus stays ordinary functions on values. This table is the feature that removes the chase. There is no derivative operator.


## Foreign libraries

`include "file.tpth_s"` loads Toplethos. It does not parse C or C++. A header is not a Toplethos file.

`include lib` names a host library and the C symbols Toplethos may call. A C++ library is reached only through `extern "C"` in that library. Overloaded C++ names, templates, and methods stay on the C++ side of the shim. An object crosses as the layout in Objects: parent prefix, then fields, no vptr.

```text
include lib "m" {
  fn sin in[f64 x] out[f64] foreign;
}
sin in[1.0];
```

The library is loaded once. A second `include lib "m"` means already loaded. A missing library or a missing symbol fails at that include, not at the first call. The host binds it, either on the link line or with `dlopen`.

A value argument is copied. A pointer is the address, and it must stay valid for the call. The foreign function must not keep the pointer unless that cell outlives the call.

A bare `foreign` uses the function's short name as the C symbol. That is legal only when the short name is one C identifier. A qualified name, and a method, must write the symbol: `foreign[Point_move]`. `foreign` has no Toplethos body. `transform` of its `@body` is a compile error. The call runs on the CPU in the host. `@gpuparallel` does not retarget it. If the C function starts its own device work, that stays inside the C function.


## Entry

The host calls `main`.

```text
fn main in[] out[i32] {
  return 0;
}
```

There is one `main`. It takes nothing. The result type is `i32`, and you write it. `0` is a normal exit. A failure that leaves `main` halts. It is not returned as `0`.

## Values

An atom is one of `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `size`, `f32`, `f64`. `size` is the target's unsigned pointer width. `bool` is `true` or `false`. A string is `"text"`. `int` and `long` are not names: their width is not fixed.

`+` is the functor `addmagnitude`. The compiler unwraps the call to one definite row. There is no value that stays polymorphic at run time.

The compiler's record of a cell that holds an atom is `valueprimitive { ptr_to_elem, size, atom }`. It is not a cell the program allocates. A struct is not an atom. A field is, when the field's type is an atom. `v.x += 1` is `v.x = v.x + 1` for that field's atom.

| inputs | row | result |
|---|---|---|
| the same integer atom twice | Integer add. The bits wrap. | That atom. |
| the same float atom twice | IEEE 754 add. | That atom. |
| an integer and a float, either order | Convert the integer to that float, then float add. | That float. |
| two integers of different width | No row. | Compile error. |
| two floats of different width | No row. | Compile error. |
| anything else | No row, until you add one. | Compile error. |

`-`, `*`, and `/` are `submagnitude`, `mulmagnitude`, and `divmagnitude`. They use the same row rules. `/` by zero is a failure.

`==`, `!=`, `<`, `>`, `<=`, and `>=` result in `bool`. They accept two of the same atom, or an integer and a float under the same conversion as `+`. Pointer `==` and `!=` compare the address. An ordered compare of pointers is a compile error. `bool` and `string` have no arithmetic row.

A new row is compile time. Its types are definite atoms. The body may call a row that already exists. It may not call the functor and hit itself.

```text
row[addmagnitude] in[i16 a, i32 b] out[i32] {
  return addmagnitude in[as[i32, a], b];
}
```

`as[i32, a]` on atoms is the `convert` functor. Built-in rows widen an integer to a wider integer of the same signedness, convert an integer to `f32` or `f64`, and widen `f32` to `f64`. A narrowing `as` has no built-in row.

The C++ environment maps fixed-width names only.

| C++ | here |
|---|---|
| `int8_t` `int16_t` `int32_t` `int64_t` | `i8` `i16` `i32` `i64` |
| `uint8_t` `uint16_t` `uint32_t` `uint64_t` | `u8` `u16` `u32` `u64` |
| `float` `double` | `f32` `f64` |
| `size_t` | `size` |
| `bool` | `bool` |

## Flow

`Hz` and `Pin` do not match because both store an integer. Nothing converts implicitly.

```text
flow rgb565 to vec4 {
  r = r;
  g = g;
  b = b;
}
vec4 color = formatconvert in[src];
```

`a $= b` is true only when a `flow` says the layouts correspond. A field not named is dropped. No `flow` means not structurally equal, even when the field names look the same. `!$=` is the opposite.

`formatconvert` returns the converted value. The `=` stores it. `out[new]` allocates, the same job as `create`. The runner does not do this yet. The rule is one direction, the fields you name, no guessed conversion.

`@ignore_if_structure_matches` on a function lets a parameter accept a `$=` source without `formatconvert`. Omitted: it does not.

## Defaults that are not a group

| if you write nothing else | what happens |
|---|---|
| `i32 y = 1` or `i32 y; y = 1` | Both store. Later stores are legal unless the cell is `@val` or `@config`. |
| a function instance | Frozen. Callable. Not transformable until `@mut`. |
| no `@comptime` and no `@compeval` | The function runs when called. |
| no `@structuredConcurrency` | A failed sibling keeps going. The run may outlive the caller. |
| `@topology[fanin]` or `[barrier]` | Wait for every participant. A failure does not cancel that wait. |
| `@topology[race]` or `select` | The first present result. The other arms of that race are cancelled. |
| no `@stream` | The function runs to one result. |
| `@stream` with no n | A queue of one. The producer waits when it is full. |
| a missing `else` or match arm | No-op, unless `@total` or the project switch `total` is on. |
| `@NoFailTolerance` | Compile error. It is not a mode. |
| a pipe with no kind | `@process` and `@direct`. |
| a pool hold with no mode | `@read`. |

`@comptime` may run at compile time when every input is a constant. `@compeval` must. A non-constant input to `@compeval` is a compile error.

## Shared store

`@mutex` guards a cell that is not a pool. `lock` and `unlock` are the calls. `@xor` is not a mutex: a mutex is shared, and xor has one owner. A pool uses `hold` and its access mode instead.

```text
i32 tape[4] @static @mutex = {1, 2, 3, 4};
lock[tape];
tape[0] = tape[0] + 1;
unlock[tape];
hold[tape] {
  tape[0] = tape[0] + 1;
}
```

`hold` on a `@mutex` cell is `lock` plus `unlock` on every exit. A store outside the pair is a compile error. Every path that locks must unlock.

| how you leave | order |
|---|---|
| success | `defer`, then `unlock`. A pool `hold` releases in that `unlock` step. |
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
| `STOP[THIS]` | Halt the failure in hand. `THIS` is that failure. |
| `fatal` | Skip `onfail`. The host shuts down. |
| `onwarn` | Does not halt. |

A failure has no payload. `STOP[THIS]` ends it. It does not produce a value, and it does not become `0`. If the failure leaves `main` with nothing stopping it, the host halts.

`@structuredConcurrency` is the only mark that cancels siblings when one fails. A detached run must not keep a pointer to a cell that dies with the caller.

## Keywords

| keyword | what it does |
|---|---|
| `fn` | Names one definition in its namespace. The body runs when it is called. Also spells the type: `fn in[i32] out[i32]`. |
| `in[ ]` | The parameters. A value is copied. A pointer is not. |
| `out[ ]` | The designated result. `out[]` means none. `out[i32]` means an `i32`. `out[here]` uses the input function's `out` type. |
| `return` | Yields the designated result and leaves. `return` with a value in `out[]` is a compile error. |
| `HERE` | The running function's own context. |
| `THIS` | The value in hand. Not a context. |
| `context` | Published cells of another function. |
| `links` | The two-way pointer record for one pair of function ids. |
| `instance` | Makes a cell from a definition. Frozen unless the cell says `@mut`. |
| `compose` | New instance. Inner runs first, then outer. |
| `bind` | New instance with one parameter filled. |
| `transform` | Rewrites `@body` or `@geometry` of a `@mut` instance. Does not touch the definition. |
| `@ismutationof[id]` | Links this function to another function's id. Not an overload and not a call. |
| `getfuncidbyname` | Compile time. The call name becomes a `fnid`. |
| `getnamebyfuncid` | The qualified name of that id. |
| `getfuncbyid` | The frozen function value of that id. |
| `pool` | Shared memory. The access mode is its lock. |
| `pipe` | Moves items. Does not own their storage. |
| `push` `pop` `flush` | Queue operations. `flush` finishes a `@device` batch. |
| `include` | Loads a `.tpth_s` file once. A cycle means already loaded. Does not reveal `@private` names. `include lib` binds a host library's C symbols. |
| `namespace` | A C++-style prefix. It may be opened again. Not a value. No `using`. |
| `object` | Declares one layout. `@childof[Parent]` is the one parent. An embedded object field is composition, not a parent. |
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
| `lock` `unlock` | The mutex pair, for a cell that is not a pool. |
| `hold` | That pair around a block, or a pool access of the named mode. |
| `pause` | Keeps the coroutine stack. |
| `resume` | Continues a coroutine. |
| `await` | A host wait on that same stack. Not a second kind of coroutine. |
| `next` | Takes one item from a stream queue. |
| `window` | A pointer over n elements. No copy. |
| `gather` | Copies the present lanes named by the ids, in that order. Absent lanes are skipped, not stored as zero. |
| `formatconvert` | Applies a `flow`. The result is a value. |
| `as` | Parent or child pointer, or an atom conversion that has a `convert` row. |
| method | A `fn` inside an object. Called as `inst.move in[...]`. Not a field. |

`@stream[n]` on a function makes that function the producer. Calling it starts it. It may run ahead until n items are unread, then it waits. `leave` inside it ends it. A stream is not a pool and not a pipe.

`@dataparallel` on a function applies the body once per lane. The result is a new array. Different lengths are a failure. Absent lanes are skipped.

`@checkpoint` saves the present cells at a barrier or at the end of a `hold`. Omitted: nothing is saved.

`@name` is allowed on `loop` and `do`. `@total` on a `match` makes a missing arm a compile error.

If a wait has already lasted 15 minutes, a coroutine stack may leave live memory. The host picks disk or other memory.

An unknown `@` is a compile error. `@` sits after a function, after a binding, or after `loop` and `do` when the mark is `@name`.

## Compile errors

- `return` of a value when `out[]` has no designated type
- a result whose type is not the designated `out` type
- a path out of `out[T]` with no `return`
- a second `main`, or a program with no `main`
- `+` with no row, or a narrowing `as` with no row
- `/` is not a compile error when the divisor is zero. It is a failure
- `expr = name`
- `name()` as a call
- `i32 name(i32)` as a function type
- a second function with the same short name in the same namespace
- `@ismutationof` a missing id, a name that is not one function, or a mutation cycle
- `getfuncidbyname` of a name that is not one function
- `getnamebyfuncid` or `getfuncbyid` of a constant id that does not exist
- `out[here]` when the function takes no function, or the named parameter is not a function
- using `HERE` as another function, or `THIS` as a context
- `context` reading a `@private` cell
- a store into a function definition
- `transform` on an instance that is not `@mut`
- `transform` that names no group, or names `@scope` or `@time`
- a `@body` transform that changes `in[]` or `out[]`
- a `@mut` instance of a `@compeval` definition
- a later store to `@val` or `@config`
- `@mut` written on `@val` or `@config`
- a pointer to an `@xor` cell, or a second reader of one
- a second use of `@ephemeral`
- a store to a `@mutex` cell outside `lock` and `unlock`
- a path that locks and does not unlock
- a pool read or store without the matching `hold`
- a second `@mutex` on a pool
- storing the pointer from a `@buffer` pipe
- a function call inside a pipe filter
- writing `i` in a `do`
- `leave[name]` with no matching `@name`
- `@NoFailTolerance`
- `@ptr` or `@ref` as a mark
- `^=` as structural equality. The operator is `$=`
- a second `@childof`, a cycle, or a parent that is not an object
- a child field whose name the parent already uses
- a child method reusing a short name without `@ismutationof` of that parent method
- a subclass calling or mutating a `@private` method
- an implicit conversion between a child and its parent
- `as` between types that are not parent and descendant
- a downcast of a value
- `struct` used as a type
- an object embedding itself by value
- a time mark on a field
- a second object with the same short name in one namespace, or an object and a function with that short name there
- a second member with the same short name in one namespace
- using a namespace as a value, a parameter, or a parent
- `include` of a C or C++ header as Toplethos source
- a `foreign` function with a body, or `@body` on one
- `include lib` of a mangled C++ name
- an unknown `@`
- a detached pointer to a cell that dies with the caller
- two `@public` variables with the same short name in one namespace
- `@compeval` with an input that is not a constant
- `@dataparallel` arrays of different lengths
- a missing match arm when `@total` or the project switch `total` is on

## Not in the language

No overload resolution. No multiple inheritance. No implicit parent conversion. No struct keyword. No virtual dispatch. No vptr. A child method is a mutation, not an override. No inferred result types. No runtime polymorphic number. No `using`. No argument-dependent lookup. No borrow checker. No traits. No `goto` in this version. No `guard`. No default reduction besides the add on `@accum`. No gradient. No warp syntax. No silent device choice. No in-place rewrite of a definition. No dimension type beyond naming `@body` and `@geometry` in one transform. No walk of another function's private locals. A missing arm is not an error unless `@total` or the project switch `total` says so.
