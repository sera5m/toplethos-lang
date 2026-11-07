#include "tp/bytecode.hpp"

namespace tp {
namespace {

void require_row(const Insn& in) {
  if (!in.atom) throw Fail("spirv", "opcode is missing its atom", in.line, 0);
  if (in.atom->family != Family::SignedInt || (in.atom->bits != 32 && in.atom->bits != 64)) {
    throw Fail("spirv", "no spirv row for " + in.atom->name + " yet. The bytecode still has that atom.", in.line, 0);
  }
}

std::string id(uint32_t slot) { return "%" + std::to_string(slot + 1); }

}  // namespace

std::string emit_spirv(const Program& prog) {
  std::string out = "; target spirv\n; ";
  out += stage_info("spirv").does;
  out += "\n; Vulkan loads this module. It does not switch on these opcodes.\n";
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) require_row(in);
    out += "; " + shape_fn(fn.shape) + "\n";
    bool saw32 = false;
    bool saw64 = false;
    for (const Insn& in : fn.code) {
      if (in.atom->bits == 32) saw32 = true;
      if (in.atom->bits == 64) saw64 = true;
    }
    if (saw32) out += "%i32 = OpTypeInt 32 1\n";
    if (saw64) out += "%i64 = OpTypeInt 64 1\n";
    for (const Insn& in : fn.code) {
      std::string ty = in.atom->bits == 64 ? "%i64" : "%i32";
      switch (in.op) {
        case Op::Imm:
          out += id(in.dst) + " = OpConstant " + ty + " " + std::to_string(static_cast<int64_t>(in.imm)) + "\n";
          break;
        case Op::Mov:
          out += id(in.dst) + " = OpCopyObject " + ty + " " + id(in.lhs) + "\n";
          break;
        case Op::Add:
          out += id(in.dst) + " = OpIAdd " + ty + " " + id(in.lhs) + " " + id(in.rhs) + "\n";
          break;
        case Op::Sub:
          out += id(in.dst) + " = OpISub " + ty + " " + id(in.lhs) + " " + id(in.rhs) + "\n";
          break;
        case Op::Mul:
          out += id(in.dst) + " = OpIMul " + ty + " " + id(in.lhs) + " " + id(in.rhs) + "\n";
          break;
        case Op::Div:
          throw Fail("spirv", "division is in the bytecode; the spirv row is not written yet", in.line, 0);
        case Op::Ret:
          out += "; return " + id(in.dst) + " " + in.atom->name + " bits=" + std::to_string(in.atom->bits) + "\n";
          out += "OpReturnValue " + id(in.dst) + "\n";
          break;
      }
    }
  }
  return out;
}

}  // namespace tp
