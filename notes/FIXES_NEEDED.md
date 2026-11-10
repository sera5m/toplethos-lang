# Fixes needed

Wrong behavior in UMMES, release 1.2 tripar. A feature that is not built is not in this file. Those rows are [implemented_in_compiler.md](implemented_in_compiler.md) and [implemented_in_enterpreter.md](implemented_in_enterpreter.md).

USE says two hosts that accept a program produce the same result. Each row below is a place they do not, or a place one host breaks a rule it already implements.

## Hosts disagree

| what | interpreter | amd64 listing |
|---|---|---|
| `@mutex` outside `lock` or `hold` | A failure at run time: `outside lock or hold`. USE says this is a compile error. Bind does not reject it. | `Lock` and `Unlock` are empty. The read is allowed. |
| `f64` divided by zero | A failure: `division by zero`. | `divsd` yields an infinity. Integer division by zero is a failure on both, including the lowest signed value divided by -1. |
| `saveArrayAsbmp` past the cells | Returns 0 when `width * height` does not fit in the allocated cells from that index. It does not know the declared length, so a count that stays inside the frame but past the array is still written. | The pointer is a raw byte address. There is no check. A count past the frame can fault. |

## Interpreter, on a program that should finish

The step counter is a signed `int` and the limit is 10000000. A longer run stops with `stopped after 10000000 steps`. Each call allocates a frame and nothing frees it, so a long `--run` can also be killed by the OS before the counter trips. `sh aot/battery.sh` hits the counter on the N=2000000 `--run`. The `--asm` row is the one next to C++ and Rust.

## Fork

`start` runs the lanes in order and sums the `f64` results. That order is what the batteries match. It is not an OS thread. Two further rows are still wrong against USE Runs:

- A second `fork` before `finish` overwrites the pending fork. USE says that is a failure. Neither host raises it.
- A lane that fails aborts `start` immediately. USE says the failure is raised after the other lanes finish.
