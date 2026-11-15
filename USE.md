# Use guide

This is language spec 1.0. The compiler release is 1.2 tripar. [MATRIX.md](MATRIX.md) is older and does not win where they differ. A program with `fn main` is parsed by one front end. The interpreter and the amd64 listing both execute that program, or both reject it. A file with no `fn main` is not a program.

`()` groups. It never calls and it never takes an attribute argument. `[]` does.

`//` starts a comment that runs to the end of the line. No space is required. `/*` starts a comment that runs until `*/` and may cross lines. A comment does not nest.

A statement ends with `;`. A brace block does not. `return`, a declaration, a store, and a call used as a statement all end with `;`.

`a + b * c` is `a + (b * c)`. `a - b - c` is `(a - b) - c`. `a = b = c` is `a = (b = c)`. The order is C++ precedence, and only these operators exist:

| order | operators | associativity |
|---|---|---|
| 1 | `.` `in[]` `[index]` `++` `--` | postfix `++` `--` |
| 2 | unary `+` `-` `!` `~` `*` `&` `++` `--`, and `as[T, v]` | |
| 3 | `**` | right |
| 4 | `*` `/` `%` `/_/` | left |
| 5 | `+` `-` | left |
| 6 | `<<` `>>` | left |
| 7 | `<` `>` `<=` `>=` | left |
| 8 | `==` `!=` `$=` `!$=` | left |
| 9 | `&` | left |
| 10 | `^` | left |
| 11 | `\|` | left |
| 12 | `&&` | left, and the right runs only when the left is true |
| 13 | `\|\|` | left, and the right runs only when the left is false |
| 14 | `=` `+=` | right |

`!`, `&&`, and `||` take `bool` and result in `bool`. An integer is not a `bool`. Unary `&` is the address. Binary `&`, `|`, and `^` are bitwise, and so are `~`, `<<`, and `>>`. `$=` lowers to constant false. `!$=` lowers to constant true. There is no structural row behind them. `^=` is not an operator. The lexer reads `^` and then `=`.

Floor division is the token `/_/`. `a / b` is division toward zero.

## What a variable is

A variable is one cell.

| mark | meaning |
|---|---|
| unmarked | Mutable. Freed when the function returns. Any number of stores. |
| `@ephemeral` | One read. A second use is a failure. `state[name]` looks and does not consume it. |
| `@static` | Life of the process. The initializer runs on the first call. A later call skips it and keeps every element, including an array. |
| `@config` | Life of the process. First store only. A later store is a compile error. |
| `@val` | Frozen after the first store. A later store is a compile error. |
| `@xor` | A copy of the value. |
| `@mut` | Changes nothing on an ordinary cell. On `@val` or `@config` it is a compile error. |
| `@mutex` | The interpreter counts `lock` and `hold`. A use outside them is a failure there. |
| `@private` | Hidden from a file that `include`s this one. Default for a local. |

`@public` and `@protected` are not marks in this version. A name declared inside a brace ends at that brace. The same name in that same brace is `already declared`. A parameter lives for the whole function.

States sit on a tracked cell: **absent** (no value), **present** (a store has happened), **empty** (an ephemeral after its one use). `if[state[name]]` looks at the state and does not read the value. A read of an absent or empty value is a failure. An array with no initializer is stored as zeros, not left absent. An uninitialized `@static` array is those zeros. An uninitialized `@static` scalar stays absent until its initializer runs.

## Types

| name | meaning |
|---|---|
| `i4` `i8` `i16` `i32` `i64` | Signed integers. |
| `ui4` `ui8` `ui16` `ui32` `ui64` | Unsigned integers. |
| `f64` | IEEE 754 binary64. |
| `bool` | `true` or `false`. |
| `T*` | A pointer. Kind `cell` is an interpreter index. `*p` loads. `*p = v` stores. |
| `object` | A layout. Not a `struct` keyword. |

`f32`, `char`, `string`, `size`, and the other float widths are not types here. `int` and `long` are not names.

`+` `-` `*` `/` `%` `&` `|` `^` `<<` `>>` `~` and the compares take one atom. Two widths do not add. Integer `/` truncates toward zero. `/` or `%` by zero is a failure. Dividing the lowest signed value by `-1`, or negating it, is a failure. A shift count outside `0` .. width-1 is a failure, and a constant count outside that is a compile error. Integer `+` and `-` wrap. `a/_/b` is floor division. `a ** b` on integers rejects a negative exponent. `0 ** 0` is `1`. A power that does not fit is a failure. Unary `-` binds tighter than `**`, so `-2 ** 2` is `(-2) ** 2`.

`@divzero[0]` makes a zero divisor return `0`. `@divzero[max]` returns the largest finite positive value of that atom. Any other argument is a compile error. The mark is off unless you write it.

`as[T, v]` widens a signed or unsigned integer, or converts an integer to `f64`, or reads an enum as an integer. A narrowing `as` with no row is rejected. Pointer `==` and `!=` compare the address. An ordered compare of pointers is a compile error.

`++` and `--` add or subtract one. The prefix value is the new value. The postfix value is the old value.

The amd64 listing prints arithmetic for `i32`, `i64`, `f64`, and `bool`. `ui32` and `ui64` have no amd64 row. Widths below 32 are slot traffic on that listing so a bitmap writer can read the cells. The interpreter runs every integer width in the table above. Where the two hosts disagree, the row is in [notes/FIXES_NEEDED.md](notes/FIXES_NEEDED.md).

## Arrays and enums

An array length is a constant. `i32[0]` is a compile error. A short brace list zero-fills the tail. A long one is a compile error. One pair of brackets is the length. A second `[` is an index.

```text
i32 tape[4] = {1, 2, 3};
i32 cell = tape[0];
i32* p = &tape;
```

`&` takes a name. It does not take an index. `tape` in a pointer hole is the address of element 0.

An enum is a closed set of names, not an integer. Omitted width is 32 bits. The first name is `0` unless you write a value. Two names with the same value are a compile error.

```text
enum Color {
  red,
  green,
  blue = 4
}
Color c = Color::red;
match @total [c] {
  Color::red { }
  Color::green { }
  Color::blue { }
}
```

`match` does not fall through. A missing arm is a no-op. `@total` makes a missing named arm a compile error. `match` of an `i32`, an `i64`, or a `bool` takes constant arms of that atom. `match` is lowered to `if`.

## Objects

An object is a layout in source order. No vptr. One parent, written `@childof`. The parent is a prefix at offset 0. A second `@childof`, a cycle, or a repeated field name is a compile error. An embedded object is a field, not a parent. An object must not embed itself by value. A pointer field may.

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
```

A method is a function inside the object. The call is `p.move in[1, 2]`. `HERE` is the cell address of the instance. A child method that reuses a short name needs `@ismutationof[...]`. The pack is not evaluated. The call is chosen from the static type. `as[Parent, &child]` is the parent prefix. A downcast is a compile error. There is no implicit conversion between a child and its parent.

## Functions

```text
fn add in[i32 a] out[i32] {
  return a + 1;
}
fn main in[] out[i32] {
  return add in[41];
}
```

`out[i32]` is the result type. You write it. `out[]` means there is no result. `return` of a value from `out[]` is a compile error. A path out of `out[T]` with no `return` is a compile error. A second `main` is a compile error. `name()` is not a call. The call is `name in[args]`.

`include "file.tpth_s"` loads that file once. `@private` in that file stays there. A C or C++ header is rejected. `include lib` is rejected.

`@require[bool]` traps at run time when the bool is false.

`@inlined` on a pure scalar `i32`, `i64`, or `bool` function asks the interpreter to compile that function, check it against the oracle, and call the native symbol. A call, a print, a watch, a file load or save, or a pointer is rejected.

`@time["name"] watch <f64> { }` measures the brace with a monotonic clock and prints `name`, the seconds, and the `f64`.

## Control

`if` takes a `bool`. A missing `else` does nothing.

`loop` runs until `leave`. `leave[name]` ends the loop marked `@name[name]`.

`do n[k]` runs the body `k` times. `i` is `0` through `k - 1`, read-only, and only for that `do`, including a nested `do` and its `onFinished`. Writing `i` is a compile error. `onFinished` runs once after the `do`.

`continue` starts the next iteration of the innermost `do` or `loop`. Inside a `do` it still increments `i`. Outside both, the message is `continue is not inside a loop`.

## Runs

`@parallel[n]` marks a worker. The worker returns `f64`. The first parameter is the lane, an `i32`.

```text
fork work in[4];
f64 total = start work;
finish work;
f64 total = run work in[4];
```

`fork` stores the count. It does not enter the body. `start` runs the lanes in order on this thread, from `0` through `count - 1`, and returns the sum of the `f64` results. `finish` does nothing further. The lanes already finished inside `start`. `run` is `fork`, then `start`. A second `fork` before `finish` replaces the pending count. A lane that fails stops `start`. This is not an operating-system thread, and it is not a device launch.

`@gpuparallel` and `@NoFailTolerance` are rejected on purpose.

## Files

`loadbmp` reads one 24-bit uncompressed bitmap into an `i32` array. The path is a string literal. The array's length must be the file's width times its height. A pixel is `r * 65536 + g * 256 + b`. A file that is missing, compressed, or a different size is a failure. A negative height is top-down. `loadbmp` has no result.

```text
i32 pix[2249868] @static;
loadbmp in["examples/street.bmp", pix];
```

`loadbytes` reads a file into an `i32` array, one byte per cell, and results in the number of bytes stored. A missing file is a failure. A file longer than the array is a failure. Cells past the end of the file are left as they were.

```text
i32 buf[140000] @static;
i32 n = loadbytes in["lipsum-cicero.txt", buf];
```

`loadfile` is the same read. A longer file is cut to the array length, and that length is the result. A missing file is still a failure.

`savebmp` writes one 24-bit uncompressed bottom-up bitmap and has no result. The arguments are a string-literal path, an `i32` array, an `i32` width, and an `i32` height. A pixel is `r * 65536 + g * 256 + b`. A failed write is a failure.

```text
savebmp in["out.bmp", pix, w, h];
```

`saveArrayAsbmp` writes the same kind of file. The first argument is an integer array, or a pointer to a signed or unsigned integer from `i4` through `i64` or `ui4` through `ui64`. Then an `i32` width, an `i32` height, and a string-literal path. The result is `i32` `1` or `0`. It does not raise. Width and height must be in `1` .. `1048576`. A cell of 24 bits or more stores `r * 65536 + g * 256 + b` in the low 24 bits. A cell of 8 .. 23 bits stores grayscale in the low 8 bits. A cell of fewer than 8 bits stores `v * 255 / ((1 << bits) - 1)`. Row 0 of the array is the top row.

```text
i32 pix[4] = {255, 65280, 16711680, 16777215};
i32 ok = saveArrayAsbmp in[pix, 2, 2, "out.bmp"];
```

`print` writes one value and a newline. `threads` is the processor count, an `i32`. `freemem` is available RAM in bytes, an `i64`. If the host cannot answer, that is a failure. It is not `0`.

`min`, `max`, `clamp`, and `abs` take one atom. `abs` of the lowest signed value is a failure. `swap` takes two pointers of one type.

```text
i32 lo = min in[a, b];
i32 hi = clamp in[v, 0, 10];
swap in[&a, &b];
```

## Keywords

| keyword | what it does |
|---|---|
| `fn` | One definition. Called as `name in[args]`. |
| `in[ ]` | The parameters. A value is copied. A pointer is not. |
| `out[ ]` | The result. `out[]` means none. |
| `return` | Yields that result and leaves. |
| `HERE` | The instance, inside a method. |
| `include` | Loads one `.tpth_s` file once. |
| `object` | One layout. `@childof[Parent]` is the one parent. |
| `if` | Runs the body when the condition is true. |
| `match` | One arm. No fall-through. |
| `do n[k]` | The body, `k` times. |
| `onFinished` | Once, after that `do`. |
| `loop` | Until `leave`. |
| `continue` | Next iteration of the innermost `do` or `loop`. |
| `leave` | Ends that `do` or `loop`. `leave[name]` uses `@name`. |
| `lock` `unlock` | The mutex pair. |
| `hold` | That pair around a block. |
| `fork` | Stores a lane count. Does not enter the body. |
| `start` | Runs those lanes in order and sums `f64`. |
| `finish` | The lanes already finished. |
| `run` | `fork`, then `start`. |
| `enum` | A closed set of names. |
| `min` `max` `clamp` `abs` | Arithmetic. Same atom. |
| `swap` | Two pointers, one atom. |
| `as` | A conversion that has a row. |
| `print` `threads` `freemem` | Host facts. |
| `loadbmp` `loadbytes` `loadfile` `savebmp` `saveArrayAsbmp` | The file operations above. |

`@name` is allowed on `loop` and `do`. An unknown `@` is a compile error.

## Nothing is undefined

A program that compiles has one meaning. A violation is a compile error or a failure. Two hosts that accept a program produce the same result, apart from the rows in [notes/FIXES_NEEDED.md](notes/FIXES_NEEDED.md).

## Compile errors

- `return` of a value when `out[]` has no result type
- a result whose type is not the designated `out` type
- a path out of `out[T]` with no `return`
- a second `main`, or a program with no `main`
- `name()` as a call
- `+` with no row, or a narrowing `as` with no row
- `/` by zero is a failure, not a compile error
- a constant shift count outside `0` .. width-1
- a second declaration of a name in the same brace
- `continue` outside a `do` or a `loop`
- writing `i` in a `do`
- `if` or `match` used where a `bool` is required and the condition is not `bool`
- a later store to `@val` or `@config`
- `@mut` written on `@val` or `@config`
- `@divzero` with an argument other than `0` or `max`
- an ordered compare of pointers
- `namespace`
- `@NoFailTolerance`
- `@gpuparallel`
- an unknown `@`
- `include lib`, or `include` of a C or C++ header
- a second `@childof`, a cycle, or a parent that is not an object
- a child field whose name the parent already uses
- a child method reusing a short name without `@ismutationof`
- an implicit conversion between a child and its parent
- a downcast
- `struct` used as a type
- an object embedding itself by value
- two enumerators with the same value
- a missing match arm when `@total` is set
- `i32[0]`, or an array length that is not a constant
- a brace list longer than the array
- `min`, `max`, or `clamp` of two different atoms
- `loadbmp` or `savebmp` used as a value
- `loadbmp`, `loadbytes`, `loadfile`, or `savebmp` without a named `i32` array

## Not in the language

These are not waiting. A program that names one is rejected, or the token is not a keyword.

No namespace. No `string` or `char` value. No `rng`, `rand`, or `randf`. No pipe, no pool, no `push` or `pop`. No coroutine, `defer`, `onfail`, `select`, or `fatal`. No page placement and no load block. No `flow`. No function value, `fnid`, `instance`, `compose`, `bind`, or `transform`. No operating-system thread. No GPU launch and no Vulkan module. No overload. No multiple inheritance. No vptr. No downcast. No `goto`. No undefined behavior.
