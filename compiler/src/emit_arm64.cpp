#include "tp/arch.hpp"

namespace tp {
namespace {

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
    for (const Insn& in : fn.code) arch_require(Target::Arm64, in);
    int frame = static_cast<int>(((fn.nslots * 8) + 15) & ~15);
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    sub sp, sp, #" + std::to_string(frame) + "\n";
    auto slot = [](uint32_t id) { return std::to_string(8 * id); };
    auto lab = [&](uint64_t id) { return ".L" + fn.shape.name + "_" + std::to_string(id); };
    if (fn.shape.inputs.size() > 8) throw Fail("arm64", "more than 8 arguments", 0, 0);
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      const Atom* atom = fn.shape.inputs[i].atom;
      bool ptr = atom && atom->family == Family::Pointer;
      if (!atom || (!ptr && (atom->family != Family::SignedInt || (atom->bits != 32 && atom->bits != 64)))) {
        throw Fail("arm64", "no abi row for this argument", 0, 0);
      }
      std::string from = ((ptr || atom->bits == 64) ? "x" : "w") + std::to_string(i);
      out += "    str " + from + ", [sp, #" + slot(static_cast<uint32_t>(i)) + "]\n";
    }
    for (const Insn& in : fn.code) {
      if (in.op == Op::Label) {
        out += lab(in.imm) + ":\n";
        continue;
      }
      if (in.op == Op::Jmp) {
        out += "    b " + lab(in.imm) + "\n";
        continue;
      }
      if (in.op == Op::Br) {
        out += "    ldr w8, [sp, #" + slot(in.lhs) + "]\n";
        out += "    cbz w8, " + lab(in.imm) + "\n";
        continue;
      }
      bool wide = in.atom && in.atom->bits == 64;
      const char* reg = wide ? "x8" : "w8";
      const char* reg2 = wide ? "x9" : "w9";
      auto binary = [&](const char* how) {
        out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
        out += "    ldr " + std::string(reg2) + ", [sp, #" + slot(in.rhs) + "]\n";
        out += "    " + std::string(how) + " " + reg + ", " + reg + ", " + reg2 + "\n";
        out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
      };
      switch (in.op) {
        case Op::Imm:
          out += imm_mov(wide, static_cast<int64_t>(in.imm));
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Mov:
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Add:
          binary("add");
          break;
        case Op::Sub:
          binary("sub");
          break;
        case Op::Mul:
          binary("mul");
          break;
        case Op::And:
          binary("and");
          break;
        case Op::Or:
          binary("orr");
          break;
        case Op::Xor:
          binary("eor");
          break;
        case Op::Div:
          binary("sdiv");
          break;
        case Op::Rem:
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    ldr " + std::string(reg2) + ", [sp, #" + slot(in.rhs) + "]\n";
          out += "    sdiv " + std::string(wide ? "x10" : "w10") + ", " + reg + ", " + reg2 + "\n";
          out += "    msub " + std::string(reg) + ", " + (wide ? "x10" : "w10") + ", " + reg2 + ", " + reg + "\n";
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Shl:
        case Op::Shr:
          out += "    ldr w9, [sp, #" + slot(in.rhs) + "]\n";
          out += "    cmp w9, #" + std::to_string(in.atom->bits) + "\n";
          out += "    b.hs .Ltp_fail\n";
          binary(in.op == Op::Shl ? "lsl" : "asr");
          break;
        case Op::Neg:
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    neg " + std::string(reg) + ", " + reg + "\n";
          out += "    str " + std::string(reg) + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::BrRange:
          out += "    ldr " + std::string(reg) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    sub " + std::string(reg) + ", " + reg + ", #" + std::to_string(static_cast<int64_t>(in.imm_lo) & (wide ? 0xfff : 0xfff)) + "\n";
          out += "    cmp " + std::string(reg) + ", #" + std::to_string(static_cast<uint64_t>(in.imm_hi - in.imm_lo) & 0xfff) + "\n";
          out += "    b.hi " + lab(in.imm) + "\n";
          break;
        case Op::Call: {
          const BcFunc& callee = prog.fns[in.imm];
          if (in.args.size() > 8) throw Fail("arm64", "more than 8 arguments", in.line, 0);
          for (size_t i = 0; i < in.args.size(); ++i) {
            const Atom* arg = callee.shape.inputs[i].atom;
            bool argwide = arg && (arg->bits == 64 || arg->family == Family::Pointer);
            out += "    ldr " + std::string(argwide ? "x" : "w") + std::to_string(i) + ", [sp, #" + slot(in.args[i]) + "]\n";
          }
          out += "    bl " + callee.shape.name + "\n";
          if (in.atom && in.dst < fn.nslots) {
            bool retwide = in.atom->bits == 64 || in.atom->family == Family::Pointer;
            out += "    str " + std::string(retwide ? "x0" : "w0") + ", [sp, #" + slot(in.dst) + "]\n";
          }
          break;
        }
        case Op::Threads:
          out += "    mov w0, #84\n    bl sysconf\n    str w0, [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Freemem:
          out += "    mov w0, #30\n    bl sysconf\n    mov x1, x0\n    mov w0, #86\n    bl sysconf\n    mul x0, x0, x1\n";
          out += "    str x0, [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Not:
          out += "    ldr w8, [sp, #" + slot(in.lhs) + "]\n";
          out += "    cmp w8, #0\n    cset w8, eq\n";
          out += "    str w8, [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Eq:
        case Op::Ne:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge: {
          bool srcwide = in.src && in.src->bits == 64;
          const char* a = srcwide ? "x8" : "w8";
          const char* b = srcwide ? "x9" : "w9";
          const char* cc = "eq";
          if (in.op == Op::Ne) cc = "ne";
          if (in.op == Op::Lt) cc = "lt";
          if (in.op == Op::Le) cc = "le";
          if (in.op == Op::Gt) cc = "gt";
          if (in.op == Op::Ge) cc = "ge";
          out += "    ldr " + std::string(a) + ", [sp, #" + slot(in.lhs) + "]\n";
          out += "    ldr " + std::string(b) + ", [sp, #" + slot(in.rhs) + "]\n";
          out += "    cmp " + std::string(a) + ", " + b + "\n";
          out += "    cset w8, " + std::string(cc) + "\n";
          out += "    str w8, [sp, #" + slot(in.dst) + "]\n";
          break;
        }
        case Op::Addr:
          out += "    add " + std::string(wide ? "x8" : "x8") + ", sp, #" + slot(in.lhs) + "\n";
          out += "    str x8, [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Load:
          out += "    ldr x8, [sp, #" + slot(in.lhs) + "]\n";
          out += std::string(wide ? "    ldr x8, [x8]\n" : "    ldr w8, [x8]\n");
          out += "    str " + std::string(wide ? "x8" : "w8") + ", [sp, #" + slot(in.dst) + "]\n";
          break;
        case Op::Store:
          out += "    ldr x8, [sp, #" + slot(in.lhs) + "]\n";
          out += "    ldr " + std::string(wide ? "x9" : "w9") + ", [sp, #" + slot(in.rhs) + "]\n";
          out += std::string(wide ? "    str x9, [x8]\n" : "    str w9, [x8]\n");
          break;
        case Op::Ret: {
          const char* ret = wide ? "x0" : "w0";
          out += "    ldr " + std::string(ret) + ", [sp, #" + slot(in.dst) + "]\n";
          out += "    add sp, sp, #" + std::to_string(frame) + "\n";
          out += "    ret\n";
          break;
        }
        case Op::Jmp:
        case Op::Br:
        case Op::Label:
          break;
      }
    }
    if (fn.shape.outputs.empty()) {
      out += "    add sp, sp, #" + std::to_string(frame) + "\n";
      out += "    ret\n";
    }
  }
  return out;
}

}  // namespace tp
