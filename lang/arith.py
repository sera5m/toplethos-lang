"""Integer and float rows shared by the interpreter and the constant folder.

The compiler's helpers in desktop.c follow the same rules.
"""

import math

from front import Failure

I32_MIN = -2147483648
I64_MIN = -9223372036854775808


def wrap(n, bits):
    mask = (1 << bits) - 1
    n &= mask
    sign = 1 << (bits - 1)
    if n >= sign:
        n -= 1 << bits
    return n


def limits(bits):
    return -(1 << (bits - 1)), (1 << (bits - 1)) - 1


def trunc_div(a, b):
    if b == 0:
        return None
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def iop(op, a, b, bits, standin=None):
    lo, hi = limits(bits)
    if op == "+":
        return wrap(a + b, bits)
    if op == "-":
        return wrap(a - b, bits)
    if op == "*":
        return wrap(a * b, bits)
    if op == "/":
        if b == 0:
            if standin is None:
                raise Failure("division by zero")
            return standin
        if a == lo and b == -1:
            raise Failure("division overflow")
        return trunc_div(a, b)
    if op == "%":
        if b == 0:
            if standin is None:
                raise Failure("division by zero")
            return standin
        if a == lo and b == -1:
            raise Failure("division overflow")
        q = trunc_div(a, b)
        return wrap(a - q * b, bits)
    if op == "//":
        if b == 0:
            if standin is None:
                raise Failure("division by zero")
            return standin
        if a == lo and b == -1:
            raise Failure("division overflow")
        return a // b
    if op == "**":
        return ipow(a, b, bits)
    if op == "&":
        return wrap(a & b, bits)
    if op == "|":
        return wrap(a | b, bits)
    if op == "^":
        return wrap(a ^ b, bits)
    if op in ("<<", ">>"):
        return ishift(a, b, bits, op)
    raise Failure("bad operator")


def ipow(a, b, bits):
    if b < 0:
        raise Failure("negative power")
    lo, hi = limits(bits)
    acc = 1
    base = a
    exp = b
    while exp:
        if exp & 1:
            acc = _mul_fit(acc, base, lo, hi)
        exp >>= 1
        if exp:
            base = _mul_fit(base, base, lo, hi)
    return acc


def _mul_fit(x, y, lo, hi):
    p = x * y
    if p < lo or p > hi:
        raise Failure("power overflow")
    return p


def ishift(a, count, bits, op):
    if count < 0 or count >= bits:
        raise Failure("shift")
    if op == "<<":
        return wrap(a << count, bits)
    # Arithmetic. Python >> already fills the sign.
    return a >> count


def bitnot(a, bits):
    return wrap(~a, bits)


def fop(op, a, b, standin=None):
    if op == "**":
        r = math.pow(a, b)
        if math.isnan(r):
            raise Failure("power")
        return r
    if op == "//":
        if b == 0.0:
            if standin is None:
                raise Failure("division by zero")
            return standin
        return math.floor(a / b)
    raise Failure("bad operator")
