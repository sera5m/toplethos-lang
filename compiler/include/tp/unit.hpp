#pragma once

#include "tp/token.hpp"
#include "tp/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tp {

enum class ExprKind : uint8_t {
  Imm, Name, Bin, Not, Neg, Call, Addr, Load, Index, Field, Bump, Cast, State
};

enum class StmtKind : uint8_t {
  Decl, Assign, Return, If, Loop, Leave, Store, CallStmt, Do, Lock, Unlock, Hold, Fork, Finish, Time, Continue
};

struct Expr {
  ExprKind kind = ExprKind::Imm;
  const Atom* atom = nullptr;
  std::string name;
  std::string op;
  uint64_t bits = 0;
  bool is_float = false;
  /* Bump: 1 means prefix. Field: 1 means the object is an address (HERE).
     Call: 1 means `start`. Call: 2 means `run` (fork, then start). */
  int aux = 0;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  std::vector<std::unique_ptr<Expr>> args;
  int line = 1;
  int col = 1;
};

struct Stmt {
  StmtKind kind = StmtKind::Return;
  VarShape var;
  std::unique_ptr<Expr> expr;
  std::unique_ptr<Expr> other;
  std::vector<Stmt> yes;
  std::vector<Stmt> no;
  std::vector<std::unique_ptr<Expr>> list;
  std::string label;
  int line = 1;
  int col = 1;
};

struct Function {
  FnShape shape;
  std::vector<Stmt> body;
  std::unique_ptr<Expr> require;
  int divzero = 0;
  int parallel = 0;
  bool method = false;
  const Atom* here = nullptr;
};

struct Unit {
  std::vector<Function> fns;
  const Atom* atom_i32 = nullptr;
  const Atom* atom_i64 = nullptr;
  const Atom* atom_bool = nullptr;
  const Atom* atom_f64 = nullptr;
};

/* Bind names to the type map. The tree still has every width. */
Unit bind_unit(const std::vector<Token>& toks, TypeMap& types);

std::string dump_tokens(const std::vector<Token>& toks);
std::string dump_unit(const Unit& unit);

}  // namespace tp
