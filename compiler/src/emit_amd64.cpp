#include "tp/arch.hpp"

namespace tp {

std::string emit_amd64(const Program& prog) {
  std::string out = "# target amd64\n# ";
  out += stage_info("amd64").does;
  out += "\n    .text\n";
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) arch_require(Target::Amd64, in);
    int frame = static_cast<int>(((fn.nslots * 8) + 15) & ~15);
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    push %rbp\n    mov %rsp, %rbp\n";
    out += "    sub $" + std::to_string(frame) + ", %rsp\n";
    auto slot = [](uint32_t id) { return "-" + std::to_string(8 * (id + 1)) + "(%rbp)"; };
    for (const Insn& in : fn.code) {
      bool wide = in.atom->bits == 64;
      const char* reg = wide ? "%rax" : "%eax";
      switch (in.op) {
        case Op::Imm:
          if (wide) out += "    movabs $" + std::to_string(static_cast<int64_t>(in.imm)) + ", %rax\n";
          else out += "    mov $" + std::to_string(static_cast<int32_t>(in.imm)) + ", %eax\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::Mov:
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::Add:
        case Op::Sub:
        case Op::Mul: {
          const char* how = in.op == Op::Add ? "add" : in.op == Op::Sub ? "sub" : "imul";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += "    " + std::string(how) + " " + slot(in.rhs) + ", " + reg + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        }
        case Op::Div:
          throw Fail("amd64", "division is in the bytecode; the amd64 row is not written yet", in.line, 0);
        case Op::Ret:
          out += "    mov " + slot(in.dst) + ", " + reg + "\n";
          out += "    leave\n    ret\n";
          break;
      }
    }
  }
  out += "    .section .note.GNU-stack,\"\",@progbits\n";
  return out;
}

}  // namespace tp
