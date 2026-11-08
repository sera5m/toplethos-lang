#pragma once

#include "tp/bytecode.hpp"

namespace tp {

/* One arithmetic row. The interpreter and the evaluation passes both call it,
   so a folded immediate is the same bits the oracle would have computed. */
uint64_t apply(Op op, const Atom& atom, uint64_t lhs, uint64_t rhs, const char* stage);

/* as[dst, src]. Constant rows and the interpreter share this. */
uint64_t cast_bits(const Atom& src, const Atom& dst, uint64_t bits, const char* stage);

/* @divzero stand-in. mode 1 is 0, mode 2 is the signed maximum. */
uint64_t div_stand_in(const Atom& atom, int mode);

}  // namespace tp
