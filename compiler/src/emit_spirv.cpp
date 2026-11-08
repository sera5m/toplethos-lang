#include "tp/arch.hpp"

namespace tp {
namespace {

std::string id(uint32_t slot) { return "%" + std::to_string(slot + 1); }

}  // namespace

std::string emit_spirv(const Program& prog) {
  std::string out = "; target spirv\n; ";
  out += stage_info("spirv").does;
  out += "\n; Vulkan loads this module. It does not switch on these opcodes.\n";
  for (const BcFunc& fn : prog.fns) {
    if (!fn.shape.inputs.empty()) {
      throw Fail("spirv", "a parameter has no SPIR-V calling convention yet", 0, 0);
    }
    for (const Insn& in : fn.code) arch_require(Target::Spirv, in);
    out += "; " + shape_fn(fn.shape) + "\n";
    bool saw32 = false;
    bool saw64 = false;
    bool saw_bool = false;
    for (const Insn& in : fn.code) {
      if (in.op == Op::BrRange) saw_bool = true;
      if (!in.atom) continue;
      if (in.atom->name == "bool") saw_bool = true;
      if (in.atom->bits == 32 && in.atom->name != "bool") saw32 = true;
      if (in.atom->bits == 64) saw64 = true;
      if (in.src && in.src->bits == 32) saw32 = true;
      if (in.src && in.src->bits == 64) saw64 = true;
    }
    if (saw_bool) out += "%bool = OpTypeBool\n";
    if (saw32) out += "%i32 = OpTypeInt 32 1\n";
    if (saw64) out += "%i64 = OpTypeInt 64 1\n";
    int seq = 0;
    auto lab = [&](uint64_t id) { return "%L" + fn.shape.name + "_" + std::to_string(id); };
    auto nxt = [&]() { return "%N" + fn.shape.name + "_" + std::to_string(seq++); };
    for (const Insn& in : fn.code) {
      if (in.op == Op::Label) {
        out += lab(in.imm) + " = OpLabel\n";
        continue;
      }
      if (in.op == Op::Jmp) {
        out += "OpBranch " + lab(in.imm) + "\n";
        continue;
      }
      if (in.op == Op::Br) {
        std::string next = nxt();
        out += "OpBranchConditional " + id(in.lhs) + " " + next + " " + lab(in.imm) + "\n";
        out += next + " = OpLabel\n";
        continue;
      }
      if (in.op == Op::BrRange) {
        bool wide = in.atom->bits == 64;
        std::string ty = wide ? "%i64" : "%i32";
        uint64_t mask = wide ? ~0ull : 0xffffffffull;
        int64_t lo = static_cast<int64_t>(in.imm_lo & mask);
        int64_t span = static_cast<int64_t>((in.imm_hi - in.imm_lo) & mask);
        if (!wide) {
          lo = static_cast<int32_t>(in.imm_lo);
          span = static_cast<int32_t>((in.imm_hi - in.imm_lo) & mask);
        }
        std::string a = nxt();
        std::string b = nxt();
        std::string diff = nxt();
        std::string cmp = nxt();
        std::string next = nxt();
        out += a + " = OpConstant " + ty + " " + std::to_string(lo) + "\n";
        out += b + " = OpConstant " + ty + " " + std::to_string(span) + "\n";
        out += diff + " = OpISub " + ty + " " + id(in.lhs) + " " + a + "\n";
        out += cmp + " = OpUGreaterThan %bool " + diff + " " + b + "\n";
        out += "OpBranchConditional " + cmp + " " + lab(in.imm) + " " + next + "\n";
        out += next + " = OpLabel\n";
        continue;
      }
      std::string ty = "%i32";
      if (in.atom && in.atom->name == "bool") ty = "%bool";
      else if (in.src && in.src->bits == 64) ty = "%i64";
      else if (in.atom && in.atom->bits == 64) ty = "%i64";
      auto bin = [&](const char* spv) {
        std::string sty = in.src && in.src->bits == 64 ? "%i64" : (in.atom && in.atom->name == "bool" ? "%bool" : ty);
        if (in.src) sty = in.src->bits == 64 ? "%i64" : "%i32";
        out += id(in.dst) + " = " + spv + " " + (in.atom && in.atom->name == "bool" ? std::string("%bool") : sty) + " " +
               id(in.lhs) + " " + id(in.rhs) + "\n";
      };
      switch (in.op) {
        case Op::Imm:
          out += id(in.dst) + " = OpConstant " + ty + " " + std::to_string(static_cast<int64_t>(in.imm)) + "\n";
          break;
        case Op::Mov:
          out += id(in.dst) + " = OpCopyObject " + ty + " " + id(in.lhs) + "\n";
          break;
        case Op::Add:
          bin("OpIAdd");
          break;
        case Op::Sub:
          bin("OpISub");
          break;
        case Op::Mul:
          bin("OpIMul");
          break;
        case Op::Div:
          bin("OpSDiv");
          break;
        case Op::Rem:
          bin("OpSRem");
          break;
        case Op::And:
          bin("OpBitwiseAnd");
          break;
        case Op::Or:
          bin("OpBitwiseOr");
          break;
        case Op::Xor:
          bin("OpBitwiseXor");
          break;
        case Op::Shl:
          bin("OpShiftLeftLogical");
          break;
        case Op::Shr:
          bin("OpShiftRightArithmetic");
          break;
        case Op::Eq:
          bin("OpIEqual");
          break;
        case Op::Ne:
          bin("OpINotEqual");
          break;
        case Op::Lt:
          bin("OpSLessThan");
          break;
        case Op::Le:
          bin("OpSLessThanEqual");
          break;
        case Op::Gt:
          bin("OpSGreaterThan");
          break;
        case Op::Ge:
          bin("OpSGreaterThanEqual");
          break;
        case Op::Not:
          out += id(in.dst) + " = OpLogicalNot %bool " + id(in.lhs) + "\n";
          break;
        case Op::Neg:
          out += id(in.dst) + " = OpSNeg " + ty + " " + id(in.lhs) + "\n";
          break;
        case Op::Call:
        case Op::Threads:
        case Op::Freemem:
          throw Fail("spirv", "a shader cannot ask the host or call with a host convention", in.line, 0);
        case Op::Addr:
        case Op::Load:
        case Op::Store:
          throw Fail("spirv", "a shader does not keep a host pointer", in.line, 0);
        case Op::Ret:
          out += "OpReturnValue " + id(in.dst) + "\n";
          break;
        case Op::Jmp:
        case Op::Br:
        case Op::BrRange:
        case Op::Label:
          break;
        default:
          throw Fail("spirv", "no row for this opcode yet", in.line, 0);
      }
    }
  }
  return out;
}

}  // namespace tp
