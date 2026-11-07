#pragma once

#include "tp/bytecode.hpp"

namespace tp {

/* One arithmetic row. The interpreter and the evaluation passes both call it,
   so a folded immediate is the same bits the oracle would have computed. */
uint64_t apply(Op op, const Atom& atom, uint64_t lhs, uint64_t rhs, const char* stage);

}  // namespace tp
