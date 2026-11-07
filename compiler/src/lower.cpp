#include "tp/bytecode.hpp"

#include <cstring>
#include <unordered_map>

namespace tp {
namespace {

struct Lowerer {
  BcFunc fn;
  std::unordered_map<std::string, uint32_t> slots;

  uint32_t fresh(const std::string& name) {
    uint32_t id = fn.nslots++;
    fn.slot_name.push_back(name);
    return id;
  }

  uint32_t local(const std::string& name) {
    auto it = slots.find(name);
    if (it == slots.end()) throw Fail("bytecode", "'" + name + "' has no slot", 0, 0);
    return it->second;
  }

  uint32_t emit_expr(const Expr& e) {
    switch (e.kind) {
      case ExprKind::Imm: {
        uint32_t dst = fresh("imm");
        Insn in;
        in.op = Op::Imm;
        in.atom = e.atom;
        in.dst = dst;
        in.imm = e.bits;
        in.is_float = e.is_float;
        if (e.is_float) {
          double v = std::stod(e.name);
          static_assert(sizeof(double) == sizeof(uint64_t), "f64 width");
          uint64_t bits = 0;
          std::memcpy(&bits, &v, sizeof bits);
          in.imm = bits;
        }
        in.line = e.line;
        fn.code.push_back(in);
        return dst;
      }
      case ExprKind::Name: {
        uint32_t dst = fresh(e.name);
        Insn in;
        in.op = Op::Mov;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = local(e.name);
        in.line = e.line;
        fn.code.push_back(in);
        return dst;
      }
      case ExprKind::Bin: {
        uint32_t lhs = emit_expr(*e.lhs);
        uint32_t rhs = emit_expr(*e.rhs);
        uint32_t dst = fresh("bin");
        Insn in;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = lhs;
        in.rhs = rhs;
        in.line = e.line;
        if (e.op == "+") in.op = Op::Add;
        else if (e.op == "-") in.op = Op::Sub;
        else if (e.op == "*") in.op = Op::Mul;
        else if (e.op == "/") in.op = Op::Div;
        else throw Fail("bytecode", "no opcode for " + e.op, e.line, e.col);
        fn.code.push_back(in);
        return dst;
      }
    }
    throw Fail("bytecode", "empty expression", e.line, e.col);
  }

  void run(const Function& src) {
    fn.shape = src.shape;
    for (const VarShape& in : src.shape.inputs) {
      uint32_t id = fresh(in.name);
      slots[in.name] = id;
    }
    for (const Stmt& st : src.body) {
      switch (st.kind) {
        case StmtKind::Decl: {
          uint32_t id = fresh(st.var.name);
          slots[st.var.name] = id;
          uint32_t val = emit_expr(*st.expr);
          Insn in;
          in.op = Op::Mov;
          in.atom = st.var.atom;
          in.dst = id;
          in.lhs = val;
          in.line = st.line;
          fn.code.push_back(in);
          break;
        }
        case StmtKind::Assign: {
          uint32_t val = emit_expr(*st.expr);
          Insn in;
          in.op = Op::Mov;
          in.atom = st.var.atom;
          in.dst = local(st.var.name);
          in.lhs = val;
          in.line = st.line;
          fn.code.push_back(in);
          break;
        }
        case StmtKind::Return: {
          uint32_t val = emit_expr(*st.expr);
          if (src.shape.outputs.empty() || st.expr->atom != src.shape.outputs[0].atom) {
            throw Fail("bytecode", "return atom does not match the function output", st.line, st.col);
          }
          Insn in;
          in.op = Op::Ret;
          in.atom = st.expr->atom;
          in.dst = val;
          in.line = st.line;
          fn.code.push_back(in);
          break;
        }
      }
    }
  }
};

const char* op_name(Op op) {
  switch (op) {
    case Op::Imm:
      return "imm";
    case Op::Mov:
      return "mov";
    case Op::Add:
      return "add";
    case Op::Sub:
      return "sub";
    case Op::Mul:
      return "mul";
    case Op::Div:
      return "div";
    case Op::Ret:
      return "ret";
  }
  return "unset";
}

}  // namespace

Program lower(const Unit& unit) {
  Program prog;
  for (const Function& fn : unit.fns) {
    Lowerer low;
    low.run(fn);
    prog.fns.push_back(std::move(low.fn));
  }
  return prog;
}

std::string dump_program(const Program& prog) {
  std::string out = "stage bytecode\n";
  out += stage_info("bytecode").does;
  out += "\n";
  for (const BcFunc& fn : prog.fns) {
    out += shape_fn(fn.shape);
    out += "\n";
    for (const Insn& in : fn.code) {
      out += "  t";
      out += std::to_string(in.dst);
      out += " = ";
      out += op_name(in.op);
      out += " ";
      out += in.atom ? in.atom->name : "?";
      out += " bits=";
      out += in.atom ? std::to_string(in.atom->bits) : "?";
      switch (in.op) {
        case Op::Imm:
          if (in.is_float) out += " float-bits";
          else out += " " + std::to_string(static_cast<int64_t>(in.imm));
          break;
        case Op::Mov:
        case Op::Ret:
          out += " t" + std::to_string(in.op == Op::Ret ? in.dst : in.lhs);
          break;
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
          out += " t" + std::to_string(in.lhs) + " t" + std::to_string(in.rhs);
          break;
      }
      out += "\n";
    }
  }
  return out;
}

}  // namespace tp
