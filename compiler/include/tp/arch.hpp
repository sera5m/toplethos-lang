#pragma once

#include "tp/target.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace tp {

/* Conversion-stage map. A missing cell means that target cannot print the op.
   The interpreter is not this table. It can run a row no backend has yet. */

constexpr uint8_t kOpImm = 1u << 0;
constexpr uint8_t kOpMov = 1u << 1;
constexpr uint8_t kOpAdd = 1u << 2;
constexpr uint8_t kOpSub = 1u << 3;
constexpr uint8_t kOpMul = 1u << 4;
constexpr uint8_t kOpDiv = 1u << 5;
constexpr uint8_t kOpRet = 1u << 6;

/* Division is omitted on purpose. No target has a row for it yet. */
constexpr uint8_t kIntArith = kOpImm | kOpMov | kOpAdd | kOpSub | kOpMul | kOpRet;

struct ArchRow {
  Target target;
  const char* atom;
  uint8_t ops;
};

inline constexpr ArchRow kArch[] = {
    {Target::Amd64, "i32", kIntArith},
    {Target::Amd64, "i64", kIntArith},
    {Target::Arm64, "i32", kIntArith},
    {Target::Arm64, "i64", kIntArith},
    {Target::Spirv, "i32", kIntArith},
    {Target::Spirv, "i64", kIntArith},
    {Target::Cpp, "i32", kIntArith},
    {Target::Cpp, "i64", kIntArith},
    {Target::Cpp, "ui32", kIntArith},
    {Target::Cpp, "ui64", kIntArith},
};

inline uint8_t arch_bit(Op op) {
  switch (op) {
    case Op::Imm:
      return kOpImm;
    case Op::Mov:
      return kOpMov;
    case Op::Add:
      return kOpAdd;
    case Op::Sub:
      return kOpSub;
    case Op::Mul:
      return kOpMul;
    case Op::Div:
      return kOpDiv;
    case Op::Ret:
      return kOpRet;
  }
  return 0;
}

inline const char* arch_op_name(Op op) {
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

inline bool arch_supports(Target target, Op op, std::string_view atom) {
  uint8_t bit = arch_bit(op);
  for (const ArchRow& row : kArch) {
    if (row.target == target && atom == row.atom) return (row.ops & bit) != 0;
  }
  return false;
}

inline void arch_require(Target target, const Insn& in) {
  if (in.atom && arch_supports(target, in.op, in.atom->name)) return;
  std::string why = "no ";
  why += target_name(target);
  why += " row for ";
  why += in.atom ? in.atom->name : "missing atom";
  why += " ";
  why += arch_op_name(in.op);
  why += ". The bytecode still has that atom.";
  throw Fail(target_name(target), why, in.line, 0);
}

}  // namespace tp
