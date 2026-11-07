#include "tp/bytecode.hpp"

namespace tp {
namespace {

void require_row(const Insn& in) {
  if (!in.atom) throw Fail("arm64", "opcode is missing its atom", in.line, 0);
  if (in.atom->family != Family::SignedInt || (in.atom->bits != 32 && in.atom->bits != 64)) {
    throw Fail("arm64", "no arm64 row for " + in.atom->name + " yet. The bytecode still has that atom.", in.line, 0);
  }
}

std::string imm_mov(bool wide, int64_t value) {
  uint64_t bits = static_cast<uint64_t>(value);
  std::string reg = wide ? "x8" : "w8";
  std::string out = "    movz " + reg + ", #" + std::to_string(bits & 0xffff) + "\n";
  if ((bits >> 16) & 0xffff) out += "    movk " + reg + ", #" + std::to_string((bits >> 16) & 0xffff) + ", lsl #16\n";
  if (wide && ((bits >> 32) & 0xffff)) out += "    movk x8, #" + std::to_string((bits >> 32) & 0xffff) + ", lsl #32\n";
  if (wide && ((bits >> 48) & 0xffff)) out += "    movk x8, #" + std::to_string((bits >> 48) & 0xffff) + ", lsl #48\n";
  return out;
}

}  // namespace

std::string emit_arm64(const Program& prog) {
  std::string out = "// target arm64\n// ";
  out += stage_info("arm64").does;
  out += "\n    .text\n";
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) require_row(in);
    int frame = static_cast<int>(((fn.nslots * 8) + 15) & ~15);
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    sub sp, sp, #" + std::to_string(frame) + "\n";
    auto slot = [](uint32_t id) { return std::to_string(8 * id); };
    for (const Insn& in : fn.code) {
      bool wide = in.atom->bits == 64;
      const char* reg = wide ? "x8" : "w8";
      const char* reg2 = wide ? "x9" : "w9";
      const char* store = wide ? "x8" : "w8";
      switch (in.op) {
        case Op::Imm:
          out += imm_mov(wide, static_cast<int64_t>(in.imm));
          out += "    str " + std::string(store) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Mov:
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Add:
        case Op::Sub:
        case Op::Mul: {
          const char* how = in.op == Op::Add ? "add" : in.op == Op::Sub ? "sub" : "mul";
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    ldr " + std::string(reg2) + ", [sp, #" + slot(in.rhs) + "]\n";
          out += "    " + std::string(how) + " " + reg + ", " + reg + ", " + reg2 + "\n";
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        }
        case Op::Div:
          throw Fail("arm64", "division is in the bytecode; the arm64 row is not written yet", in.line, 0);
        case Op::Ret: {
          const char* ret = wide ? "x0" : "w0";
          out += "    ldr " + std::string(ret) + ", [sp, #" + slot(in.dst) + "]\n";
          out += "    add sp, sp, #" + std::to_string(frame) + "\n";
          out += "    ret\n";
          break;
        }
      }
    }
  }
  return out;
}

}  // namespace tp
