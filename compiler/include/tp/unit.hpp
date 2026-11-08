#pragma once

#include "tp/token.hpp"
#include "tp/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tp {

enum class ExprKind : uint8_t { Imm, Name, Bin, Not, Neg, Call, Addr, Load };
enum class StmtKind : uint8_t { Decl, Assign, Return, If, Loop, Leave, Store, CallStmt };

struct Expr {
  ExprKind kind = ExprKind::Imm;
  const Atom* atom = nullptr;
  std::string name;
  std::string op;
  uint64_t bits = 0;
  bool is_float = false;
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
  int line = 1;
  int col = 1;
};

struct Function {
  FnShape shape;
  std::vector<Stmt> body;
};

struct Unit {
  std::vector<Function> fns;
};

/* Bind names to the type map. The tree still has every width. */
Unit bind_unit(const std::vector<Token>& toks, TypeMap& types);

std::string dump_tokens(const std::vector<Token>& toks);
std::string dump_unit(const Unit& unit);

}  // namespace tp
