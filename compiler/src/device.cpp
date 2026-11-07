#include "tp/bytecode.hpp"

#include <cstring>

namespace tp {
namespace {

uint64_t mask_bits(int bits) {
  if (bits >= 64) return ~0ull;
  return (1ull << bits) - 1ull;
}

uint64_t wrap_add(uint64_t a, uint64_t b, const Atom& atom, bool sub) {
  switch (atom.family) {
    case Family::SignedInt:
    case Family::UnsignedInt: {
      uint64_t m = mask_bits(atom.bits);
      uint64_t x = a & m;
      uint64_t y = b & m;
      uint64_t r = sub ? (x - y) : (x + y);
      return r & m;
    }
    case Family::Float:
      if (atom.name != "f64") throw Fail("interpret", "no device row for " + atom.name + " yet", 0, 0);
      {
        double da, db;
        std::memcpy(&da, &a, 8);
        std::memcpy(&db, &b, 8);
        double r = sub ? da - db : da + db;
        uint64_t bits = 0;
        std::memcpy(&bits, &r, 8);
        return bits;
      }
    default:
      throw Fail("interpret", "no device row for " + atom.name, 0, 0);
  }
}

uint64_t wrap_mul(uint64_t a, uint64_t b, const Atom& atom) {
  switch (atom.family) {
    case Family::SignedInt:
    case Family::UnsignedInt:
      return (a * b) & mask_bits(atom.bits);
    case Family::Float:
      if (atom.name != "f64") throw Fail("interpret", "no device row for " + atom.name + " yet", 0, 0);
      {
        double da, db;
        std::memcpy(&da, &a, 8);
        std::memcpy(&db, &b, 8);
        double r = da * db;
        uint64_t bits = 0;
        std::memcpy(&bits, &r, 8);
        return bits;
      }
    default:
      throw Fail("interpret", "no device row for " + atom.name, 0, 0);
  }
}

uint64_t wrap_div(uint64_t a, uint64_t b, const Atom& atom) {
  switch (atom.family) {
    case Family::SignedInt: {
      if (b == 0) throw Fail("interpret", "division by zero", 0, 0);
      int64_t da = static_cast<int64_t>(a);
      int64_t db = static_cast<int64_t>(b);
      if (atom.bits < 64) {
        int64_t sign = 1ll << (atom.bits - 1);
        if (da & sign) da |= ~mask_bits(atom.bits);
        if (db & sign) db |= ~mask_bits(atom.bits);
      }
      return static_cast<uint64_t>(da / db) & mask_bits(atom.bits);
    }
    case Family::UnsignedInt:
      if (b == 0) throw Fail("interpret", "division by zero", 0, 0);
      return (a / b) & mask_bits(atom.bits);
    default:
      throw Fail("interpret", "no device row for " + atom.name, 0, 0);
  }
}

int64_t signed_result(uint64_t bits, const Atom& atom) {
  if (atom.family == Family::Float && atom.name == "f64") {
    double d;
    std::memcpy(&d, &bits, 8);
    return static_cast<int64_t>(d);
  }
  uint64_t m = mask_bits(atom.bits);
  uint64_t v = bits & m;
  if (atom.family == Family::SignedInt && atom.bits < 64) {
    uint64_t sign = 1ull << (atom.bits - 1);
    if (v & sign) v |= ~m;
  }
  return static_cast<int64_t>(v);
}

const BcFunc* find_fn(const Program& prog, const std::string& name) {
  for (const BcFunc& fn : prog.fns) {
    if (fn.shape.name == name) return &fn;
  }
  return nullptr;
}

}  // namespace

int64_t interpret(const Program& prog, const std::string& entry) {
  const BcFunc* fn = find_fn(prog, entry);
  if (!fn) throw Fail("interpret", "no function '" + entry + "'", 0, 0);
  if (!fn->shape.outputs.empty() && fn->shape.outputs[0].atom->family == Family::Float) {
    throw Fail("interpret", "this runner prints an integer result; " + fn->shape.outputs[0].atom->name + " stays in the bytecode", 0, 0);
  }
  std::vector<uint64_t> slot(fn->nslots, 0);
  for (const Insn& in : fn->code) {
    switch (in.op) {
      case Op::Imm:
        slot[in.dst] = in.imm;
        break;
      case Op::Mov:
        slot[in.dst] = slot[in.lhs];
        break;
      case Op::Add:
        slot[in.dst] = wrap_add(slot[in.lhs], slot[in.rhs], *in.atom, false);
        break;
      case Op::Sub:
        slot[in.dst] = wrap_add(slot[in.lhs], slot[in.rhs], *in.atom, true);
        break;
      case Op::Mul:
        slot[in.dst] = wrap_mul(slot[in.lhs], slot[in.rhs], *in.atom);
        break;
      case Op::Div:
        slot[in.dst] = wrap_div(slot[in.lhs], slot[in.rhs], *in.atom);
        break;
      case Op::Ret:
        return signed_result(slot[in.dst], *in.atom);
    }
  }
  throw Fail("interpret", "'" + entry + "' did not return", 0, 0);
}

std::string emit_asm(const Program& prog) {
  std::string out = "# stage asm\n# ";
  out += stage_info("asm").does;
  out += "\n    .text\n";
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) {
      if (!in.atom) throw Fail("asm", "opcode is missing its atom", in.line, 0);
      if (in.atom->family != Family::SignedInt || (in.atom->bits != 32 && in.atom->bits != 64)) {
        throw Fail("asm", "no amd64 row for " + in.atom->name + " yet. The bytecode still has that atom.", in.line, 0);
      }
    }
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
          throw Fail("asm", "division is in the bytecode; the amd64 row is not written yet", in.line, 0);
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
