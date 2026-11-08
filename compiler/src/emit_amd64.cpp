#include "tp/arch.hpp"

namespace tp {

std::string emit_amd64(const Program& prog) {
  std::string out = "# target amd64\n# ";
  out += stage_info("amd64").does;
  out += "\n    .text\n";
  bool needs_fail = false;
  int chk = 0;
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) arch_require(Target::Amd64, in);
    int frame = static_cast<int>(((fn.nslots * 8) + 15) & ~15);
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    push %rbp\n    mov %rsp, %rbp\n";
    out += "    sub $" + std::to_string(frame) + ", %rsp\n";
    auto slot = [](uint32_t id) { return "-" + std::to_string(8 * (id + 1)) + "(%rbp)"; };
    auto lab = [&](uint64_t id) { return ".L" + fn.shape.name + "_" + std::to_string(id); };
    static const char* r32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
    static const char* r64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    if (fn.shape.inputs.size() > 6) throw Fail("amd64", "more than 6 arguments", 0, 0);
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      const Atom* atom = fn.shape.inputs[i].atom;
      bool ptr = atom && atom->family == Family::Pointer;
      if (!atom || (!ptr && (atom->family != Family::SignedInt || (atom->bits != 32 && atom->bits != 64)))) {
        throw Fail("amd64", "no abi row for this argument", 0, 0);
      }
      const char* from = (ptr || atom->bits == 64) ? r64[i] : r32[i];
      out += "    mov " + std::string(from) + ", " + slot(static_cast<uint32_t>(i)) + "\n";
    }
    for (const Insn& in : fn.code) {
      if (in.op == Op::Label) {
        out += lab(in.imm) + ":\n";
        continue;
      }
      if (in.op == Op::Jmp) {
        out += "    jmp " + lab(in.imm) + "\n";
        continue;
      }
      if (in.op == Op::Br) {
        out += "    cmpl $0, " + slot(in.lhs) + "\n";
        out += "    je " + lab(in.imm) + "\n";
        continue;
      }
      bool wide = in.atom && in.atom->bits == 64;
      const char* reg = wide ? "%rax" : "%eax";
      auto binary = [&](const char* how) {
        out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
        out += "    " + std::string(how) + " " + slot(in.rhs) + ", " + reg + "\n";
        out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
      };
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
          binary("add");
          break;
        case Op::Sub:
          binary("sub");
          break;
        case Op::Mul:
          binary("imul");
          break;
        case Op::And:
          binary("and");
          break;
        case Op::Or:
          binary("or");
          break;
        case Op::Xor:
          binary("xor");
          break;
        case Op::Div:
        case Op::Rem: {
          bool srcwide = in.atom->bits == 64;
          needs_fail = true;
          std::string skip = ".Lchk" + std::to_string(chk++);
          out += "    cmp" + std::string(srcwide ? "q" : "l") + " $0, " + slot(in.rhs) + "\n";
          out += "    je .Ltp_fail\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += "    cmp" + std::string(srcwide ? "q" : "l") + " $-1, " + slot(in.rhs) + "\n";
          out += "    jne " + skip + "\n";
          if (srcwide) out += "    movabs $-9223372036854775808, %r11\n    cmp %r11, %rax\n";
          else out += "    cmpl $-2147483648, %eax\n";
          out += "    je .Ltp_fail\n";
          out += skip + ":\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += srcwide ? "    cqo\n    idivq " : "    cdq\n    idivl ";
          out += slot(in.rhs) + "\n";
          out += "    mov " + std::string(in.op == Op::Div ? reg : (srcwide ? "%rdx" : "%edx")) + ", " + slot(in.dst) + "\n";
          break;
        }
        case Op::Shl:
        case Op::Shr:
          needs_fail = true;
          out += "    mov " + slot(in.rhs) + ", %ecx\n";
          out += "    cmpl $" + std::to_string(in.atom->bits) + ", %ecx\n";
          out += "    jae .Ltp_fail\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += in.op == Op::Shl ? "    shl %cl, " : "    sar %cl, ";
          out += std::string(reg) + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::Neg:
          needs_fail = true;
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          if (wide) out += "    movabs $-9223372036854775808, %r11\n    cmp %r11, %rax\n";
          else out += "    cmpl $-2147483648, %eax\n";
          out += "    je .Ltp_fail\n";
          out += "    neg " + std::string(reg) + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::BrRange: {
          uint64_t mask = wide ? ~0ull : 0xffffffffull;
          uint64_t lo = in.imm_lo & mask;
          uint64_t span = (in.imm_hi - in.imm_lo) & mask;
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          if (wide) {
            out += "    movabs $" + std::to_string(static_cast<int64_t>(lo)) + ", %r11\n";
            out += "    sub %r11, %rax\n";
            out += "    movabs $" + std::to_string(static_cast<int64_t>(span)) + ", %r11\n";
            out += "    cmp %r11, %rax\n";
          } else {
            out += "    subl $" + std::to_string(static_cast<int32_t>(lo)) + ", %eax\n";
            out += "    cmpl $" + std::to_string(static_cast<int32_t>(span)) + ", %eax\n";
          }
          out += "    ja " + lab(in.imm) + "\n";
          break;
        }
        case Op::Call: {
          const BcFunc& callee = prog.fns[in.imm];
          if (in.args.size() > 6) throw Fail("amd64", "more than 6 arguments", in.line, 0);
          if (in.args.size() != callee.shape.inputs.size()) throw Fail("amd64", "argument count", in.line, 0);
          for (size_t i = 0; i < in.args.size(); ++i) {
            bool argwide = callee.shape.inputs[i].atom->bits == 64 || callee.shape.inputs[i].atom->family == Family::Pointer;
            const char* to = argwide ? r64[i] : r32[i];
            out += "    mov " + slot(in.args[i]) + ", " + to + "\n";
          }
          out += "    call " + callee.shape.name + "\n";
          if (in.atom && in.dst < fn.nslots) {
            bool retwide = in.atom->bits == 64 || in.atom->family == Family::Pointer;
            out += "    mov " + std::string(retwide ? "%rax" : "%eax") + ", " + slot(in.dst) + "\n";
          }
          break;
        }
        case Op::Threads:
          needs_fail = true;
          out += "    mov $84, %edi\n    call sysconf@PLT\n    cmp $1, %rax\n    jl .Ltp_fail\n";
          out += "    mov %eax, " + slot(in.dst) + "\n";
          break;
        case Op::Freemem:
          needs_fail = true;
          out += "    sub $16, %rsp\n    mov $30, %edi\n    call sysconf@PLT\n    mov %rax, (%rsp)\n";
          out += "    mov $86, %edi\n    call sysconf@PLT\n    mov (%rsp), %rcx\n    add $16, %rsp\n";
          out += "    cmp $0, %rax\n    jl .Ltp_fail\n    cmp $0, %rcx\n    jl .Ltp_fail\n";
          out += "    imul %rcx, %rax\n    mov %rax, " + slot(in.dst) + "\n";
          break;
        case Op::Addr:
          out += "    lea " + slot(in.lhs) + ", %rax\n    mov %rax, " + slot(in.dst) + "\n";
          break;
        case Op::Load: {
          bool widev = in.atom && in.atom->bits == 64;
          out += "    mov " + slot(in.lhs) + ", %rax\n";
          out += widev ? "    mov (%rax), %rax\n" : "    mov (%rax), %eax\n";
          out += "    mov " + std::string(widev ? "%rax" : "%eax") + ", " + slot(in.dst) + "\n";
          break;
        }
        case Op::Store: {
          bool widev = in.atom && in.atom->bits == 64;
          out += "    mov " + slot(in.lhs) + ", %rax\n";
          out += "    mov " + slot(in.rhs) + ", " + (widev ? "%rcx" : "%ecx") + "\n";
          out += widev ? "    mov %rcx, (%rax)\n" : "    mov %ecx, (%rax)\n";
          break;
        }
        case Op::Eq:
        case Op::Ne:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge:
        case Op::Not: {
          bool srcwide = in.src && in.src->bits == 64;
          const char* sreg = srcwide ? "%rax" : "%eax";
          if (in.op == Op::Not) {
            out += "    cmpl $0, " + slot(in.lhs) + "\n";
          } else {
            out += "    mov " + slot(in.lhs) + ", " + sreg + "\n";
            out += "    cmp " + slot(in.rhs) + ", " + sreg + "\n";
          }
          const char* set = "sete";
          if (in.op == Op::Ne) set = "setne";
          if (in.op == Op::Lt) set = "setl";
          if (in.op == Op::Le) set = "setle";
          if (in.op == Op::Gt) set = "setg";
          if (in.op == Op::Ge) set = "setge";
          out += "    " + std::string(set) + " %al\n    movzbl %al, %eax\n";
          out += "    mov %eax, " + slot(in.dst) + "\n";
          break;
        }
        case Op::Ret:
          out += "    mov " + slot(in.dst) + ", " + reg + "\n";
          out += "    leave\n    ret\n";
          break;
        case Op::Jmp:
        case Op::Br:
        case Op::Label:
          break;
        default:
          throw Fail("amd64", "no row for this opcode yet", in.line, 0);
      }
    }
    if (fn.shape.outputs.empty()) out += "    leave\n    ret\n";
  }
  if (needs_fail) out += ".Ltp_fail:\n    mov $1, %edi\n    call exit@PLT\n";
  out += "    .section .note.GNU-stack,\"\",@progbits\n";
  return out;
}

}  // namespace tp
