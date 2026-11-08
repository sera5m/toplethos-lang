#pragma once

#include "tp/unit.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tp {

/* Three-address. The type id is an index into TypeMap, not a comment. */
enum class Op : uint16_t {
  Imm, Mov, Add, Sub, Mul, Div, Rem, And, Or, Xor, Shl, Shr,
  Eq, Ne, Lt, Le, Gt, Ge, Not, Neg, Jmp, Br, BrRange, Label, Ret,
  Call, Threads, Freemem, Addr, Load, Store
};

struct Insn {
  Op op = Op::Imm;
  const Atom* atom = nullptr;
  const Atom* src = nullptr;
  uint32_t dst = 0;
  uint32_t lhs = 0;
  uint32_t rhs = 0;
  uint64_t imm = 0;
  uint64_t imm_lo = 0;
  uint64_t imm_hi = 0;
  std::vector<uint32_t> args;
  bool is_float = false;
  /* True when this opcode is one SPIR-V can print. Host facts, calls, and
     cell pointers are not. The tag is not a placement: a scalar function
     stays on the CPU even when every opcode is a GPU opcode. */
  bool gpu = false;
  int line = 1;
};

struct BcFunc {
  FnShape shape;
  std::vector<Insn> code;
  uint32_t nslots = 0;
  std::vector<std::string> slot_name;
  /* Source counts. cost is not stored: it is unknown, not a cycle score. */
  int drop_n = 0;
  int reuse_n = 0;
  int inc_n = 0;
  int range_n = 0;
  int forward_n = 0;
  bool cell_ptr = false;
  /* Every opcode is a SPIR-V row. scalar_ctrl means a branch or a jump:
     that shape stays on the CPU. A straight line with no host op is still
     `short`: one scalar result is not a large uniform run. */
  bool gpu_ops = false;
  bool scalar_ctrl = false;
};

struct Program {
  std::vector<BcFunc> fns;
};

/* Filled while the interpreter runs. Not a rewrite, and not a snapshot. */
struct Active {
  std::vector<uint64_t> calls;
  std::vector<std::vector<std::string>> callers;
};

Program lower(const Unit& unit);
std::string dump_program(const Program& prog);

/* CPU oracle. It runs the bytecode. It does not select instructions.
   active may be null. When it is set, call counts and caller edges are recorded. */
int64_t interpret(const Program& prog, const std::string& entry, Active* active = nullptr, std::string* jit_log = nullptr);
std::string active_report(const Program& prog, const Active& active);

}  // namespace tp
