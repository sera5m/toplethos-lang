# Task

You are editing nothing except a description. Do not rewrite the compiler. Return two things:

1. A register map for the direct loop.
2. The replacement AT&T instructions for the loop body, in the shape of the C++ loop below.

## Rules you may not break

- The sum must stay the same as separate `mulsd` then `addsd`. No `vfmadd`. No contraction.
- Do not use the `loop` instruction.
- `i` is an `i32`. The trip count is the constant 2000000. It does not wrap.
- After the first store, `i`, `x`, `y`, and `s` are present. Do not emit `cmpb` present-flag checks for them inside this loop.
- Coefficients 8, 7, 6, 5, 4, 3, 2, 1 and the scale `0.000001` are invariant. Hold them in xmm for the whole loop.
- `x = i * scale`, then seven Horner steps, then `s = s + y`. Same order as the source.
- Do not add page alignment. Do not change any other trial.

## C++ already does this (`.L477` in `main`)

```asm
.L477:
	pxor	%xmm10, %xmm10
	cvtsi2sdl	%eax, %xmm10
	addl	$1, %eax
	mulsd	.LC10(%rip), %xmm10
	movapd	%xmm10, %xmm0
	mulsd	%xmm9, %xmm0
	addsd	%xmm8, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm7, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm6, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm5, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm4, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm3, %xmm0
	mulsd	%xmm10, %xmm0
	addsd	%xmm2, %xmm0
	addsd	%xmm0, %xmm1
	cmpl	$2000000, %eax
	jne	.L477
```

`%eax` is `i`. `%xmm1` is `s`. `%xmm10` is `x`. `%xmm0` is `y`. The other xmm registers are the coefficients, loaded once before the loop.

## Toplethos today (`.L11` in `main`)

```asm
.L11:
    mov -336(%rbp), %eax
    cmp (%rsp), %eax
    jge .L13
    mov -336(%rbp), %eax
    cvtsi2sd %eax, %xmm0
    sub $16, %rsp
    movsd %xmm0, (%rsp)
    movsd .D43(%rip), %xmm0
    movsd (%rsp), %xmm1
    add $16, %rsp
    mulsd %xmm0, %xmm1
    movapd %xmm1, %xmm0
    movsd %xmm0, -240(%rbp)
    movb $1, -232(%rbp)
    # seven steps follow, each:
    #   movsd coeff(%rip), %xmm0
    #   spill it through (%rsp)
    #   reload x from -240(%rbp) into %xmm1
    #   mulsd / addsd
    # then y is stored to -256(%rbp), s is reloaded from -208(%rbp),
    # added through another (%rsp) spill, and stored back.
    incl -336(%rbp)
    jmp .L11
```

The measured cost of this row is 0.019 s. The C++ row is 0.0023 s. The Horner arithmetic is already fused. The rest is stack traffic.

## Return format

```text
registers:
  i: 
  x: 
  y: 
  s: 
  scale: 
  c8..c1: 
loop:
  <at&t only>
still on the stack, and why:
banned:
```

Nothing else.
