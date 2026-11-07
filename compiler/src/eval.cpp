#include "tp/arith.hpp"
#include "tp/eval.hpp"

#include <optional>
#include <vector>

namespace tp {
namespace {

/* Pass 1. An opcode whose inputs are already immediates becomes an immediate.
   Mov of an immediate becomes that immediate. Later backends must not re-add. */
void fold_const(BcFunc& fn, int& folded) {
  std::vector<std::optional<uint64_t>> known(fn.nslots);
  std::vector<Insn> next;
  next.reserve(fn.code.size());
  for (Insn in : fn.code) {
    switch (in.op) {
      case Op::Imm:
        known[in.dst] = in.imm;
        break;
      case Op::Mov:
        if (known[in.lhs]) {
          in.op = Op::Imm;
          in.imm = *known[in.lhs];
          in.lhs = 0;
          in.rhs = 0;
          known[in.dst] = in.imm;
          ++folded;
        } else {
          known[in.dst].reset();
        }
        break;
      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Div:
        if (known[in.lhs] && known[in.rhs]) {
          uint64_t value = apply(in.op, *in.atom, *known[in.lhs], *known[in.rhs], "eval");
          in.op = Op::Imm;
          in.imm = value;
          in.lhs = 0;
          in.rhs = 0;
          known[in.dst] = value;
          ++folded;
        } else {
          known[in.dst].reset();
        }
        break;
      case Op::Ret:
        break;
    }
    next.push_back(in);
  }
  fn.code = std::move(next);
}

/* Pass 2. Drop a write that no later instruction reads. Ret is a read. */
void drop_unread(BcFunc& fn, int& dropped) {
  std::vector<char> used(fn.nslots, 0);
  std::vector<char> keep(fn.code.size(), 0);
  for (size_t n = fn.code.size(); n > 0; --n) {
    const Insn& in = fn.code[n - 1];
    bool live = in.op == Op::Ret || used[in.dst];
    if (in.op != Op::Ret) used[in.dst] = 0;
    if (!live) {
      ++dropped;
      continue;
    }
    keep[n - 1] = 1;
    switch (in.op) {
      case Op::Ret:
        used[in.dst] = 1;
        break;
      case Op::Mov:
        used[in.lhs] = 1;
        break;
      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Div:
        used[in.lhs] = 1;
        used[in.rhs] = 1;
        break;
      case Op::Imm:
        break;
    }
  }
  std::vector<Insn> next;
  for (size_t i = 0; i < fn.code.size(); ++i) {
    if (keep[i]) next.push_back(fn.code[i]);
  }
  fn.code = std::move(next);
}

}  // namespace

Evaluated evaluate(Program prog) {
  int folded = 0;
  int dropped = 0;
  for (BcFunc& fn : prog.fns) {
    fold_const(fn, folded);
    drop_unread(fn, dropped);
  }
  Evaluated out;
  out.prog = std::move(prog);
  out.report = "stage eval\n";
  out.report += "Passes rewrite the bytecode. A backend runs after this, so it prints the folded value and not the arithmetic that produced it.\n";
  out.report += "pass const: folded " + std::to_string(folded) + "\n";
  out.report += "pass dce: dropped " + std::to_string(dropped) + "\n";
  return out;
}

}  // namespace tp
