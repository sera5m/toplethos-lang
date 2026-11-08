#pragma once

#include "tp/pointer.hpp"
#include "tp/target.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace tp {

/* Conversion-stage map. A missing cell means that target cannot print the op.
   The interpreter is not this table. It can run a row no backend has yet. */

constexpr uint32_t kOpImm = 1u << 0;
constexpr uint32_t kOpMov = 1u << 1;
constexpr uint32_t kOpAdd = 1u << 2;
constexpr uint32_t kOpSub = 1u << 3;
constexpr uint32_t kOpMul = 1u << 4;
constexpr uint32_t kOpDiv = 1u << 5;
constexpr uint32_t kOpRem = 1u << 6;
constexpr uint32_t kOpAnd = 1u << 7;
constexpr uint32_t kOpOr = 1u << 8;
constexpr uint32_t kOpXor = 1u << 9;
constexpr uint32_t kOpShl = 1u << 10;
constexpr uint32_t kOpShr = 1u << 11;
constexpr uint32_t kOpRet = 1u << 12;
constexpr uint32_t kOpEq = 1u << 13;
constexpr uint32_t kOpNe = 1u << 14;
constexpr uint32_t kOpLt = 1u << 15;
constexpr uint32_t kOpLe = 1u << 16;
constexpr uint32_t kOpGt = 1u << 17;
constexpr uint32_t kOpGe = 1u << 18;
constexpr uint32_t kOpNot = 1u << 19;
constexpr uint32_t kOpBr = 1u << 20;
constexpr uint32_t kOpNeg = 1u << 21;
constexpr uint32_t kOpCall = 1u << 22;
constexpr uint32_t kOpThreads = 1u << 23;
constexpr uint32_t kOpFreemem = 1u << 24;
constexpr uint32_t kOpBrRange = 1u << 25;
constexpr uint32_t kOpAddr = 1u << 26;
constexpr uint32_t kOpLoad = 1u << 27;
constexpr uint32_t kOpStore = 1u << 28;

struct ArchRow {
  Target target;
  const char* atom;
  uint32_t ops;
};

constexpr uint32_t kIntArith = kOpImm | kOpMov | kOpAdd | kOpSub | kOpMul | kOpDiv | kOpRem | kOpAnd | kOpOr | kOpXor |
                               kOpShl | kOpShr | kOpRet;
constexpr uint32_t kBoolOps = kOpImm | kOpMov | kOpEq | kOpNe | kOpLt | kOpLe | kOpGt | kOpGe | kOpNot | kOpBr;
constexpr uint32_t kCpuInt = kIntArith | kOpNeg | kOpBrRange | kOpCall;
constexpr uint32_t kGpuInt = kIntArith | kOpNeg | kOpBrRange;

inline constexpr ArchRow kArch[] = {
    {Target::Amd64, "i32", kCpuInt | kOpThreads},
    {Target::Amd64, "i64", kCpuInt | kOpFreemem},
    {Target::Amd64, "bool", kBoolOps},
    {Target::Arm64, "i32", kCpuInt | kOpThreads},
    {Target::Arm64, "i64", kCpuInt | kOpFreemem},
    {Target::Arm64, "bool", kBoolOps},
    {Target::Spirv, "i32", kGpuInt},
    {Target::Spirv, "i64", kGpuInt},
    {Target::Spirv, "bool", kBoolOps},
    {Target::Cpp, "i32", kCpuInt | kOpThreads},
    {Target::Cpp, "i64", kCpuInt | kOpFreemem},
    {Target::Cpp, "bool", kBoolOps},
    {Target::Cpp, "ui32", kIntArith | kOpBrRange | kOpCall},
    {Target::Cpp, "ui64", kIntArith | kOpBrRange | kOpCall},
};

inline uint32_t arch_bit(Op op) {
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
    case Op::Rem:
      return kOpRem;
    case Op::And:
      return kOpAnd;
    case Op::Or:
      return kOpOr;
    case Op::Xor:
      return kOpXor;
    case Op::Shl:
      return kOpShl;
    case Op::Shr:
      return kOpShr;
    case Op::Ret:
      return kOpRet;
    case Op::Eq:
      return kOpEq;
    case Op::Ne:
      return kOpNe;
    case Op::Lt:
      return kOpLt;
    case Op::Le:
      return kOpLe;
    case Op::Gt:
      return kOpGt;
    case Op::Ge:
      return kOpGe;
    case Op::Not:
      return kOpNot;
    case Op::Neg:
      return kOpNeg;
    case Op::Br:
      return kOpBr;
    case Op::BrRange:
      return kOpBrRange;
    case Op::Call:
      return kOpCall;
    case Op::Threads:
      return kOpThreads;
    case Op::Freemem:
      return kOpFreemem;
    case Op::Addr:
      return kOpAddr;
    case Op::Load:
      return kOpLoad;
    case Op::Store:
      return kOpStore;
    case Op::Jmp:
    case Op::Label:
      return 0;
    default:
      return 0;
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
    case Op::Rem:
      return "rem";
    case Op::And:
      return "and";
    case Op::Or:
      return "or";
    case Op::Xor:
      return "xor";
    case Op::Shl:
      return "shl";
    case Op::Shr:
      return "shr";
    case Op::Ret:
      return "ret";
    case Op::Eq:
      return "eq";
    case Op::Ne:
      return "ne";
    case Op::Lt:
      return "lt";
    case Op::Le:
      return "le";
    case Op::Gt:
      return "gt";
    case Op::Ge:
      return "ge";
    case Op::Not:
      return "not";
    case Op::Neg:
      return "neg";
    case Op::Jmp:
      return "jmp";
    case Op::Br:
      return "br";
    case Op::BrRange:
      return "brrange";
    case Op::Label:
      return "label";
    case Op::Call:
      return "call";
    case Op::Threads:
      return "threads";
    case Op::Freemem:
      return "freemem";
    case Op::Addr:
      return "addr";
    case Op::Load:
      return "load";
    case Op::Store:
      return "store";
    default:
      return "op";
  }
  return "unset";
}

inline bool arch_supports(Target target, Op op, std::string_view atom) {
  if (op == Op::Jmp || op == Op::Label) return true;
  uint32_t bit = arch_bit(op);
  for (const ArchRow& row : kArch) {
    if (row.target == target && atom == row.atom) return (row.ops & bit) != 0;
  }
  return false;
}

inline bool cell_op_ok(Target target, Op op) {
  if (target == Target::Spirv) return false;
  switch (op) {
    case Op::Addr:
    case Op::Load:
    case Op::Store:
    case Op::Mov:
    case Op::Ret:
      return target == Target::Amd64 || target == Target::Arm64 || target == Target::Cpp;
    default:
      return false;
  }
}

inline void arch_require(Target target, const Insn& in) {
  if (in.op == Op::Jmp || in.op == Op::Label) return;
  if (in.op == Op::Call && !in.atom) {
    if (target == Target::Spirv) {
      throw Fail("spirv", "a call has no SPIR-V calling convention yet", in.line, 0);
    }
    return;
  }
  const Atom* ptr = nullptr;
  if (in.op == Op::Addr) ptr = in.atom;
  else if (in.op == Op::Load || in.op == Op::Store) ptr = in.src;
  else if (in.atom && in.atom->family == Family::Pointer) ptr = in.atom;
  if (ptr && ptr->family == Family::Pointer) {
    const PtrKind* kind = pointer_kind(ptr->ptr_kind);
    bool ok = kind && std::string(kind->name) == "cell" && kind->program && cell_op_ok(target, in.op);
    if (ok) return;
    std::string why = "no ";
    why += target_name(target);
    why += " row for ";
    why += ptr->ptr_kind ? ptr->ptr_kind : "pointer";
    why += " ";
    why += arch_op_name(in.op);
    why += ". ";
    if (target == Target::Spirv) why += "A shader does not keep a host pointer.";
    else why += "The program loads and stores only cell.";
    throw Fail(target_name(target), why, in.line, 0);
  }
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
