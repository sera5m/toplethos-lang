#include "tp/unit.hpp"

#include <unordered_map>

namespace tp {
namespace {

struct Cursor {
  const std::vector<Token>& toks;
  size_t i = 0;

  const Token& cur() const { return toks[i]; }

  bool eat(TokenKind kind) {
    if (cur().kind != kind) return false;
    ++i;
    return true;
  }

  void expect(TokenKind kind, const char* what) {
    if (!eat(kind)) throw Fail("bind", std::string("expected ") + what, cur().line, cur().col);
  }
};

struct Binder {
  Cursor c;
  const TypeMap& types;
  std::unordered_map<std::string, VarShape> locals;

  const Atom* atom_of(const Token& tok) {
    if (tok.kind != TokenKind::Ident) throw Fail("bind", "expected a type name", tok.line, tok.col);
    const Atom* atom = types.find(tok.text);
    if (!atom) {
      throw Fail("bind", "'" + tok.text + "' is not in the type map", tok.line, tok.col);
    }
    return atom;
  }

  VarShape param() {
    VarShape var;
    var.atom = atom_of(c.cur());
    c.expect(TokenKind::Ident, "a type name");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a parameter name", c.cur().line, c.cur().col);
    var.name = c.cur().text;
    ++c.i;
    return var;
  }

  std::unique_ptr<Expr> expr(const Atom* expect);
  std::unique_ptr<Expr> term(const Atom* expect);
  std::unique_ptr<Expr> unary(const Atom* expect);
  const Atom* ret_atom = nullptr;

  void need_same(const Expr& a, const Expr& b, const Token& op) {
    if (a.atom != b.atom) {
      throw Fail("bind",
                 std::string(op.text) + " needs two of the same atom, got " + a.atom->name + " and " + b.atom->name,
                 op.line, op.col);
    }
  }

  std::unique_ptr<Expr> bin(std::unique_ptr<Expr> left, const Token& op, std::unique_ptr<Expr> right) {
    need_same(*left, *right, op);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Bin;
    node->atom = left->atom;
    node->op = op.text;
    node->line = op.line;
    node->col = op.col;
    node->lhs = std::move(left);
    node->rhs = std::move(right);
    return node;
  }

  Stmt stmt() {
    Stmt st;
    st.line = c.cur().line;
    st.col = c.cur().col;
    if (c.eat(TokenKind::KwReturn)) {
      st.kind = StmtKind::Return;
      st.expr = expr(ret_atom);
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    if (c.cur().kind == TokenKind::Ident && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::Eq) {
      auto found = locals.find(c.cur().text);
      if (found == locals.end()) throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
      st.kind = StmtKind::Assign;
      st.var = found->second;
      ++c.i;
      c.expect(TokenKind::Eq, "=");
      st.expr = expr(st.var.atom);
      if (st.expr->atom != st.var.atom) {
        throw Fail("bind", "assignment of " + st.expr->atom->name + " into " + st.var.atom->name, st.line, st.col);
      }
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    if (c.cur().kind == TokenKind::Ident && types.find(c.cur().text)) {
      st.kind = StmtKind::Decl;
      st.var.atom = atom_of(c.cur());
      c.expect(TokenKind::Ident, "a type name");
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a variable name", c.cur().line, c.cur().col);
      st.var.name = c.cur().text;
      if (locals.count(st.var.name)) throw Fail("bind", "'" + st.var.name + "' is already declared", c.cur().line, c.cur().col);
      ++c.i;
      if (c.cur().kind == TokenKind::LBracket) {
        throw Fail("bind", "arrays are tokenized; this stage does not lower them yet", c.cur().line, c.cur().col);
      }
      c.expect(TokenKind::Eq, "=");
      st.expr = expr(st.var.atom);
      if (st.expr->atom != st.var.atom) {
        throw Fail("bind", "initializer is " + st.expr->atom->name + ", the variable is " + st.var.atom->name, st.line, st.col);
      }
      c.expect(TokenKind::Semicolon, ";");
      locals[st.var.name] = st.var;
      return st;
    }
    if (c.cur().kind == TokenKind::KwIf || c.cur().kind == TokenKind::KwLoop || c.cur().kind == TokenKind::KwDo) {
      throw Fail("bind", std::string("'") + token_name(c.cur().kind) + "' is a token; this stage does not lower it yet",
                 c.cur().line, c.cur().col);
    }
    throw Fail("bind", "expected a declaration, an assignment, or return", c.cur().line, c.cur().col);
  }

  Function function() {
    Function fn;
    ret_atom = nullptr;
    c.expect(TokenKind::KwFn, "fn");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a function name", c.cur().line, c.cur().col);
    fn.shape.name = c.cur().text;
    ++c.i;
    c.expect(TokenKind::KwIn, "in");
    c.expect(TokenKind::LBracket, "[");
    locals.clear();
    if (!c.eat(TokenKind::RBracket)) {
      for (;;) {
        VarShape var = param();
        fn.shape.inputs.push_back(var);
        locals[var.name] = var;
        if (c.eat(TokenKind::RBracket)) break;
        c.expect(TokenKind::Comma, ",");
      }
    }
    c.expect(TokenKind::KwOut, "out");
    c.expect(TokenKind::LBracket, "[");
    if (!c.eat(TokenKind::RBracket)) {
      VarShape out;
      out.atom = atom_of(c.cur());
      c.expect(TokenKind::Ident, "a type name");
      out.name = "out";
      fn.shape.outputs.push_back(out);
      ret_atom = out.atom;
      c.expect(TokenKind::RBracket, "]");
    }
    c.expect(TokenKind::LBrace, "{");
    while (!c.eat(TokenKind::RBrace)) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed function", c.cur().line, c.cur().col);
      fn.body.push_back(stmt());
    }
    return fn;
  }
};

std::unique_ptr<Expr> Binder::expr(const Atom* expect) {
  auto left = term(expect);
  while (c.cur().kind == TokenKind::Plus || c.cur().kind == TokenKind::Minus) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, term(left->atom));
  }
  return left;
}

std::unique_ptr<Expr> Binder::term(const Atom* expect) {
  auto left = unary(expect);
  while (c.cur().kind == TokenKind::Star || c.cur().kind == TokenKind::Slash || c.cur().kind == TokenKind::Percent) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, unary(left->atom));
  }
  return left;
}

bool integer_fits(const Atom* atom, uint64_t bits) {
  if (!atom) return false;
  if (atom->family != Family::SignedInt && atom->family != Family::UnsignedInt) return false;
  if (atom->bits >= 64) return bits <= static_cast<uint64_t>(INT64_MAX);
  uint64_t limit = 1ull << (atom->bits - (atom->family == Family::SignedInt ? 1 : 0));
  if (atom->family == Family::SignedInt) return bits < limit;
  return bits < (1ull << atom->bits);
}

std::unique_ptr<Expr> Binder::unary(const Atom* expect) {
  if (c.cur().kind == TokenKind::Minus && (c.i + 1 < c.toks.size()) && c.toks[c.i + 1].kind == TokenKind::Number) {
    ++c.i;
    auto inner = unary(expect);
    if (inner->atom->family != Family::SignedInt) throw Fail("bind", "negating a " + inner->atom->name, inner->line, inner->col);
    inner->bits = static_cast<uint64_t>(-static_cast<int64_t>(inner->bits));
    return inner;
  }
  if (c.eat(TokenKind::LParen)) {
    auto inner = expr(expect);
    c.expect(TokenKind::RParen, ")");
    return inner;
  }
  if (c.cur().kind == TokenKind::Number) {
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Imm;
    node->line = c.cur().line;
    node->col = c.cur().col;
    node->is_float = c.cur().is_float;
    if (c.cur().is_float) {
      if (expect && expect->name != "f64") throw Fail("bind", "float literal is f64, the slot is " + expect->name, c.cur().line, c.cur().col);
      node->atom = types.find("f64");
      node->name = c.cur().text;
    } else {
      const Atom* atom = expect && integer_fits(expect, c.cur().bits) ? expect : types.find("i32");
      if (!integer_fits(atom, c.cur().bits)) throw Fail("bind", "integer does not fit in " + atom->name, c.cur().line, c.cur().col);
      node->atom = atom;
      node->bits = c.cur().bits;
    }
    ++c.i;
    return node;
  }
  if (c.cur().kind == TokenKind::KwTrue || c.cur().kind == TokenKind::KwFalse) {
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Imm;
    node->atom = types.find("bool");
    node->bits = c.cur().kind == TokenKind::KwTrue ? 1 : 0;
    node->line = c.cur().line;
    node->col = c.cur().col;
    ++c.i;
    return node;
  }
  if (c.cur().kind == TokenKind::Ident) {
    auto found = locals.find(c.cur().text);
    if (found == locals.end()) throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Name;
    node->name = c.cur().text;
    node->atom = found->second.atom;
    node->line = c.cur().line;
    node->col = c.cur().col;
    ++c.i;
    return node;
  }
  throw Fail("bind", "expected an expression", c.cur().line, c.cur().col);
}

void dump_expr(std::string& out, const Expr& e) {
  switch (e.kind) {
    case ExprKind::Imm:
      out += e.atom->name;
      out += " ";
      if (e.is_float) out += e.name;
      else out += std::to_string(static_cast<int64_t>(e.bits));
      return;
    case ExprKind::Name:
      out += e.name;
      out += ":";
      out += e.atom->name;
      return;
    case ExprKind::Bin:
      out += "(";
      dump_expr(out, *e.lhs);
      out += " ";
      out += e.op;
      out += " ";
      dump_expr(out, *e.rhs);
      out += "):";
      out += e.atom->name;
      return;
  }
}

}  // namespace

Unit bind_unit(const std::vector<Token>& toks, const TypeMap& types) {
  Binder binder{{toks, 0}, types, {}};
  Unit unit;
  while (binder.c.cur().kind != TokenKind::End) {
    unit.fns.push_back(binder.function());
  }
  return unit;
}

std::string dump_tokens(const std::vector<Token>& toks) {
  std::string out = "stage lex\n";
  out += stage_info("lex").does;
  out += "\n";
  for (const Token& tok : toks) {
    if (tok.kind == TokenKind::End) continue;
    out += std::to_string(tok.line);
    out += ":";
    out += std::to_string(tok.col);
    out += " ";
    out += token_name(tok.kind);
    if (!tok.text.empty() && tok.kind == TokenKind::Ident) {
      out += " ";
      out += tok.text;
    } else if (tok.kind == TokenKind::Number || tok.kind == TokenKind::String) {
      out += " ";
      out += tok.text;
    }
    out += "\n";
  }
  return out;
}

std::string dump_unit(const Unit& unit) {
  std::string out = "stage bind\n";
  out += stage_info("bind").does;
  out += "\n";
  for (const Function& fn : unit.fns) {
    out += shape_fn(fn.shape);
    out += "\n";
    for (const Stmt& st : fn.body) {
      out += "  ";
      switch (st.kind) {
        case StmtKind::Decl:
          out += "decl ";
          out += shape_var(st.var);
          out += " = ";
          dump_expr(out, *st.expr);
          break;
        case StmtKind::Assign:
          out += "set ";
          out += st.var.name;
          out += " = ";
          dump_expr(out, *st.expr);
          break;
        case StmtKind::Return:
          out += "return ";
          dump_expr(out, *st.expr);
          break;
      }
      out += "\n";
    }
  }
  return out;
}

}  // namespace tp
