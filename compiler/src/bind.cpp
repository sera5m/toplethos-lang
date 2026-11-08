#include "tp/arith.hpp"
#include "tp/unit.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

struct FieldInfo {
  std::string name;
  const Atom* atom = nullptr;
  int offset = 0;
};

struct ObjRec {
  const Atom* atom = nullptr;
  std::string parent;
  std::vector<FieldInfo> fields;
};

struct EnumRec {
  const Atom* atom = nullptr;
  int bits = 32;
  std::vector<std::pair<std::string, int64_t>> items;
};

bool has_mut(const VarShape& v, Mutator m) {
  for (Mutator x : v.mutators)
    if (x == m) return true;
  return false;
}

bool integer_fits(const Atom* atom, uint64_t bits) {
  if (!atom) return false;
  if (atom->family != Family::SignedInt && atom->family != Family::UnsignedInt) return false;
  if (atom->bits >= 64) return bits <= static_cast<uint64_t>(INT64_MAX);
  uint64_t limit = 1ull << (atom->bits - (atom->family == Family::SignedInt ? 1 : 0));
  if (atom->family == Family::SignedInt) return bits < limit;
  return bits < (1ull << atom->bits);
}

bool width_fits(int bits, int64_t v) {
  if (bits >= 64) return true;
  int64_t lim = int64_t(1) << (bits - 1);
  return v >= -lim && v < lim;
}

struct Binder {
  Cursor c;
  TypeMap& types;
  std::unordered_map<std::string, VarShape> locals;
  std::unordered_map<std::string, FnShape> fns;
  std::unordered_map<std::string, ObjRec> objs;
  std::unordered_map<std::string, EnumRec> enums;
  const Atom* ret_atom = nullptr;
  bool in_method = false;
  std::string current_obj;

  const Atom* atom_of(const Token& tok) {
    if (tok.kind != TokenKind::Ident) throw Fail("bind", "expected a type name", tok.line, tok.col);
    const Atom* atom = types.find(tok.text);
    if (!atom) throw Fail("bind", "'" + tok.text + "' is not in the type map", tok.line, tok.col);
    return atom;
  }

  const Atom* parse_type() {
    const Atom* atom = atom_of(c.cur());
    c.expect(TokenKind::Ident, "a type name");
    while (c.eat(TokenKind::Star)) atom = types.pointer_to(atom);
    return atom;
  }

  void must_store(const VarShape& v, int line, int col) {
    if (v.readonly) throw Fail("bind", "writing i in a do", line, col);
    if (has_mut(v, Mutator::Val) || has_mut(v, Mutator::Config)) throw Fail("bind", "a later store", line, col);
  }

  void mismatch(const Atom* got, const Atom* want, int line, int col, const char* what) {
    std::string g = got ? got->name : "nothing";
    std::string w = want ? want->name : "nothing";
    if ((got && got->family == Family::Enum) || (want && want->family == Family::Enum)) {
      throw Fail("bind", "cannot store " + g + " in " + w, line, col);
    }
    throw Fail("bind", std::string(what) + " " + g + ", the slot is " + w, line, col);
  }

  std::unique_ptr<Expr> name_expr(const std::string& name, const Atom* atom, int line, int col) {
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Name;
    node->name = name;
    node->atom = atom;
    node->line = line;
    node->col = col;
    return node;
  }

  std::unique_ptr<Expr> imm_int(const Atom* atom, uint64_t bits, int line, int col) {
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Imm;
    node->atom = atom;
    node->bits = bits;
    node->line = line;
    node->col = col;
    return node;
  }

  void skip_pack() {
    c.expect(TokenKind::LBracket, "[");
    int depth = 1;
    while (depth > 0) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed attribute", c.cur().line, c.cur().col);
      if (c.cur().kind == TokenKind::LBracket) ++depth;
      else if (c.cur().kind == TokenKind::RBracket) --depth;
      ++c.i;
    }
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
  std::unique_ptr<Expr> power(const Atom* expect);
  std::unique_ptr<Expr> unary(const Atom* expect);
  std::unique_ptr<Expr> primary(const Atom* expect);
  std::unique_ptr<Expr> postfix(std::unique_ptr<Expr> node);
  std::unique_ptr<Expr> call_expr(const Atom* expect, bool as_value);
  std::unique_ptr<Expr> assign_rhs(const Atom* expect);
  std::vector<Stmt> block();
  void append_stmt(std::vector<Stmt>& out);
  Stmt stmt();

  void need_same(const Expr& a, const Expr& b, const Token& op) {
    if (a.atom != b.atom) {
      throw Fail("bind", std::string(op.text) + " needs two of the same atom, got " + a.atom->name + " and " + b.atom->name, op.line, op.col);
    }
  }

  std::unique_ptr<Expr> bin(std::unique_ptr<Expr> left, const Token& op, std::unique_ptr<Expr> right) {
    bool en = (left->atom && left->atom->family == Family::Enum) || (right->atom && right->atom->family == Family::Enum);
    if (en) {
      bool cmp = op.text == "==" || op.text == "!=";
      if (!(cmp && left->atom == right->atom)) throw Fail("bind", "no row for " + (left->atom ? left->atom->name : std::string("enum")), op.line, op.col);
    }
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

  std::unique_ptr<Expr> as_addr(std::unique_ptr<Expr> e) {
    if (e->kind == ExprKind::Name && e->atom && e->atom->family != Family::Pointer) {
      auto node = std::make_unique<Expr>();
      node->kind = ExprKind::Addr;
      node->name = e->name;
      node->atom = types.pointer_to(e->atom);
      node->line = e->line;
      node->col = e->col;
      return node;
    }
    return e;
  }

  const VarShape* local_var(const std::string& name) {
    auto it = locals.find(name);
    if (it == locals.end()) return nullptr;
    return &it->second;
  }

  void guard_place(const Expr& place, int line, int col) {
    const Expr* p = &place;
    std::string root;
    if (p->kind == ExprKind::Name) root = p->name;
    else if ((p->kind == ExprKind::Index || p->kind == ExprKind::Field) && p->lhs && p->lhs->kind == ExprKind::Name) root = p->lhs->name;
    if (root.empty() || root == "HERE") return;
    const VarShape* v = local_var(root);
    if (v) must_store(*v, line, col);
  }

  ObjRec* object_of(const Atom* atom) {
    if (!atom || atom->family != Family::Object) return nullptr;
    auto it = objs.find(atom->name);
    if (it == objs.end()) return nullptr;
    return &it->second;
  }

  std::unique_ptr<Expr> field_access(std::unique_ptr<Expr> obj, const std::string& field, int line, int col) {
    const ObjRec* rec = nullptr;
    int indirect = 0;
    if (obj->kind == ExprKind::Name && obj->name == "HERE" && in_method) {
      auto it = objs.find(current_obj);
      if (it == objs.end()) throw Fail("bind", "HERE is not inside an object", line, col);
      rec = &it->second;
      indirect = 1;
    } else {
      rec = object_of(obj->atom);
    }
    if (!rec) throw Fail("bind", "no field on " + (obj->atom ? obj->atom->name : obj->name), line, col);
    const FieldInfo* found = nullptr;
    for (const FieldInfo& f : rec->fields)
      if (f.name == field) found = &f;
    if (!found && !rec->parent.empty()) {
      auto pit = objs.find(rec->parent);
      if (pit != objs.end()) {
        for (const FieldInfo& f : pit->second.fields)
          if (f.name == field) found = &f;
      }
    }
    if (!found) throw Fail("bind", "'" + field + "' is not a field", line, col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Field;
    node->name = field;
    node->atom = found->atom;
    node->bits = static_cast<uint64_t>(found->offset);
    node->aux = indirect;
    node->lhs = std::move(obj);
    node->line = line;
    node->col = col;
    return node;
  }

  std::string lookup_method(const std::string& type, const std::string& method) {
    std::string cursor = type;
    for (int n = 0; n < 8 && !cursor.empty(); ++n) {
      auto it = fns.find(cursor + "." + method);
      if (it != fns.end()) return it->first;
      auto ot = objs.find(cursor);
      if (ot == objs.end()) break;
      cursor = ot->second.parent;
    }
    return {};
  }

  std::unique_ptr<Expr> method_call(std::unique_ptr<Expr> recv, const std::string& method, int line, int col) {
    std::string type;
    if (recv->kind == ExprKind::Name && recv->name == "HERE" && in_method) type = current_obj;
    else if (recv->atom && recv->atom->family == Family::Object) type = recv->atom->name;
    else throw Fail("bind", "no method on this value", line, col);
    std::string fn = lookup_method(type, method);
    if (fn.empty()) throw Fail("bind", "'" + method + "' is not declared", line, col);
    const FnShape& sig = fns[fn];
    c.expect(TokenKind::KwIn, "in");
    c.expect(TokenKind::LBracket, "[");
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Call;
    node->name = fn;
    node->lhs = std::move(recv);
    node->line = line;
    node->col = col;
    size_t n = 0;
    if (!c.eat(TokenKind::RBracket)) {
      for (;;) {
        if (n >= sig.inputs.size()) throw Fail("bind", method + " argument count", c.cur().line, c.cur().col);
        auto arg = expr(sig.inputs[n].atom);
        if (arg->atom != sig.inputs[n].atom) mismatch(arg->atom, sig.inputs[n].atom, arg->line, arg->col, "argument is");
        node->args.push_back(std::move(arg));
        ++n;
        if (c.eat(TokenKind::RBracket)) break;
        c.expect(TokenKind::Comma, ",");
      }
    }
    if (n != sig.inputs.size()) throw Fail("bind", method + " argument count", line, col);
    node->atom = sig.outputs.empty() ? nullptr : sig.outputs[0].atom;
    return node;
  }

  std::vector<std::unique_ptr<Expr>> loose_args() {
    std::vector<std::unique_ptr<Expr>> args;
    if (c.eat(TokenKind::RBracket)) return args;
    for (;;) {
      args.push_back(expr(nullptr));
      if (c.eat(TokenKind::RBracket)) break;
      c.expect(TokenKind::Comma, ",");
    }
    return args;
  }

  void parse_mutators(VarShape& var, bool& saw_mut) {
    saw_mut = false;
    while (c.cur().kind == TokenKind::At) {
      std::string m = c.cur().text;
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      if (m == "mut") saw_mut = true;
      else if (m == "static") var.mutators.push_back(Mutator::Static);
      else if (m == "config") var.mutators.push_back(Mutator::Config);
      else if (m == "val") var.mutators.push_back(Mutator::Val);
      else if (m == "xor") var.mutators.push_back(Mutator::Xor);
      else if (m == "ephemeral") var.mutators.push_back(Mutator::Ephemeral);
      else if (m == "mutex") var.mutators.push_back(Mutator::Mutex);
      else throw Fail("bind", "@" + m + " is not in this stage", line, col);
    }
    if (saw_mut && has_mut(var, Mutator::Val)) throw Fail("bind", "@mut written on @val", c.cur().line, c.cur().col);
  }

  std::unique_ptr<Expr> enum_ref() {
    std::string type = c.cur().text;
    int line = c.cur().line;
    int col = c.cur().col;
    ++c.i;
    c.expect(TokenKind::Colon, ":");
    c.expect(TokenKind::Colon, ":");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected an enumerator", c.cur().line, c.cur().col);
    std::string item = c.cur().text;
    ++c.i;
    auto it = enums.find(type);
    if (it == enums.end()) throw Fail("bind", "'" + type + "' is not declared", line, col);
    for (const auto& row : it->second.items) {
      if (row.first == item) return imm_int(it->second.atom, static_cast<uint64_t>(row.second), line, col);
    }
    throw Fail("bind", "'" + item + "' is not declared", line, col);
  }

  Function function(bool method) {
    Function fn;
    ret_atom = nullptr;
    c.expect(TokenKind::KwFn, "fn");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a function name", c.cur().line, c.cur().col);
    std::string raw = c.cur().text;
    Token name_tok = c.cur();
    ++c.i;
    fn.shape.name = method ? current_obj + "." + raw : raw;
    if (!method && (raw == "threads" || raw == "freemem")) throw Fail("bind", raw + " is a host fact", name_tok.line, name_tok.col);
    c.expect(TokenKind::KwIn, "in");
    c.expect(TokenKind::LBracket, "[");
    auto saved_locals = locals;
    if (!method) locals.clear();
    else {
      locals.clear();
    }
    if (!c.eat(TokenKind::RBracket)) {
      for (;;) {
        VarShape var;
        var.atom = parse_type();
        if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a parameter name", c.cur().line, c.cur().col);
        var.name = c.cur().text;
        ++c.i;
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
    bool ismut = false;
    while (c.cur().kind == TokenKind::At) {
      std::string m = c.cur().text;
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      if (m == "inlined") {
        for (Mutator have : fn.shape.mutators)
          if (have == Mutator::Inlined) throw Fail("bind", "@inlined is already set", line, col);
        fn.shape.mutators.push_back(Mutator::Inlined);
      } else if (m == "NoFailTolerance") {
        throw Fail("bind", "@NoFailTolerance is rejected", line, col);
      } else if (m == "gpuparallel") {
        throw Fail("bind", "@gpuparallel is rejected", line, col);
      } else if (m == "divzero") {
        c.expect(TokenKind::LBracket, "[");
        if (c.cur().kind == TokenKind::Number && c.cur().bits == 0) fn.divzero = 1;
        else if (c.cur().kind == TokenKind::Ident && c.cur().text == "max") fn.divzero = 2;
        else throw Fail("bind", "@divzero wants 0 or max", c.cur().line, c.cur().col);
        ++c.i;
        c.expect(TokenKind::RBracket, "]");
      } else if (m == "parallel") {
        c.expect(TokenKind::LBracket, "[");
        if (c.cur().kind != TokenKind::Number) throw Fail("bind", "@parallel wants a count", c.cur().line, c.cur().col);
        fn.parallel = static_cast<int>(c.cur().bits);
        ++c.i;
        c.expect(TokenKind::RBracket, "]");
      } else if (m == "require") {
        c.expect(TokenKind::LBracket, "[");
        fn.require = expr(types.find("bool"));
        if (!fn.require->atom || fn.require->atom->name != "bool") throw Fail("bind", "@require wants a bool", line, col);
        c.expect(TokenKind::RBracket, "]");
      } else if (m == "ismutationof") {
        ismut = true;
        skip_pack();
      } else {
        throw Fail("bind", "@" + m + " is not in this stage", line, col);
      }
    }
    if (method) {
      std::string parent = objs[current_obj].parent;
      std::string cursor = parent;
      bool overrides = false;
      while (!cursor.empty()) {
        if (fns.count(cursor + "." + raw)) overrides = true;
        cursor = objs[cursor].parent;
      }
      if (overrides && !ismut) throw Fail("bind", "without @ismutationof", name_tok.line, name_tok.col);
      fn.method = true;
      fn.here = types.find("i64");
    }
    if (fns.count(fn.shape.name)) throw Fail("bind", "'" + fn.shape.name + "' is already declared", name_tok.line, name_tok.col);
    fns[fn.shape.name] = fn.shape;
    bool saved_method = in_method;
    std::string saved_obj = current_obj;
    if (method) in_method = true;
    c.expect(TokenKind::LBrace, "{");
    while (!c.eat(TokenKind::RBrace)) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed function", c.cur().line, c.cur().col);
      append_stmt(fn.body);
    }
    in_method = saved_method;
    if (!method) current_obj = saved_obj;
    locals = std::move(saved_locals);
    ret_atom = nullptr;
    return fn;
  }

  void parse_object(std::vector<Function>& out) {
    c.expect(TokenKind::KwObject, "object");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected an object name", c.cur().line, c.cur().col);
    std::string name = c.cur().text;
    ++c.i;
    ObjRec rec;
    while (c.cur().kind == TokenKind::At) {
      if (c.cur().text != "childof") throw Fail("bind", "@" + c.cur().text + " is not in this stage", c.cur().line, c.cur().col);
      ++c.i;
      c.expect(TokenKind::LBracket, "[");
      if (c.cur().kind != TokenKind::Ident || !objs.count(c.cur().text)) {
        throw Fail("bind", "childof wants an object", c.cur().line, c.cur().col);
      }
      rec.parent = c.cur().text;
      rec.fields = objs[rec.parent].fields;
      ++c.i;
      c.expect(TokenKind::RBracket, "]");
    }
    rec.atom = types.make(name, Family::Object, 64, "object");
    objs[name] = rec;
    std::string saved = current_obj;
    current_obj = name;
    c.expect(TokenKind::LBrace, "{");
    while (!c.eat(TokenKind::RBrace)) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed object", c.cur().line, c.cur().col);
      if (c.cur().kind == TokenKind::KwFn) {
        out.push_back(function(true));
        continue;
      }
      const Atom* atom = parse_type();
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a field name", c.cur().line, c.cur().col);
      FieldInfo field;
      field.name = c.cur().text;
      field.atom = atom;
      field.offset = static_cast<int>(objs[name].fields.size());
      ++c.i;
      c.expect(TokenKind::Semicolon, ";");
      objs[name].fields.push_back(field);
    }
    current_obj = saved;
  }

  void parse_enum() {
    c.expect(TokenKind::KwEnum, "enum");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected an enum name", c.cur().line, c.cur().col);
    EnumRec rec;
    std::string name = c.cur().text;
    ++c.i;
    rec.bits = 32;
    if (c.cur().kind == TokenKind::Ident && c.cur().text.size() > 1 && c.cur().text[0] == 'e') {
      std::string w = c.cur().text.substr(1);
      if (w == "8" || w == "16" || w == "32" || w == "64") {
        rec.bits = std::stoi(w);
        ++c.i;
      }
    }
    c.expect(TokenKind::LBrace, "{");
    int64_t next = 0;
    std::unordered_set<int64_t> seen;
    while (c.cur().kind != TokenKind::RBrace) {
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected an enumerator", c.cur().line, c.cur().col);
      std::string item = c.cur().text;
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      int64_t value = next;
      if (c.eat(TokenKind::Eq)) {
        bool neg = c.eat(TokenKind::Minus);
        if (c.cur().kind != TokenKind::Number || c.cur().is_float) throw Fail("bind", "expected an enumerator value", c.cur().line, c.cur().col);
        value = static_cast<int64_t>(c.cur().bits);
        if (neg) value = -value;
        ++c.i;
      }
      if (!width_fits(rec.bits, value)) throw Fail("bind", "enumerator value does not fit", line, col);
      if (!seen.insert(value).second) throw Fail("bind", "two enumerators with the same value", line, col);
      rec.items.push_back({item, value});
      next = value + 1;
      if (!c.eat(TokenKind::Comma)) break;
    }
    c.expect(TokenKind::RBrace, "}");
    rec.atom = types.make(name, Family::Enum, rec.bits, "enum");
    enums[name] = std::move(rec);
  }

  void parse_match(std::vector<Stmt>& out) {
    int line = c.cur().line;
    int col = c.cur().col;
    ++c.i;
    bool total = false;
    if (c.cur().kind == TokenKind::At && c.cur().text == "total") {
      total = true;
      ++c.i;
    }
    c.expect(TokenKind::LBracket, "[");
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a match value", c.cur().line, c.cur().col);
    std::string scr = c.cur().text;
    const VarShape* var = local_var(scr);
    if (!var) throw Fail("bind", "'" + scr + "' is not declared", c.cur().line, c.cur().col);
    if (!var->atom || var->atom->family != Family::Enum) throw Fail("bind", "match wants an enum", c.cur().line, c.cur().col);
    auto eit = enums.find(var->atom->name);
    if (eit == enums.end()) throw Fail("bind", "match wants an enum", c.cur().line, c.cur().col);
    ++c.i;
    c.expect(TokenKind::RBracket, "]");
    c.expect(TokenKind::LBrace, "{");
    std::unordered_set<int64_t> seen;
    while (!c.eat(TokenKind::RBrace)) {
      if (c.cur().kind == TokenKind::End) throw Fail("bind", "unclosed match", c.cur().line, c.cur().col);
      auto arm_val = enum_ref();
      if (arm_val->atom != var->atom) throw Fail("bind", "no row for " + arm_val->atom->name, arm_val->line, arm_val->col);
      int64_t value = static_cast<int64_t>(arm_val->bits);
      seen.insert(value);
      c.expect(TokenKind::LBrace, "{");
      std::vector<Stmt> body = block();
      c.expect(TokenKind::RBrace, "}");
      Stmt iff;
      iff.kind = StmtKind::If;
      iff.line = arm_val->line;
      iff.col = arm_val->col;
      Token op;
      op.text = "==";
      op.line = arm_val->line;
      op.col = arm_val->col;
      int line = arm_val->line;
      int col = arm_val->col;
      auto cmp = bin(name_expr(scr, var->atom, line, col), op, std::move(arm_val));
      cmp->atom = types.find("bool");
      iff.expr = std::move(cmp);
      iff.yes = std::move(body);
      out.push_back(std::move(iff));
    }
    if (total) {
      for (const auto& item : eit->second.items) {
        if (!seen.count(item.second)) throw Fail("bind", "match is not total", line, col);
      }
    }
  }
};

std::vector<Stmt> Binder::block() {
  std::vector<Stmt> body;
  while (c.cur().kind != TokenKind::RBrace && c.cur().kind != TokenKind::End) append_stmt(body);
  if (c.cur().kind != TokenKind::RBrace) throw Fail("bind", "unclosed block", c.cur().line, c.cur().col);
  return body;
}

void Binder::append_stmt(std::vector<Stmt>& out) {
  if (c.cur().kind == TokenKind::KwMatch) {
    parse_match(out);
    return;
  }
  out.push_back(stmt());
}

Stmt Binder::stmt() {
  Stmt st;
  st.line = c.cur().line;
  st.col = c.cur().col;
  if (c.eat(TokenKind::KwReturn)) {
    st.kind = StmtKind::Return;
    if (!ret_atom) {
      if (c.cur().kind != TokenKind::Semicolon) throw Fail("bind", "no designated type out[]", st.line, st.col);
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    st.expr = expr(ret_atom);
    if (st.expr->atom != ret_atom) mismatch(st.expr->atom, ret_atom, st.line, st.col, "return is");
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.cur().kind == TokenKind::KwIf) {
    ++c.i;
    bool bracket = c.eat(TokenKind::LBracket);
    st.kind = StmtKind::If;
    st.expr = expr(nullptr);
    if (bracket) c.expect(TokenKind::RBracket, "]");
    if (st.expr->kind == ExprKind::State) {
      Token op;
      op.text = "==";
      op.line = st.expr->line;
      op.col = st.expr->col;
      auto one = imm_int(types.find("i32"), 1, st.expr->line, st.expr->col);
      st.expr = bin(std::move(st.expr), op, std::move(one));
      st.expr->atom = types.find("bool");
    }
    if (!st.expr->atom || st.expr->atom->name != "bool") throw Fail("bind", "condition is not bool", st.line, st.col);
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
    if (c.cur().kind == TokenKind::At && c.cur().text == "name") {
      ++c.i;
      c.expect(TokenKind::LBracket, "[");
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a loop name", c.cur().line, c.cur().col);
      st.label = c.cur().text;
      ++c.i;
      c.expect(TokenKind::RBracket, "]");
    }
    c.expect(TokenKind::LBrace, "{");
    st.yes = block();
    c.expect(TokenKind::RBrace, "}");
    return st;
  }
  if (c.eat(TokenKind::KwLeave)) {
    st.kind = StmtKind::Leave;
    if (c.eat(TokenKind::LBracket)) {
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a loop name", c.cur().line, c.cur().col);
      st.label = c.cur().text;
      ++c.i;
      c.expect(TokenKind::RBracket, "]");
    }
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.eat(TokenKind::KwDo)) {
    if (c.cur().kind != TokenKind::Ident || c.cur().text != "n") throw Fail("bind", "do wants n[count]", c.cur().line, c.cur().col);
    ++c.i;
    c.expect(TokenKind::LBracket, "[");
    st.kind = StmtKind::Do;
    st.expr = expr(types.find("i32"));
    c.expect(TokenKind::RBracket, "]");
    bool had_i = locals.count("i") != 0;
    VarShape prev_i;
    if (had_i) prev_i = locals["i"];
    VarShape idx;
    idx.name = "i";
    idx.atom = types.find("i32");
    idx.readonly = true;
    locals["i"] = idx;
    c.expect(TokenKind::LBrace, "{");
    st.yes = block();
    c.expect(TokenKind::RBrace, "}");
    if (c.cur().kind == TokenKind::Ident && c.cur().text == "onFinished") {
      ++c.i;
      c.expect(TokenKind::LBrace, "{");
      st.no = block();
      c.expect(TokenKind::RBrace, "}");
    }
    if (had_i) locals["i"] = prev_i;
    else locals.erase("i");
    return st;
  }
  if (c.eat(TokenKind::KwFork)) {
    st.kind = StmtKind::Fork;
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a worker", c.cur().line, c.cur().col);
    st.label = c.cur().text;
    if (!fns.count(st.label)) throw Fail("bind", "'" + st.label + "' is not declared", c.cur().line, c.cur().col);
    const FnShape& sig = fns[st.label];
    ++c.i;
    if (c.eat(TokenKind::KwIn)) {
      c.expect(TokenKind::LBracket, "[");
      st.expr = expr(types.find("i32"));
      c.expect(TokenKind::RBracket, "]");
    }
    if (c.cur().kind == TokenKind::Ident && c.cur().text == "with") {
      ++c.i;
      c.expect(TokenKind::LBracket, "[");
      size_t n = 1;
      if (!c.eat(TokenKind::RBracket)) {
        for (;;) {
          if (n >= sig.inputs.size()) throw Fail("bind", st.label + " argument count", c.cur().line, c.cur().col);
          auto arg = expr(nullptr);
          const Atom* want = sig.inputs[n].atom;
          if (want && want->family == Family::Pointer && arg->atom && arg->atom->family != Family::Pointer) arg = as_addr(std::move(arg));
          if (arg->atom != want) mismatch(arg->atom, want, arg->line, arg->col, "argument is");
          st.list.push_back(std::move(arg));
          ++n;
          if (c.eat(TokenKind::RBracket)) break;
          c.expect(TokenKind::Comma, ",");
        }
      }
      if (n != sig.inputs.size()) throw Fail("bind", st.label + " argument count", st.line, st.col);
    }
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.eat(TokenKind::KwFinish)) {
    st.kind = StmtKind::Finish;
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a worker", c.cur().line, c.cur().col);
    st.label = c.cur().text;
    ++c.i;
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  auto lock_like = [&](StmtKind kind) {
    st.kind = kind;
    c.expect(TokenKind::LBracket, "[");
    st.expr = as_addr(expr(nullptr));
    c.expect(TokenKind::RBracket, "]");
  };
  if (c.eat(TokenKind::KwLock)) {
    lock_like(StmtKind::Lock);
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.eat(TokenKind::KwUnlock)) {
    lock_like(StmtKind::Unlock);
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.eat(TokenKind::KwHold)) {
    lock_like(StmtKind::Hold);
    c.expect(TokenKind::LBrace, "{");
    st.yes = block();
    c.expect(TokenKind::RBrace, "}");
    return st;
  }
  if (c.cur().kind == TokenKind::At && c.cur().text == "time") {
    ++c.i;
    c.expect(TokenKind::LBracket, "[");
    if (c.cur().kind != TokenKind::String) throw Fail("bind", "time wants a name", c.cur().line, c.cur().col);
    st.kind = StmtKind::Time;
    st.label = c.cur().text;
    ++c.i;
    c.expect(TokenKind::RBracket, "]");
    if (c.cur().kind != TokenKind::Ident || c.cur().text != "watch") {
      throw Fail("bind", "time wants watch", c.cur().line, c.cur().col);
    }
    ++c.i;
    st.expr = expr(types.find("f64"));
    if (!st.expr->atom || st.expr->atom->name != "f64") throw Fail("bind", "watch wants an f64", st.line, st.col);
    c.expect(TokenKind::LBrace, "{");
    st.yes = block();
    c.expect(TokenKind::RBrace, "}");
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
    if (st.other->atom != st.expr->atom->pointee) mismatch(st.other->atom, st.expr->atom->pointee, st.line, st.col, "store of");
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.cur().kind == TokenKind::Ident && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::LParen) {
    throw Fail("bind", "name() is not a call", c.cur().line, c.cur().col);
  }
  if (c.cur().kind == TokenKind::Ident && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::KwIn && !types.find(c.cur().text)) {
    st.kind = StmtKind::CallStmt;
    st.expr = call_expr(nullptr, false);
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.cur().kind == TokenKind::Ident && !types.find(c.cur().text) && c.i + 1 < c.toks.size() &&
      (c.toks[c.i + 1].kind == TokenKind::Eq || c.toks[c.i + 1].kind == TokenKind::PlusEq)) {
    auto found = locals.find(c.cur().text);
    if (found == locals.end()) throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
    must_store(found->second, c.cur().line, c.cur().col);
    st.kind = StmtKind::Assign;
    st.var = found->second;
    bool plus = c.toks[c.i + 1].kind == TokenKind::PlusEq;
    ++c.i;
    ++c.i;
    if (plus) {
      auto rhs = expr(st.var.atom);
      Token op;
      op.text = "+";
      op.line = st.line;
      op.col = st.col;
      st.expr = bin(name_expr(st.var.name, st.var.atom, st.line, st.col), op, std::move(rhs));
    } else {
      st.expr = assign_rhs(st.var.atom);
    }
    if (st.expr->atom != st.var.atom) mismatch(st.expr->atom, st.var.atom, st.line, st.col, "assignment of");
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.cur().kind == TokenKind::Ident && !types.find(c.cur().text) && c.i + 1 < c.toks.size() &&
      (c.toks[c.i + 1].kind == TokenKind::LBracket || c.toks[c.i + 1].kind == TokenKind::Dot ||
       c.toks[c.i + 1].kind == TokenKind::PlusPlus || c.toks[c.i + 1].kind == TokenKind::MinusMinus)) {
    auto place = expr(nullptr);
    if (c.eat(TokenKind::Eq)) {
      guard_place(*place, st.line, st.col);
      const Atom* want = place->atom;
      st.kind = StmtKind::Assign;
      st.expr = assign_rhs(want);
      if (st.expr->atom != want) mismatch(st.expr->atom, want, st.line, st.col, "assignment of");
      st.other = std::move(place);
      c.expect(TokenKind::Semicolon, ";");
      return st;
    }
    st.kind = StmtKind::CallStmt;
    st.expr = std::move(place);
    c.expect(TokenKind::Semicolon, ";");
    return st;
  }
  if (c.cur().kind == TokenKind::PlusPlus || c.cur().kind == TokenKind::MinusMinus) {
    st.kind = StmtKind::CallStmt;
    st.expr = expr(nullptr);
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
    if (c.eat(TokenKind::LBracket)) {
      if (c.cur().kind != TokenKind::Number || c.cur().is_float) throw Fail("bind", "array length wants a count", c.cur().line, c.cur().col);
      st.var.length = static_cast<int>(c.cur().bits);
      ++c.i;
      c.expect(TokenKind::RBracket, "]");
    } else if (st.var.atom->family == Family::Object) {
      auto it = objs.find(st.var.atom->name);
      st.var.length = it == objs.end() ? 0 : static_cast<int>(it->second.fields.size());
    }
    bool saw_mut = false;
    parse_mutators(st.var, saw_mut);
    if (c.eat(TokenKind::Eq)) {
      if (c.eat(TokenKind::LBrace)) {
        if (!c.eat(TokenKind::RBrace)) {
          for (;;) {
            auto elem = expr(st.var.atom);
            if (elem->atom != st.var.atom) mismatch(elem->atom, st.var.atom, elem->line, elem->col, "initializer is");
            st.list.push_back(std::move(elem));
            if (c.eat(TokenKind::RBrace)) break;
            c.expect(TokenKind::Comma, ",");
          }
        }
      } else {
        st.expr = expr(st.var.atom);
        if (st.expr->atom != st.var.atom) mismatch(st.expr->atom, st.var.atom, st.line, st.col, "initializer is");
      }
    }
    c.expect(TokenKind::Semicolon, ";");
    locals[st.var.name] = st.var;
    return st;
  }
  throw Fail("bind", "expected a declaration, an assignment, or return", c.cur().line, c.cur().col);
}

std::unique_ptr<Expr> Binder::assign_rhs(const Atom* expect) {
  if (c.cur().kind == TokenKind::Ident && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::Eq) {
    auto found = locals.find(c.cur().text);
    if (found != locals.end() && found->second.atom == expect) {
      must_store(found->second, c.cur().line, c.cur().col);
      auto lhs = name_expr(found->second.name, found->second.atom, c.cur().line, c.cur().col);
      ++c.i;
      ++c.i;
      auto rhs = assign_rhs(expect);
      if (rhs->atom != expect) mismatch(rhs->atom, expect, rhs->line, rhs->col, "assignment of");
      int line = lhs->line;
      int col = lhs->col;
      auto node = std::make_unique<Expr>();
      node->kind = ExprKind::Bin;
      node->op = "=";
      node->atom = expect;
      node->lhs = std::move(lhs);
      node->rhs = std::move(rhs);
      node->line = line;
      node->col = col;
      return node;
    }
  }
  return expr(expect);
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
  while (c.cur().kind == TokenKind::EqEq || c.cur().kind == TokenKind::NotEq || c.cur().kind == TokenKind::DollarEq ||
         c.cur().kind == TokenKind::NotDollarEq) {
    Token op = c.cur();
    ++c.i;
    if (op.kind == TokenKind::DollarEq || op.kind == TokenKind::NotDollarEq) {
      auto right = relation(nullptr);
      auto node = std::make_unique<Expr>();
      node->kind = ExprKind::Bin;
      node->op = op.kind == TokenKind::DollarEq ? "$=" : "!$=";
      node->atom = types.find("bool");
      node->lhs = std::move(left);
      node->rhs = std::move(right);
      node->line = op.line;
      node->col = op.col;
      left = std::move(node);
      continue;
    }
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
    if (left->atom && left->atom->family == Family::Pointer) throw Fail("bind", "ordered compare of pointers", op.line, op.col);
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
  auto left = power(expect);
  while (c.cur().kind == TokenKind::Star || c.cur().kind == TokenKind::Slash || c.cur().kind == TokenKind::Percent ||
         c.cur().kind == TokenKind::SlashFloor) {
    Token op = c.cur();
    ++c.i;
    left = bin(std::move(left), op, power(left->atom));
  }
  return left;
}

std::unique_ptr<Expr> Binder::power(const Atom* expect) {
  auto left = unary(expect);
  if (c.cur().kind == TokenKind::StarStar) {
    Token op = c.cur();
    ++c.i;
    auto right = power(left->atom);
    left = bin(std::move(left), op, std::move(right));
  }
  return left;
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
  if (name.text == "print" || name.text == "min" || name.text == "max" || name.text == "clamp" || name.text == "abs" ||
      name.text == "swap") {
    auto args = loose_args();
    node->args = std::move(args);
    if (name.text == "print") {
      if (node->args.size() != 1) throw Fail("bind", "print argument count", name.line, name.col);
      node->atom = nullptr;
      if (as_value) throw Fail("bind", "print has no result", name.line, name.col);
      return node;
    }
    if (name.text == "swap") {
      if (node->args.size() != 2 || !node->args[0]->atom || node->args[0]->atom->family != Family::Pointer ||
          node->args[0]->atom != node->args[1]->atom) {
        throw Fail("bind", "swap wants two pointers of one atom", name.line, name.col);
      }
      node->atom = nullptr;
      if (as_value) throw Fail("bind", "swap has no result", name.line, name.col);
      return node;
    }
    if (name.text == "abs") {
      if (node->args.size() != 1 || !node->args[0]->atom) throw Fail("bind", "abs argument count", name.line, name.col);
      node->atom = node->args[0]->atom;
      return node;
    }
    size_t need = name.text == "clamp" ? 3 : 2;
    if (node->args.size() != need) throw Fail("bind", name.text + " argument count", name.line, name.col);
    node->atom = node->args[0]->atom;
    for (const auto& arg : node->args) {
      if (arg->atom != node->atom) mismatch(arg->atom, node->atom, arg->line, arg->col, "argument is");
    }
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
      if (arg->atom != sig.inputs[n].atom) mismatch(arg->atom, sig.inputs[n].atom, arg->line, arg->col, "argument is");
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
  if (c.cur().kind == TokenKind::PlusPlus || c.cur().kind == TokenKind::MinusMinus) {
    Token op = c.cur();
    ++c.i;
    auto place = primary(expect);
    guard_place(*place, op.line, op.col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Bump;
    node->op = op.text;
    node->aux = 1;
    node->atom = place->atom;
    node->lhs = std::move(place);
    node->line = op.line;
    node->col = op.col;
    return node;
  }
  if (c.cur().kind == TokenKind::Tilde) {
    Token op = c.cur();
    ++c.i;
    auto inner = unary(expect);
    if (!inner->atom || (inner->atom->family != Family::SignedInt && inner->atom->family != Family::UnsignedInt)) {
      throw Fail("bind", "no row for bitwise not", op.line, op.col);
    }
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Neg;
    node->op = "~";
    node->atom = inner->atom;
    node->lhs = std::move(inner);
    node->line = op.line;
    node->col = op.col;
    return node;
  }
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
    if (!inner->atom || (inner->atom->family != Family::SignedInt && inner->atom->name != "f64")) {
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
  return primary(expect);
}

std::unique_ptr<Expr> Binder::primary(const Atom* expect) {
  if (c.eat(TokenKind::LParen)) {
    auto inner = expr(expect);
    c.expect(TokenKind::RParen, ")");
    return postfix(std::move(inner));
  }
  if (c.cur().kind == TokenKind::KwAs) {
    int line = c.cur().line;
    int col = c.cur().col;
    ++c.i;
    c.expect(TokenKind::LBracket, "[");
    const Atom* dest = parse_type();
    c.expect(TokenKind::Comma, ",");
    auto inner = expr(nullptr);
    c.expect(TokenKind::RBracket, "]");
    if (!inner->atom) throw Fail("bind", "as with no row", line, col);
    if (inner->atom == dest) return postfix(std::move(inner));
    if (inner->kind == ExprKind::Imm && !inner->is_float && dest->name != "f64") {
      uint64_t bits = cast_bits(*inner->atom, *dest, inner->bits, "bind");
      inner->atom = dest;
      inner->bits = bits;
      inner->is_float = false;
      return postfix(std::move(inner));
    }
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Cast;
    node->atom = dest;
    node->lhs = std::move(inner);
    node->line = line;
    node->col = col;
    return postfix(std::move(node));
  }
  if (c.cur().kind == TokenKind::KwStart) {
    int line = c.cur().line;
    int col = c.cur().col;
    ++c.i;
    if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a worker", c.cur().line, c.cur().col);
    if (!fns.count(c.cur().text)) throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
    auto node = std::make_unique<Expr>();
    node->kind = ExprKind::Call;
    node->name = c.cur().text;
    node->aux = 1;
    node->atom = types.find("f64");
    node->line = line;
    node->col = col;
    ++c.i;
    return node;
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
    return postfix(std::move(node));
  }
  if (c.cur().kind == TokenKind::KwTrue || c.cur().kind == TokenKind::KwFalse) {
    auto node = imm_int(types.find("bool"), c.cur().kind == TokenKind::KwTrue ? 1 : 0, c.cur().line, c.cur().col);
    ++c.i;
    return postfix(std::move(node));
  }
  if (c.cur().kind == TokenKind::Ident) {
    if (c.cur().text == "run" && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::Ident) {
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      std::string worker = c.cur().text;
      int wline = c.cur().line;
      int wcol = c.cur().col;
      ++c.i;
      if (!fns.count(worker)) throw Fail("bind", "'" + worker + "' is not declared", wline, wcol);
      const FnShape& sig = fns[worker];
      if (sig.inputs.size() != 1 || !sig.inputs[0].atom || sig.inputs[0].atom->name != "i32") {
        throw Fail("bind", "run wants a worker with one i32 lane", wline, wcol);
      }
      if (sig.outputs.empty() || !sig.outputs[0].atom || sig.outputs[0].atom->name != "f64") {
        throw Fail("bind", "run wants an f64 worker", wline, wcol);
      }
      c.expect(TokenKind::KwIn, "in");
      c.expect(TokenKind::LBracket, "[");
      auto count = expr(types.find("i32"));
      if (!count->atom || count->atom->name != "i32") mismatch(count->atom, types.find("i32"), count->line, count->col, "count is");
      c.expect(TokenKind::RBracket, "]");
      auto node = std::make_unique<Expr>();
      node->kind = ExprKind::Call;
      node->name = worker;
      node->aux = 2;
      node->atom = types.find("f64");
      node->args.push_back(std::move(count));
      node->line = line;
      node->col = col;
      return node;
    }
    if (c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::KwIn) return postfix(call_expr(expect, true));
    if (c.cur().text == "state" && c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::LBracket) {
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      c.expect(TokenKind::LBracket, "[");
      if (c.cur().kind != TokenKind::Ident || !locals.count(c.cur().text)) {
        throw Fail("bind", "state wants a declared name", c.cur().line, c.cur().col);
      }
      auto node = std::make_unique<Expr>();
      node->kind = ExprKind::State;
      node->name = c.cur().text;
      node->atom = types.find("i32");
      node->line = line;
      node->col = col;
      ++c.i;
      c.expect(TokenKind::RBracket, "]");
      return node;
    }
    if (c.i + 1 < c.toks.size() && c.toks[c.i + 1].kind == TokenKind::Colon) return postfix(enum_ref());
    if (c.cur().text == "HERE" && in_method) {
      auto node = name_expr("HERE", types.find("i64"), c.cur().line, c.cur().col);
      ++c.i;
      return postfix(std::move(node));
    }
    auto found = locals.find(c.cur().text);
    if (found != locals.end()) {
      auto node = name_expr(c.cur().text, found->second.atom, c.cur().line, c.cur().col);
      ++c.i;
      return postfix(std::move(node));
    }
    if (c.cur().text == "absent" || c.cur().text == "present" || c.cur().text == "empty") {
      uint64_t bit = c.cur().text == "present" ? 1 : c.cur().text == "absent" ? 2 : 0;
      auto node = imm_int(types.find("i32"), bit, c.cur().line, c.cur().col);
      ++c.i;
      return postfix(std::move(node));
    }
    throw Fail("bind", "'" + c.cur().text + "' is not declared", c.cur().line, c.cur().col);
  }
  throw Fail("bind", "expected an expression", c.cur().line, c.cur().col);
}

std::unique_ptr<Expr> Binder::postfix(std::unique_ptr<Expr> node) {
  for (;;) {
    if (c.cur().kind == TokenKind::LBracket) {
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      auto index = expr(types.find("i32"));
      c.expect(TokenKind::RBracket, "]");
      if (!node->atom) throw Fail("bind", "index wants an array", line, col);
      auto next = std::make_unique<Expr>();
      next->kind = ExprKind::Index;
      next->atom = node->atom->family == Family::Pointer && node->atom->pointee ? node->atom->pointee : node->atom;
      next->lhs = std::move(node);
      next->rhs = std::move(index);
      next->line = line;
      next->col = col;
      node = std::move(next);
      continue;
    }
    if (c.cur().kind == TokenKind::Dot) {
      int line = c.cur().line;
      int col = c.cur().col;
      ++c.i;
      if (c.cur().kind != TokenKind::Ident) throw Fail("bind", "expected a field", c.cur().line, c.cur().col);
      std::string field = c.cur().text;
      ++c.i;
      if (c.cur().kind == TokenKind::KwIn) node = method_call(std::move(node), field, line, col);
      else node = field_access(std::move(node), field, line, col);
      continue;
    }
    if (c.cur().kind == TokenKind::PlusPlus || c.cur().kind == TokenKind::MinusMinus) {
      Token op = c.cur();
      ++c.i;
      guard_place(*node, op.line, op.col);
      auto bump = std::make_unique<Expr>();
      bump->kind = ExprKind::Bump;
      bump->op = op.text;
      bump->aux = 0;
      bump->atom = node->atom;
      bump->lhs = std::move(node);
      bump->line = op.line;
      bump->col = op.col;
      node = std::move(bump);
      continue;
    }
    break;
  }
  return node;
}

void dump_expr(std::string& out, const Expr& e) {
  switch (e.kind) {
    case ExprKind::Imm:
      out += e.atom ? e.atom->name : "?";
      out += " ";
      if (e.is_float) out += e.name;
      else out += std::to_string(static_cast<int64_t>(e.bits));
      return;
    case ExprKind::Name:
      out += e.name;
      out += ":";
      out += e.atom ? e.atom->name : "?";
      return;
    case ExprKind::Not:
      out += "!";
      dump_expr(out, *e.lhs);
      return;
    case ExprKind::Neg:
      out += e.op == "~" ? "~" : "-";
      dump_expr(out, *e.lhs);
      return;
    case ExprKind::Call:
      out += e.name;
      out += " in[";
      for (size_t i = 0; i < e.args.size(); ++i) {
        if (i) out += ", ";
        dump_expr(out, *e.args[i]);
      }
      out += "]";
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
      out += ")";
      return;
    case ExprKind::Index:
      dump_expr(out, *e.lhs);
      out += "[";
      dump_expr(out, *e.rhs);
      out += "]";
      return;
    case ExprKind::Field:
      dump_expr(out, *e.lhs);
      out += ".";
      out += e.name;
      return;
    case ExprKind::Bump:
      if (e.aux) out += e.op;
      dump_expr(out, *e.lhs);
      if (!e.aux) out += e.op;
      return;
    case ExprKind::Cast:
      out += "as ";
      dump_expr(out, *e.lhs);
      return;
    case ExprKind::State:
      out += "state ";
      out += e.name;
      return;
  }
}

std::vector<Token> expand_includes(const std::vector<Token>& in, int depth) {
  if (depth > 8) throw Fail("bind", "include is too deep", 1, 1);
  std::vector<Token> out;
  for (size_t i = 0; i < in.size();) {
    if (in[i].kind == TokenKind::KwInclude) {
      if (i + 2 >= in.size() || in[i + 1].kind != TokenKind::String) {
        throw Fail("bind", "include wants a string", in[i].line, in[i].col);
      }
      if (in[i + 2].kind != TokenKind::Semicolon) throw Fail("bind", "expected ;", in[i + 2].line, in[i + 2].col);
      std::ifstream file(in[i + 1].text);
      if (!file) throw Fail("bind", "cannot read " + in[i + 1].text, in[i].line, in[i].col);
      std::ostringstream ss;
      ss << file.rdbuf();
      auto more = tokenize(ss.str());
      if (!more.empty() && more.back().kind == TokenKind::End) more.pop_back();
      auto expanded = expand_includes(more, depth + 1);
      out.insert(out.end(), expanded.begin(), expanded.end());
      i += 3;
    } else {
      out.push_back(in[i]);
      ++i;
    }
  }
  return out;
}

}  // namespace

Unit bind_unit(const std::vector<Token>& src, TypeMap& types) {
  auto toks = expand_includes(src, 0);
  Binder binder{{toks, 0}, types, {}, {}, {}, {}, nullptr, false, {}};
  Unit unit;
  unit.atom_i32 = types.find("i32");
  unit.atom_i64 = types.find("i64");
  unit.atom_bool = types.find("bool");
  unit.atom_f64 = types.find("f64");
  while (binder.c.cur().kind != TokenKind::End) {
    if (binder.c.cur().kind == TokenKind::At && binder.c.cur().text == "globaloptimize") {
      ++binder.c.i;
      binder.skip_pack();
      binder.c.expect(TokenKind::Semicolon, ";");
      continue;
    }
    if (binder.c.cur().kind == TokenKind::KwNamespace) {
      throw Fail("bind", "namespace is not implemented on either host", binder.c.cur().line, binder.c.cur().col);
    }
    if (binder.c.cur().kind == TokenKind::KwObject) {
      binder.parse_object(unit.fns);
      continue;
    }
    if (binder.c.cur().kind == TokenKind::KwEnum) {
      binder.parse_enum();
      continue;
    }
    unit.fns.push_back(binder.function(false));
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
    if (!tok.text.empty() && (tok.kind == TokenKind::Ident || tok.kind == TokenKind::At)) {
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
          if (st.expr) {
            out += " = ";
            dump_expr(out, *st.expr);
          }
          break;
        case StmtKind::Assign:
          out += "set ";
          out += st.var.name;
          break;
        case StmtKind::Return:
          out += "return";
          if (st.expr) {
            out += " ";
            dump_expr(out, *st.expr);
          }
          break;
        case StmtKind::If:
          out += "if";
          break;
        case StmtKind::Loop:
          out += "loop";
          break;
        case StmtKind::Leave:
          out += "leave";
          break;
        case StmtKind::Store:
          out += "store";
          break;
        case StmtKind::CallStmt:
          out += "call";
          break;
        default:
          out += "stmt";
          break;
      }
      out += "\n";
    }
  }
  return out;
}

}  // namespace tp
