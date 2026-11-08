#include "tp/arch.hpp"
#include "tp/arith.hpp"
#include "tp/eval.hpp"

#include <optional>
#include <vector>

namespace tp {
namespace {

bool folds(Op op) {
  switch (op) {
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Rem:
    case Op::And:
    case Op::Or:
    case Op::Xor:
    case Op::Shl:
    case Op::Shr:
    case Op::Eq:
    case Op::Ne:
    case Op::Lt:
    case Op::Le:
    case Op::Gt:
    case Op::Ge:
    case Op::Pow:
    case Op::FloorDiv:
      return true;
    default:
      return false;
  }
}

/* Pass 1. An opcode whose inputs are already immediates becomes an immediate.
   A label or a jump ends that knowledge. A branch does not, on the fall-through. */
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
        if ((in.lhs < fn.flag.size()) && (fn.flag[in.lhs] & (kFlagEphemeral | kFlagMutex))) {
          known[in.dst].reset();
          break;
        }
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
      case Op::Not:
      case Op::Neg:
      case Op::BitNot:
        if (known[in.lhs]) {
          try {
            in.imm = apply(in.op, *in.atom, *known[in.lhs], 0, "eval");
          } catch (const Fail& err) {
            throw Fail("eval", err.what(), in.line, 0);
          }
          in.op = Op::Imm;
          in.lhs = 0;
          known[in.dst] = in.imm;
          ++folded;
        } else {
          known[in.dst].reset();
        }
        break;
      case Op::Label:
      case Op::Jmp:
        known.assign(fn.nslots, std::nullopt);
        break;
      case Op::Br:
      case Op::BrRange:
      case Op::Ret:
        break;
      case Op::Store:
        known.assign(fn.nslots, std::nullopt);
        break;
      case Op::Addr:
      case Op::Load:
        if (in.dst < fn.nslots) known[in.dst].reset();
        break;
      case Op::Call:
      case Op::Start:
        known.assign(fn.nslots, std::nullopt);
        break;
      case Op::Threads:
      case Op::Freemem:
        if (in.dst < fn.nslots) known[in.dst].reset();
        break;
      default:
        if (folds(in.op) && known[in.lhs] && known[in.rhs]) {
          const Atom* numeric = in.src ? in.src : in.atom;
          uint64_t value = 0;
          try {
            value = apply(in.op, *numeric, *known[in.lhs], *known[in.rhs], "eval");
          } catch (const Fail& err) {
            if (std::string(err.what()).find("division by zero") != std::string::npos) {
              if (fn.divzero) {
                value = div_stand_in(*numeric, fn.divzero);
              } else {
                if (in.dst < fn.nslots) known[in.dst].reset();
                break;
              }
            } else {
              throw Fail("eval", err.what(), in.line, 0);
            }
          }
          in.op = Op::Imm;
          in.imm = value;
          in.lhs = 0;
          in.rhs = 0;
          known[in.dst] = value;
          ++folded;
        } else if (in.dst < fn.nslots) {
          known[in.dst].reset();
        }
        break;
    }
    next.push_back(in);
  }
  fn.code = std::move(next);
}

bool has_control(const BcFunc& fn) {
  for (const Insn& in : fn.code) {
    if (in.op == Op::Jmp || in.op == Op::Br || in.op == Op::BrRange || in.op == Op::Label) return true;
  }
  return false;
}

/* Pass 2. Drop a write that no later instruction reads. Ret is a read.
   A branch joins two paths, and this walk is not a graph, so it does not run then. */
void drop_unread(BcFunc& fn, int& dropped) {
  if (has_control(fn)) return;
  std::vector<char> used(fn.nslots, 0);
  std::vector<char> keep(fn.code.size(), 0);
  for (size_t n = fn.code.size(); n > 0; --n) {
    const Insn& in = fn.code[n - 1];
    bool writes = in.dst < fn.nslots && in.op != Op::Store;
    bool side = in.op == Op::Ret || in.op == Op::Store || in.op == Op::Call || in.op == Op::Print || in.op == Op::Trap ||
                in.op == Op::StaticInit || in.op == Op::Fork || in.op == Op::Start || in.op == Op::Lock || in.op == Op::Unlock ||
                in.op == Op::Watch;
    bool live = side || (writes && used[in.dst]);
    if (writes && in.op != Op::Ret) used[in.dst] = 0;
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
      case Op::Rem:
      case Op::And:
      case Op::Or:
      case Op::Xor:
      case Op::Shl:
      case Op::Shr:
      case Op::Eq:
      case Op::Ne:
      case Op::Lt:
      case Op::Le:
      case Op::Gt:
      case Op::Ge:
        used[in.lhs] = 1;
        used[in.rhs] = 1;
        break;
      case Op::Not:
      case Op::Neg:
      case Op::BitNot:
      case Op::Br:
      case Op::BrRange:
      case Op::Load:
      case Op::Addr:
      case Op::Cast:
      case Op::State:
        used[in.lhs] = 1;
        break;
      case Op::Pow:
      case Op::FloorDiv:
        used[in.lhs] = 1;
        used[in.rhs] = 1;
        break;
      case Op::Print:
      case Op::Lock:
      case Op::Unlock:
        if (in.lhs < fn.nslots) used[in.lhs] = 1;
        break;
      case Op::Fork:
        if (in.lhs < fn.nslots) used[in.lhs] = 1;
        for (uint32_t arg : in.args)
          if (arg < fn.nslots) used[arg] = 1;
        break;
      case Op::Store:
        used[in.lhs] = 1;
        used[in.rhs] = 1;
        break;
      case Op::Call:
        for (uint32_t arg : in.args) used[arg] = 1;
        break;
      case Op::Imm:
      case Op::Jmp:
      case Op::Label:
      case Op::Threads:
      case Op::Freemem:
      case Op::Trap:
      case Op::StaticInit:
      case Op::Start:
      case Op::Clock:
        break;
      default:
        if (in.lhs < fn.nslots) used[in.lhs] = 1;
        if (in.rhs < fn.nslots) used[in.rhs] = 1;
        for (uint32_t arg : in.args)
          if (arg < fn.nslots) used[arg] = 1;
        break;
    }
  }
  std::vector<Insn> next;
  for (size_t i = 0; i < fn.code.size(); ++i) {
    if (keep[i]) next.push_back(fn.code[i]);
  }
  fn.code = std::move(next);
}

void mark_device(BcFunc& fn) {
  fn.gpu_ops = true;
  fn.scalar_ctrl = false;
  for (Insn& in : fn.code) {
    if (in.op == Op::Jmp || in.op == Op::Br || in.op == Op::BrRange || in.op == Op::Label) fn.scalar_ctrl = true;
    bool ok = false;
    if (in.op == Op::Jmp || in.op == Op::Label) ok = true;
    else if (in.atom && in.atom->family != Family::Pointer) ok = arch_supports(Target::Spirv, in.op, in.atom->name);
    in.gpu = ok;
    if (!ok) fn.gpu_ops = false;
  }
}

bool marked_inlined(const BcFunc& fn) {
  for (Mutator m : fn.shape.mutators) {
    if (m == Mutator::Inlined) return true;
  }
  return false;
}

}  // namespace

Evaluated evaluate(Program prog) {
  int folded = 0;
  int dropped = 0;
  for (BcFunc& fn : prog.fns) {
    fold_const(fn, folded);
    drop_unread(fn, dropped);
    mark_device(fn);
  }
  Evaluated out;
  out.prog = std::move(prog);
  out.report = "stage eval\n";
  out.report += "Passes rewrite the bytecode. A backend runs after this, so it prints the folded value and not the arithmetic that produced it.\n";
  out.report += "pass const: folded " + std::to_string(folded) + "\n";
  out.report += "pass dce: dropped " + std::to_string(dropped) + "\n";
  for (const BcFunc& fn : out.prog.fns) {
    out.report += "static " + fn.shape.name + " drop=" + std::to_string(fn.drop_n) + " reuse=" + std::to_string(fn.reuse_n) +
                  " inc=" + std::to_string(fn.inc_n) + " range=" + std::to_string(fn.range_n) +
                  " forward=" + std::to_string(fn.forward_n) + " cost=unknown pointers cell=" +
                  (fn.cell_ptr ? "yes" : "-") + " ffi=- inlined=" + (marked_inlined(fn) ? "yes" : "-") +
                  " jit-fn=- jit-var=- neuron=- manager=-\n";
  }
  return out;
}

}  // namespace tp
