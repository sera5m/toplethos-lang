#include "tp/arith.hpp"
#include "tp/bytecode.hpp"

#include <cstring>

namespace tp {
namespace {

uint64_t mask_bits(int bits) {
  if (bits >= 64) return ~0ull;
  return (1ull << bits) - 1ull;
}

int64_t signed_result(uint64_t bits, const Atom& atom) {
  if (atom.family == Family::Float && atom.name == "f64") {
    double d;
    std::memcpy(&d, &bits, 8);
    return static_cast<int64_t>(d);
  }
  uint64_t m = mask_bits(atom.bits);
  uint64_t v = bits & m;
  if (atom.family == Family::SignedInt && atom.bits < 64) {
    uint64_t sign = 1ull << (atom.bits - 1);
    if (v & sign) v |= ~m;
  }
  return static_cast<int64_t>(v);
}

const BcFunc* find_fn(const Program& prog, const std::string& name) {
  for (const BcFunc& fn : prog.fns) {
    if (fn.shape.name == name) return &fn;
  }
  return nullptr;
}

}  // namespace

int64_t interpret(const Program& prog, const std::string& entry) {
  const BcFunc* fn = find_fn(prog, entry);
  if (!fn) throw Fail("interpret", "no function '" + entry + "'", 0, 0);
  if (!fn->shape.outputs.empty() && fn->shape.outputs[0].atom->family == Family::Float) {
    throw Fail("interpret", "this runner prints an integer result; " + fn->shape.outputs[0].atom->name + " stays in the bytecode", 0, 0);
  }
  std::vector<uint64_t> slot(fn->nslots, 0);
  for (const Insn& in : fn->code) {
    switch (in.op) {
      case Op::Imm:
        slot[in.dst] = in.imm;
        break;
      case Op::Mov:
        slot[in.dst] = slot[in.lhs];
        break;
      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Div:
        slot[in.dst] = apply(in.op, *in.atom, slot[in.lhs], slot[in.rhs], "interpret");
        break;
      case Op::Ret:
        return signed_result(slot[in.dst], *in.atom);
    }
  }
  throw Fail("interpret", "'" + entry + "' did not return", 0, 0);
}

}  // namespace tp
