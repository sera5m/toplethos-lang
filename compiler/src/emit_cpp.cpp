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
    case Family::Bool:
      return "bool";
    case Family::Pointer: {
      if (!atom.pointee) return "";
      std::string inner = cxx_type(*atom.pointee);
      if (inner.empty()) return "";
      return inner + "*";
    }
    default:
      break;
  }
  return "";
}

std::string slot(uint32_t id) { return "t" + std::to_string(id); }

std::string cxx_name(const BcFunc& fn) {
  if (fn.shape.name != "main") return fn.shape.name;
  if (fn.shape.outputs.empty()) return "tp_entry";
  if (cxx_type(*fn.shape.outputs[0].atom) != "int32_t") return "tp_entry";
  return "main";
}

std::string wrapped(const char* how, const Insn& in) {
  std::string ty = cxx_type(*in.atom);
  std::string raw = ty == "int32_t" ? "uint32_t" : ty == "int64_t" ? "uint64_t" : ty;
  return "static_cast<" + ty + ">(static_cast<" + raw + ">(" + slot(in.lhs) + ") " + how + " static_cast<" + raw + ">(" + slot(in.rhs) + "))";
}

}  // namespace

std::string emit_cpp(const Program& prog) {
  std::string out = "// target cpp\n// ";
  out += stage_info("cpp").does;
  out += "\n#include <cstdint>\n#include <cstdlib>\n#include <unistd.h>\n";
  auto proto = [](const BcFunc& fn) {
    std::string ret = fn.shape.outputs.empty() ? "void" : cxx_type(*fn.shape.outputs[0].atom);
    std::string line = ret + " " + cxx_name(fn) + "(";
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      if (i) line += ", ";
      line += cxx_type(*fn.shape.inputs[i].atom) + " " + fn.shape.inputs[i].name;
    }
    line += ");\n";
    return line;
  };
  for (const BcFunc& fn : prog.fns) out += proto(fn);
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) arch_require(Target::Cpp, in);
    std::string ret = fn.shape.outputs.empty() ? "void" : cxx_type(*fn.shape.outputs[0].atom);
    out += ret + " " + cxx_name(fn) + "(";
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      if (i) out += ", ";
      out += cxx_type(*fn.shape.inputs[i].atom) + " " + fn.shape.inputs[i].name;
    }
    out += ") {\n";
    std::vector<const Atom*> slot_atom(fn.nslots, nullptr);
    for (size_t i = 0; i < fn.shape.inputs.size() && i < fn.nslots; ++i) slot_atom[i] = fn.shape.inputs[i].atom;
    for (const Insn& in : fn.code) {
      if (in.op == Op::Ret || in.op == Op::Jmp || in.op == Op::Br || in.op == Op::Label) continue;
      if (in.dst < fn.nslots) slot_atom[in.dst] = in.atom;
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
        case Op::And:
          out += "  " + slot(in.dst) + " = " + wrapped("&", in) + ";\n";
          break;
        case Op::Or:
          out += "  " + slot(in.dst) + " = " + wrapped("|", in) + ";\n";
          break;
        case Op::Xor:
          out += "  " + slot(in.dst) + " = " + wrapped("^", in) + ";\n";
          break;
        case Op::Div:
        case Op::Rem: {
          std::string ty = cxx_type(*in.atom);
          std::string zero = in.atom->family == Family::UnsignedInt ? "0u" : "0";
          out += "  if (" + slot(in.rhs) + " == " + zero;
          if (in.atom->family == Family::SignedInt) {
            out += " || (" + slot(in.lhs) + " == " + (in.atom->bits == 64 ? "INT64_MIN" : "INT32_MIN") + " && " + slot(in.rhs) +
                   " == " + (in.atom->bits == 64 ? "(-1LL)" : "(-1)") + ")";
          }
          out += ") abort();\n";
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + (in.op == Op::Div ? " / " : " % ") + slot(in.rhs) + ";\n";
          break;
        }
        case Op::Shl:
        case Op::Shr:
          out += "  if (static_cast<uint32_t>(" + slot(in.rhs) + ") >= " + std::to_string(in.atom->bits) + "u) abort();\n";
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + (in.op == Op::Shl ? " << " : " >> ") + slot(in.rhs) + ";\n";
          break;
        case Op::Neg:
          out += "  if (" + slot(in.lhs) + " == " + (in.atom->bits == 64 ? "INT64_MIN" : "INT32_MIN") + ") abort();\n";
          out += "  " + slot(in.dst) + " = -" + slot(in.lhs) + ";\n";
          break;
        case Op::BrRange: {
          bool wide = in.atom->bits == 64;
          std::string u = wide ? "uint64_t" : "uint32_t";
          std::string lo = std::to_string(static_cast<int64_t>(in.imm_lo));
          std::string hi = std::to_string(static_cast<int64_t>(in.imm_hi));
          out += "  if (static_cast<" + u + ">(" + slot(in.lhs) + ") - static_cast<" + u + ">(" + lo + ") > static_cast<" + u + ">(" +
                 hi + ") - static_cast<" + u + ">(" + lo + ")) goto L" + std::to_string(in.imm) + ";\n";
          break;
        }
        case Op::Call: {
          const BcFunc& callee = prog.fns[in.imm];
          std::string call = cxx_name(callee) + "(";
          for (size_t i = 0; i < in.args.size(); ++i) {
            if (i) call += ", ";
            call += slot(in.args[i]);
          }
          call += ")";
          if (in.atom && in.dst < fn.nslots) out += "  " + slot(in.dst) + " = " + call + ";\n";
          else out += "  " + call + ";\n";
          break;
        }
        case Op::Threads:
          out += "  {\n    long tp_n = sysconf(_SC_NPROCESSORS_ONLN);\n";
          out += "    if (tp_n < 1) abort();\n";
          out += "    " + slot(in.dst) + " = static_cast<int32_t>(tp_n);\n  }\n";
          break;
        case Op::Freemem:
          out += "  {\n    long tp_pages = sysconf(_SC_AVPHYS_PAGES);\n    long tp_page = sysconf(_SC_PAGESIZE);\n";
          out += "    if (tp_pages < 0 || tp_page < 0) abort();\n";
          out += "    " + slot(in.dst) + " = static_cast<int64_t>(tp_pages) * static_cast<int64_t>(tp_page);\n  }\n";
          break;
        case Op::Addr:
          out += "  " + slot(in.dst) + " = &" + slot(in.lhs) + ";\n";
          break;
        case Op::Load:
          out += "  " + slot(in.dst) + " = *" + slot(in.lhs) + ";\n";
          break;
        case Op::Store:
          out += "  *" + slot(in.lhs) + " = " + slot(in.rhs) + ";\n";
          break;
        case Op::Not:
          out += "  " + slot(in.dst) + " = !" + slot(in.lhs) + ";\n";
          break;
        case Op::Eq:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " == " + slot(in.rhs) + ";\n";
          break;
        case Op::Ne:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " != " + slot(in.rhs) + ";\n";
          break;
        case Op::Lt:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " < " + slot(in.rhs) + ";\n";
          break;
        case Op::Le:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " <= " + slot(in.rhs) + ";\n";
          break;
        case Op::Gt:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " > " + slot(in.rhs) + ";\n";
          break;
        case Op::Ge:
          out += "  " + slot(in.dst) + " = " + slot(in.lhs) + " >= " + slot(in.rhs) + ";\n";
          break;
        case Op::Jmp:
          out += "  goto L" + std::to_string(in.imm) + ";\n";
          break;
        case Op::Br:
          out += "  if (!" + slot(in.lhs) + ") goto L" + std::to_string(in.imm) + ";\n";
          break;
        case Op::Label:
          out += "L" + std::to_string(in.imm) + ":;\n";
          break;
        case Op::Ret:
          out += "  return " + slot(in.dst) + ";\n";
          break;
        default:
          throw Fail("cpp", "no row for this opcode yet", in.line, 0);
      }
    }
    out += "}\n";
  }
  for (const BcFunc& fn : prog.fns) {
    if (cxx_name(fn) != "tp_entry") continue;
    if (fn.shape.outputs.empty()) out += "int main() { tp_entry(); return 0; }\n";
    else out += "int main() { return static_cast<int>(tp_entry()); }\n";
    break;
  }
  return out;
}

}  // namespace tp
