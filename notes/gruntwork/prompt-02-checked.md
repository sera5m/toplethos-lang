# Task

Return a replacement for `checked` only. Do not rewrite `main`. Do not touch the direct loop.

`checked` is called two million times. Each call rebuilds `f64 c[8] = {1,2,3,4,5,6,7,8}` on the stack, then counts `k` from 6 down to 0. The length is the constant 8. Every index in that countdown is in range. Every element was just stored, so it is present.

## C++ (`_Z7checkedPKdid`)

The array is the caller's, already in `%rdi`. One compare against `n`, then a pointer walk. No per-step bounds check.

```asm
	movapd	%xmm0, %xmm1
	movsd	56(%rdi), %xmm0
	cmpl	$6, %esi
	jbe	.L267
	leaq	48(%rdi), %rax
.L266:
	mulsd	%xmm1, %xmm0
	movq	%rax, %rdx
	subq	$8, %rax
	addsd	8(%rax), %xmm0
	cmpq	%rdi, %rdx
	jne	.L266
	ret
```

## Rust

Rust deleted the loop. `checked` is the same 14-instruction chain as `raw`, reading `0..56(%rdi)`. That is also acceptable, because the index range is a constant.

## Toplethos today, per iteration of `k`

```asm
    mov -240(%rbp), %eax          # k
    # compare k >= 0 through a 16-byte spill
    cmp $8, %ecx                  # bounds
    shl $4, %rcx                  # cell is 16 bytes, not 8
    cmpb $0, 8(%rax)              # present flag
    movsd (%rax), %xmm0
    # spill the coefficient
    # reload x from -80(%rbp)
    # reload y from -224(%rbp)
    mulsd
    addsd
    movsd %xmm0, -224(%rbp)       # store y back
    # k = k - 1 through another spill
    jmp .L1
```

The array init above this loop is eight `movsd` plus eight `movb $1`. It runs on every call.

## Rules

- Same rounding: `mulsd` then `addsd`. No `vfmadd`. No `loop`.
- A constant index into an array whose length is that constant, and whose elements were stored on every path to the load, does not need a bounds check or a present-flag check.
- The eight coefficients are invariant. They may live in `.rodata`, or in xmm if you unroll. They must not be rebuilt on the stack per call.
- Cell stride may stay 16 if a present flag is still required outside this function. Inside this function it is not required.
- Do not page-align `c`. It is 128 bytes.

## Return format

```text
choice: unroll like rust | pointer walk like c++
why:
loop or chain:
init removed: yes/no
what must stay a real check, and where:
```

Nothing else.
