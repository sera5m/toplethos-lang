#pragma once

#include "tp/unit.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tp {

/* Three-address. The type id is an index into TypeMap, not a comment. */
enum class Op : uint16_t { Imm, Mov, Add, Sub, Mul, Div, Ret };

struct Insn {
  Op op = Op::Imm;
  const Atom* atom = nullptr;
  uint32_t dst = 0;
  uint32_t lhs = 0;
  uint32_t rhs = 0;
  uint64_t imm = 0;
  bool is_float = false;
  int line = 1;
};

struct BcFunc {
  FnShape shape;
  std::vector<Insn> code;
  uint32_t nslots = 0;
  std::vector<std::string> slot_name;
};

struct Program {
  std::vector<BcFunc> fns;
};

Program lower(const Unit& unit);
std::string dump_program(const Program& prog);

/* Device: run bytecode. Assembly: print amd64 from the same list. */
int64_t interpret(const Program& prog, const std::string& entry);
std::string emit_asm(const Program& prog);

}  // namespace tp
