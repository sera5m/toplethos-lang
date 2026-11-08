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
  TypeMap& types;
  std::unordered_map<std::string, VarShape> locals;
  std::unordered_map<std::string, FnShape> fns;

  const Atom* atom_of(const Token& tok) {
    if (tok.kind != TokenKind::Ident) throw Fail("bind", "expected a type name", tok.line, tok.col);
    const Atom* atom = types.find(tok.text);
    if (!atom) {
      throw Fail("bind", "'" + tok.text + "' is not in the type map", tok.line, tok.col);
    }
    return atom;
  }

  const Atom* parse_type() {
    const Atom* atom = atom_of(c.cur());
    c.expect(TokenKind::Ident, "a type name");
    while (c.eat(TokenKind::Star)) atom = types.pointer_to(atom);
    return atom;
  }

  VarShape param() {
    VarShape var;
    var.atom = parse_type();
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a parameter name", c.cur().line, c.cur().col);
    var.name = c.cur().text;
    ++c.i;
    return var;
  }

  std::unique_ptr<Expr> expr(const Atom* expect);
  std::unique_ptr<Expr> bit_or(const Atom* expect);
  std::unique_ptr<Expr> bit_xor(const Atom* expect);
  std::unique_ptr<Expr> bit_and(const Atom* expect);
  std::unique_ptr<Expr> equality(const Atom* expect);
  std::unique_ptr<Expr> relation(const Atom* expect);
  std::unique_ptr<Expr> shift(const Atom* expect);
  std::unique_ptr<Expr> add(const Atom* expect);
  std::unique_ptr<Expr> term(const Atom* expect);
  std::unique_ptr<Expr> unary(const Atom* expect);
  std::unique_ptr<Expr> call_expr(const Atom* expect, bool as_value);
  std::vector<Stmt> block();
  const Atom* ret_atom = nullptr;

  void need_same(const Expr& a, const Expr& b, const Token& op) {
    if (a.atom != b.atom) {
      throw Fail("bind",
                 std::string(op.text) + " needs two of the same atom, got " + a.atom->name + " and " + b.atom->name,
                 op.line, op.col);
    }
  }

  std::unique_ptr<Expr> bin(std::unique_ptr<Expr> left, const Token& op, std::unique_ptr<Expr> right) {
    if (left->atom && left->atom->family == Family::Pointer && op.text != "==" && op.text != "!=") {
      throw Fail("bind", "no arithmetic row for a pointer", op.line, op.col);
    }
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
    if (c.cur().kind == TokenKind::Star) {
      Token op = c.cur();
      ++c.i;
      auto ptr = unary(nullptr);
      if (!ptr->atom || ptr->atom->family != Family::Pointer || !ptr->atom->pointee) {
        throw Fail("bind", "* of a value that is not a pointer", op.line, op.col);
      }
      st.kind = StmtKind::Store;
      st.expr = std::move(ptr);
      c.expect(TokenKind::Eq, "=");
      st.other = expr(st.expr->atom->pointee);
      if (st.other->atom != st.expr->atom->pointee) {
        throw Fail("bind", "store of " + st.other->atom->name + " through " + st.expr->atom->name, st.line, st.col);
      }
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    if (c.cur().kind == TokenKind::Ident && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::KwIn &&
        !types.find(c.cur().text)) {
      st.kind = StmtKind::CallStmt;
      st.expr = call_expr(nullptr, false);
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
      st.var.atom = parse_type();
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
    if (c.eat(TokenKind::KwIf)) {
      st.kind = StmtKind::If;
      st.expr = expr(types.find("bool"));
      if (!st.expr->atom || st.expr->atom->name != "bool") {
        throw Fail("bind", "if requires a bool", st.line, st.col);
      }
      c.expect(TokenKind::LBrace, "{");
      st.yes = block();
      c.expect(TokenKind::RBrace, "}");
      if (c.eat(TokenKind::KwElse)) {
        c.expect(TokenKind::LBrace, "{");
        st.no = block();
        c.expect(TokenKind::RBrace, "}");
      }
      return st;
    }
    if (c.eat(TokenKind::KwLoop)) {
      st.kind = StmtKind::Loop;
      c.expect(TokenKind::LBrace, "{");
      st.yes = block();
      c.expect(TokenKind::RBrace, "}");
      return st;
    }
    if (c.eat(TokenKind::KwLeave)) {
      st.kind = StmtKind::Leave;
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    if (c.cur().kind == TokenKind::KwDo) {
      throw Fail("bind", "'do' is a token; this stage does not lower it yet", c.cur().line, c.cur().col);
    }
    throw Fail("bind", "expected a declaration, an assignment, or return", c.cur().line, c.cur().col);
  }

  Function function() {
    Function fn;
    ret_atom = nullptr;
    c.expect(TokenKind::KwFn, "fn");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a function name", c.cur().line, c.cur().col);
    fn.shape.name = c.cur().text;
    Token name_tok = c.cur();
    ++c.i;
    if (fn.shape.name == "threads" || fn.shape.name == "freemem") {
      throw Fail("bind", fn.shape.name + " is a host fact", name_tok.line, name_tok.col);
    }
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
      out.atom = parse_type();
      out.name = "out";
      fn.shape.outputs.push_back(out);
      ret_atom = out.atom;
      c.expect(TokenKind::RBracket, "]");
    }
    while (c.cur().kind == TokenKind::At) {
      if (c.cur().text != "inlined") {
        throw Fail("bind", "@" + c.cur().text + " is not in this stage", c.cur().line, c.cur().col);
      }
      for (Mutator m : fn.shape.mutators) {
        if (m == Mutator::Inlined) throw Fail("bind", "@inlined is already set", c.cur().line, c.cur().col);
      }
      fn.shape.mutators.push_back(Mutator::Inlined);
      ++c.i;
    }
    if (fns.count(fn.shape.name)) throw Fail("bind", "'" + fn.shape.name + "' is already declared", c.cur().line, c.cur().col);
    fns[fn.shape.name] = fn.shape;
    c.expect(TokenKind::LBrace, "{");
    while (!c.eat(TokenKind::RBrace)) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed function", c.cur().line, c.cur().col);
      fn.body.push_back(stmt());
    }
    return fn;
  }
};

std::vector<Stmt> Binder::block() {
  std::vector<Stmt> body;
  while (c.cur().kind != TokenKind::RBrace && c.cur().kind != TokenKind::End) body.push_back(stmt());
  if (c.cur().kind != TokenKind::RBrace) throw Fail("bind", "unclosed block", c.cur().line, c.cur().col);
  return body;
}

std::unique_ptr<Expr> Binder::expr(const Atom* expect) {
  auto left = bit_or(expect);
  while (c.cur().kind == TokenKind::AmpAmp || c.cur().kind == TokenKind::PipePipe) {
    Token op = c.cur();
    ++c.i;
    if (!left->atom || left->atom->name != "bool") throw Fail("bind", "logic needs a bool", op.line, op.col);
    auto right = bit_or(types.find("bool"));
    if (!right->atom || right->atom->name != "bool") throw Fail("bind", "logic needs a bool", op.line, op.col);
    left = bin(std::move(left), op, std::move(right));
  }
  return left;
}

std::unique_ptr<Expr> Binder::bit_or(const Atom* expect) {
  auto left = bit_xor(expect);
  while (c.cur().kind == TokenKind::Pipe) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, bit_xor(left->atom));
  }
  return left;
}

std::unique_ptr<Expr> Binder::bit_xor(const Atom* expect) {
  auto left = bit_and(expect);
  while (c.cur().kind == TokenKind::Caret) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, bit_and(left->atom));
  }
  return left;
}

std::unique_ptr<Expr> Binder::bit_and(const Atom* expect) {
  auto left = equality(expect);
  while (c.cur().kind == TokenKind::Amp) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, equality(left->atom));
  }
  return left;
}

std::unique_ptr<Expr> Binder::equality(const Atom* expect) {
  auto left = relation(expect);
  while (c.cur().kind == TokenKind::EqEq || c.cur().kind == TokenKind::NotEq) {
    Token op = c.cur();
    ++c.i;
    auto right = relation(left->atom);
    auto node = bin(std::move(left), op, std::move(right));
    node->atom = types.find("bool");
    left = std::move(node);
  }
  return left;
}

std::unique_ptr<Expr> Binder::relation(const Atom* expect) {
  auto left = shift(expect);
  while (c.cur().kind == TokenKind::Lt || c.cur().kind == TokenKind::Gt || c.cur().kind == TokenKind::LtEq ||
         c.cur().kind == TokenKind::GtEq) {
    Token op = c.cur();
    ++c.i;
    if (left->atom && left->atom->family == Family::Pointer) {
      throw Fail("bind", "ordered compare of pointers", op.line, op.col);
    }
    auto right = shift(left->atom);
    auto node = bin(std::move(left), op, std::move(right));
    node->atom = types.find("bool");
    left = std::move(node);
  }
  return left;
}

std::unique_ptr<Expr> Binder::shift(const Atom* expect) {
  auto left = add(expect);
  while (c.cur().kind == TokenKind::Shl || c.cur().kind == TokenKind::Shr) {
    Token op = c.cur();
    ++c.i;
    if (!left->atom || (left->atom->family != Family::SignedInt && left->atom->family != Family::UnsignedInt)) {
      throw Fail("bind", "shift needs an integer", op.line, op.col);
    }
    auto right = add(types.find("i32"));
    if (!right->atom || right->atom->name != "i32") {
      throw Fail("bind", "shift count is i32, got " + (right->atom ? right->atom->name : "nothing"), op.line, op.col);
    }
    if (right->kind == ExprKind::Imm && right->bits >= static_cast<uint64_t>(left->atom->bits)) {
      throw Fail("bind", "shift count is outside 0 .. width-1", op.line, op.col);
    }
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Bin;
    node->atom = left->atom;
    node->op = op.text;
    node->line = op.line;
    node->col = op.col;
    node->lhs = std::move(left);
    node->rhs = std::move(right);
    left = std::move(node);
  }
  return left;
}

std::unique_ptr<Expr> Binder::add(const Atom* expect) {
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
    if (c.cur().kind == TokenKind::StarStar || c.cur().kind == TokenKind::SlashFloor) {
      throw Fail("bind", std::string(token_name(c.cur().kind)) + " is tokenized; this stage does not lower it yet", c.cur().line, c.cur().col);
    }
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

std::unique_ptr<Expr> Binder::call_expr(const Atom* expect, bool as_value) {
  (void)expect;
  Token name = c.cur();
  ++c.i;
  c.expect(TokenKind::KwIn, "in");
  c.expect(TokenKind::LBracket, "[");
  auto node = std::make_unique<Expr>();
  node->kind = ExprKind::Call;
  node->name = name.text;
  node->line = name.line;
  node->col = name.col;
  if (name.text == "threads" || name.text == "freemem") {
    if (!c.eat(TokenKind::RBracket)) throw Fail("bind", name.text + " takes no arguments", c.cur().line, c.cur().col);
    node->atom = types.find(name.text == "threads" ? "i32" : "i64");
    return node;
  }
  auto found = fns.find(name.text);
  if (found == fns.end()) throw Fail("bind", "'" + name.text + "' is not declared", name.line, name.col);
  const FnShape& sig = found->second;
  size_t n = 0;
  if (!c.eat(TokenKind::RBracket)) {
    for (;;) {
      if (n >= sig.inputs.size()) throw Fail("bind", name.text + " argument count", c.cur().line, c.cur().col);
      auto arg = expr(sig.inputs[n].atom);
      if (arg->atom != sig.inputs[n].atom) {
        throw Fail("bind", name.text + " argument is " + arg->atom->name + ", the parameter is " + sig.inputs[n].atom->name,
                   arg->line, arg->col);
      }
      node->args.push_back(std::move(arg));
      ++n;
      if (c.eat(TokenKind::RBracket)) break;
      c.expect(TokenKind::Comma, ",");
    }
  }
  if (n != sig.inputs.size()) throw Fail("bind", name.text + " argument count", name.line, name.col);
  if (sig.outputs.empty()) {
    if (as_value) throw Fail("bind", "'" + name.text + "' has no result", name.line, name.col);
    node->atom = nullptr;
    return node;
  }
  node->atom = sig.outputs[0].atom;
  return node;
}

std::unique_ptr<Expr> Binder::unary(const Atom* expect) {
  if (c.cur().kind == TokenKind::Amp) {
    Token op = c.cur();
    ++c.i;
    if (c.cur().kind != TokenKind::Ident || (c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::KwIn)) {
      throw Fail("bind", "& needs a cell", op.line, op.col);
    }
    auto found = locals.find(c.cur().text);
    if (found == locals.end()) throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Addr;
    node->name = c.cur().text;
    node->atom = types.pointer_to(found->second.atom);
    node->line = op.line;
    node->col = op.col;
    ++c.i;
    return node;
  }
  if (c.cur().kind == TokenKind::Star) {
    Token op = c.cur();
    ++c.i;
    auto inner = unary(nullptr);
    if (!inner->atom || inner->atom->family != Family::Pointer || !inner->atom->pointee) {
      throw Fail("bind", "* of a value that is not a pointer", op.line, op.col);
    }
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Load;
    node->atom = inner->atom->pointee;
    node->lhs = std::move(inner);
    node->line = op.line;
    node->col = op.col;
    return node;
  }
  if (c.cur().kind == TokenKind::Not) {
    Token op = c.cur();
    ++c.i;
    auto inner = unary(types.find("bool"));
    if (!inner->atom || inner->atom->name != "bool") throw Fail("bind", "! needs a bool", op.line, op.col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Not;
    node->atom = types.find("bool");
    node->lhs = std::move(inner);
    node->line = op.line;
    node->col = op.col;
    return node;
  }
  if (c.cur().kind == TokenKind::Minus && (c.i + 1 < c.toks.size()) && c.toks[c.i + 1].kind == TokenKind::Number &&
      !c.toks[c.i + 1].is_float) {
    Token op = c.cur();
    const Token& num = c.toks[c.i + 1];
    if (expect && expect->family == Family::SignedInt && expect->bits > 0 && expect->bits <= 64) {
      uint64_t mag = 1ull << (expect->bits - 1);
      if (num.bits == mag) throw Fail("bind", "unary minus of the lowest signed value", op.line, op.col);
    }
    ++c.i;
    auto inner = unary(expect);
    if (!inner->atom || inner->atom->family != Family::SignedInt) {
      throw Fail("bind", "negating a " + (inner->atom ? inner->atom->name : "value"), inner->line, inner->col);
    }
    inner->bits = static_cast<uint64_t>(-static_cast<int64_t>(inner->bits));
    return inner;
  }
  if (c.cur().kind == TokenKind::Minus) {
    Token op = c.cur();
    ++c.i;
    auto inner = unary(expect);
    if (!inner->atom || inner->atom->family != Family::SignedInt) {
      throw Fail("bind", "no row for unary minus of " + (inner->atom ? inner->atom->name : "this value"), op.line, op.col);
    }
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Neg;
    node->atom = inner->atom;
    node->lhs = std::move(inner);
    node->line = op.line;
    node->col = op.col;
    return node;
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
    if (c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::KwIn) return call_expr(expect, true);
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
    case ExprKind::Not:
      out += "!";
      dump_expr(out, *e.lhs);
      return;
    case ExprKind::Neg:
      out += "-";
      dump_expr(out, *e.lhs);
      return;
    case ExprKind::Call:
      out += e.name;
      out += " in[";
      for (size_t i = 0; i < e.args.size(); ++i) {
        if (i) out += ", ";
        dump_expr(out, *e.args[i]);
      }
      out += "]:";
      out += e.atom ? e.atom->name : "void";
      return;
    case ExprKind::Addr:
      out += "&";
      out += e.name;
      return;
    case ExprKind::Load:
      out += "*";
      dump_expr(out, *e.lhs);
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

Unit bind_unit(const std::vector<Token>& toks, TypeMap& types) {
  Binder binder{{toks, 0}, types, {}, {}};
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
        case StmtKind::If:
          out += "if ";
          dump_expr(out, *st.expr);
          break;
        case StmtKind::Loop:
          out += "loop";
          break;
        case StmtKind::Leave:
          out += "leave";
          break;
        case StmtKind::Store:
          out += "store ";
          dump_expr(out, *st.expr);
          out += " = ";
          dump_expr(out, *st.other);
          break;
        case StmtKind::CallStmt:
          out += "call ";
          dump_expr(out, *st.expr);
          break;
      }
      out += "\n";
    }
  }
  return out;
}

}  // namespace tp
