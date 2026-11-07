#include "tp/arch.hpp"

namespace tp {
namespace {

std::string cxx_type(const Atom& atom) {
  switch (atom.family) {
    case Family::SignedInt:
      if (atom.bits == 32) return "int32_t";
      if (atom.bits == 64) return "int64_t";
      break;
    case Family::UnsignedInt:
      if (atom.bits == 32) return "uint32_t";
      if (atom.bits == 64) return "uint64_t";
      break;
    default:
      break;
  }
  return "";
}

std::string slot(uint32_t id) { return "t" + std::to_string(id); }

std::string wrapped(const char* how, const Insn& in) {
  std::string ty = cxx_type(*in.atom);
  std::string raw = ty == "int32_t" ? "uint32_t" : ty == "int64_t" ? "uint64_t" : ty;
  return "static_cast<" + ty + ">(static_cast<" + raw + ">(" + slot(in.lhs) + ") " + how + " static_cast<" + raw + ">(" + slot(in.rhs) + "))";
}

}  // namespace

std::string emit_cpp(const Program& prog) {
  std::string out = "// target cpp\n// ";
  out += stage_info("cpp").does;
  out += "\n#include <cstdint>\n";
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) arch_require(Target::Cpp, in);
    std::string ret = fn.shape.outputs.empty() ? "void" : cxx_type(*fn.shape.outputs[0].atom);
    out += ret + " " + fn.shape.name + "(";
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      if (i) out += ", ";
      out += cxx_type(*fn.shape.inputs[i].atom) + " " + fn.shape.inputs[i].name;
    }
    out += ") {\n";
    std::vector<const Atom*> slot_atom(fn.nslots, nullptr);
    for (const Insn& in : fn.code) {
      if (in.op != Op::Ret) slot_atom[in.dst] = in.atom;
    }
    for (uint32_t s = 0; s < fn.nslots; ++s) {
      const Atom* atom = slot_atom[s];
      std::string ty = atom ? cxx_type(*atom) : "int32_t";
      std::string init = "0";
      if (s < fn.shape.inputs.size()) init = fn.shape.inputs[s].name;
      out += "  " + ty + " " + slot(s) + " = " + init + ";\n";
    }
    for (const Insn& in : fn.code) {
      switch (in.op) {
        case Op::Imm:
          out += "  " + slot(in.dst) + " = " + std::to_string(static_cast<int64_t>(in.imm)) + ";\n";
          break;
        case Op::Mov:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + ";\n";
          break;
        case Op::Add:
          out += "  " + slot(in.dst) + " = " + wrapped("+", in) + ";\n";
          break;
        case Op::Sub:
          out += "  " + slot(in.dst) + " = " + wrapped("-", in) + ";\n";
          break;
        case Op::Mul:
          out += "  " + slot(in.dst) + " = " + wrapped("*", in) + ";\n";
          break;
        case Op::Div:
          throw Fail("cpp", "division is in the bytecode; the C++ row is not written yet", in.line, 0);
        case Op::Ret:
          out += "  return " + slot(in.dst) + ";\n";
          break;
      }
    }
    out += "}\n";
  }
  return out;
}

}  // namespace tp
