#include "tp/arch.hpp"
#include "tp/bytecode.hpp"

#include <cstring>
#include <unordered_map>

namespace tp {
namespace {

struct RangePat {
  std::string name;
  const Atom* atom = nullptr;
  int64_t lo = 0;
  int64_t hi = 0;
};

struct Side {
  std::string name;
  const Atom* atom = nullptr;
  int64_t value = 0;
  bool name_on_left = false;
  bool lower = false;
  bool upper = false;
};

bool side_of(const Expr& e, Side& s) {
  if (e.kind != ExprKind::Bin || (e.op != ">=" && e.op != "<=")) return false;
  const Expr* name = nullptr;
  const Expr* imm = nullptr;
  if (e.lhs->kind == ExprKind::Name && e.rhs->kind == ExprKind::Imm && !e.rhs->is_float) {
    name = e.lhs.get();
    imm = e.rhs.get();
    s.name_on_left = true;
  } else if (e.rhs->kind == ExprKind::Name && e.lhs->kind == ExprKind::Imm && !e.lhs->is_float) {
    name = e.rhs.get();
    imm = e.lhs.get();
    s.name_on_left = false;
  } else {
    return false;
  }
  if (!name->atom) return false;
  if (name->atom->family != Family::SignedInt && name->atom->family != Family::UnsignedInt) return false;
  if (name->atom->bits != 32 && name->atom->bits != 64) return false;
  s.name = name->name;
  s.atom = name->atom;
  s.value = static_cast<int64_t>(imm->bits);
  /* x >= A, or A <= x, is a lower bound. x <= B, or B >= x, is an upper bound. */
  s.lower = (s.name_on_left && e.op == ">=") || (!s.name_on_left && e.op == "<=");
  s.upper = (s.name_on_left && e.op == "<=") || (!s.name_on_left && e.op == ">=");
  return s.lower || s.upper;
}

bool match_range(const Expr& e, RangePat& out) {
  if (e.kind != ExprKind::Bin || e.op != "&&" || !e.lhs || !e.rhs) return false;
  Side a, b;
  if (!side_of(*e.lhs, a) || !side_of(*e.rhs, b)) return false;
  if (a.name != b.name || a.atom != b.atom) return false;
  if (a.lower && b.upper) {
    out.lo = a.value;
    out.hi = b.value;
  } else if (b.lower && a.upper) {
    out.lo = b.value;
    out.hi = a.value;
  } else {
    return false;
  }
  out.name = a.name;
  out.atom = a.atom;
  return true;
}

bool range_fits(const RangePat& r) {
  if (r.atom->family == Family::UnsignedInt) return static_cast<uint64_t>(r.lo) <= static_cast<uint64_t>(r.hi);
  return r.lo <= r.hi;
}

void collect_names(const Expr& e, std::unordered_map<std::string, int>& reads) {
  switch (e.kind) {
    case ExprKind::Name:
      reads[e.name]++;
      break;
    case ExprKind::Not:
    case ExprKind::Neg:
      if (e.lhs) collect_names(*e.lhs, reads);
      break;
    case ExprKind::Bin:
      if (e.lhs) collect_names(*e.lhs, reads);
      if (e.rhs) collect_names(*e.rhs, reads);
      break;
    case ExprKind::Call:
      for (const auto& arg : e.args)
        if (arg) collect_names(*arg, reads);
      if (e.lhs) collect_names(*e.lhs, reads);
      break;
    case ExprKind::Addr:
      if (!e.name.empty()) reads[e.name]++;
      break;
    case ExprKind::Load:
    case ExprKind::Index:
    case ExprKind::Field:
    case ExprKind::Bump:
    case ExprKind::Cast:
      if (e.lhs) collect_names(*e.lhs, reads);
      if (e.rhs) collect_names(*e.rhs, reads);
      break;
    case ExprKind::State:
      if (!e.name.empty()) reads[e.name]++;
      break;
    case ExprKind::Imm:
      break;
  }
}

void range_walk(const Expr& e, int& range_n) {
  RangePat pat;
  if (match_range(e, pat)) {
    ++range_n;
    return;
  }
  switch (e.kind) {
    case ExprKind::Not:
    case ExprKind::Neg:
      if (e.lhs) range_walk(*e.lhs, range_n);
      break;
    case ExprKind::Bin:
      if (e.lhs) range_walk(*e.lhs, range_n);
      if (e.rhs) range_walk(*e.rhs, range_n);
      break;
    case ExprKind::Call:
      for (const auto& arg : e.args)
        if (arg) range_walk(*arg, range_n);
      if (e.lhs) range_walk(*e.lhs, range_n);
      break;
    case ExprKind::Load:
    case ExprKind::Index:
    case ExprKind::Field:
    case ExprKind::Bump:
    case ExprKind::Cast:
      if (e.lhs) range_walk(*e.lhs, range_n);
      if (e.rhs) range_walk(*e.rhs, range_n);
      break;
    case ExprKind::Imm:
    case ExprKind::Name:
    case ExprKind::Addr:
    case ExprKind::State:
      break;
  }
}

void reuse_one(const Expr& e, int& reuse_n) {
  std::unordered_map<std::string, int> reads;
  collect_names(e, reads);
  for (const auto& row : reads) {
    if (row.second >= 2) reuse_n += row.second - 1;
  }
}

bool is_plus_one(const Expr& e, const std::string& name) {
  if (e.kind != ExprKind::Bin || e.op != "+") return false;
  auto one = [](const Expr& x) { return x.kind == ExprKind::Imm && !x.is_float && x.bits == 1; };
  auto same = [&](const Expr& x) { return x.kind == ExprKind::Name && x.name == name; };
  return (same(*e.lhs) && one(*e.rhs)) || (same(*e.rhs) && one(*e.lhs));
}

int count_gets(const std::vector<Stmt>& body, const std::string& name, bool (*skip_loop)(const std::vector<Stmt>&, const std::string&));

bool is_cursor(const std::vector<Stmt>& body, const std::string& name) {
  int inc = 0;
  int other = 0;
  std::vector<const std::vector<Stmt>*> stack;
  stack.push_back(&body);
  while (!stack.empty()) {
    const std::vector<Stmt>* here = stack.back();
    stack.pop_back();
    for (const Stmt& st : *here) {
      if (st.kind == StmtKind::Assign && st.var.name == name) {
        if (st.expr && is_plus_one(*st.expr, name)) ++inc;
        else ++other;
      } else if (st.kind == StmtKind::Decl && st.var.name == name) {
        ++other;
      } else if (st.kind == StmtKind::If) {
        stack.push_back(&st.yes);
        stack.push_back(&st.no);
      } else if (st.kind == StmtKind::Loop) {
        stack.push_back(&st.yes);
      }
    }
  }
  return inc > 0 && other == 0;
}

int gets_in_expr(const Expr& e, const std::string& name) {
  std::unordered_map<std::string, int> reads;
  collect_names(e, reads);
  auto it = reads.find(name);
  return it == reads.end() ? 0 : it->second;
}

int count_gets(const std::vector<Stmt>& body, const std::string& name, bool (*skip_loop)(const std::vector<Stmt>&, const std::string&)) {
  int n = 0;
  for (const Stmt& st : body) {
    if (st.expr) n += gets_in_expr(*st.expr, name);
    if (st.kind == StmtKind::If) {
      n += count_gets(st.yes, name, skip_loop);
      n += count_gets(st.no, name, skip_loop);
    } else if (st.kind == StmtKind::Loop) {
      if (!skip_loop(st.yes, name)) n += count_gets(st.yes, name, skip_loop);
    }
  }
  return n;
}

void tally_exprs(const std::vector<Stmt>& body, int& drop_n, int& reuse_n, int& range_n) {
  for (const Stmt& st : body) {
    if (st.expr) {
      reuse_one(*st.expr, reuse_n);
      range_walk(*st.expr, range_n);
      if ((st.kind == StmtKind::Decl || st.kind == StmtKind::Assign) && st.expr->kind == ExprKind::Call) ++drop_n;
    }
    if (st.kind == StmtKind::If) {
      tally_exprs(st.yes, drop_n, reuse_n, range_n);
      tally_exprs(st.no, drop_n, reuse_n, range_n);
    } else if (st.kind == StmtKind::Loop) {
      tally_exprs(st.yes, drop_n, reuse_n, range_n);
    }
  }
}

void tally_loops(const std::vector<Stmt>& body, int& inc_n, int& forward_n) {
  for (const Stmt& st : body) {
    if (st.kind == StmtKind::If) {
      tally_loops(st.yes, inc_n, forward_n);
      tally_loops(st.no, inc_n, forward_n);
    } else if (st.kind == StmtKind::Loop) {
      tally_loops(st.yes, inc_n, forward_n);
      std::unordered_map<std::string, int> seen;
      std::vector<const std::vector<Stmt>*> stack = {&st.yes};
      while (!stack.empty()) {
        const std::vector<Stmt>* here = stack.back();
        stack.pop_back();
        for (const Stmt& inner : *here) {
          if ((inner.kind == StmtKind::Assign || inner.kind == StmtKind::Decl) && !inner.var.name.empty()) seen[inner.var.name] = 1;
          if (inner.kind == StmtKind::If) {
            stack.push_back(&inner.yes);
            stack.push_back(&inner.no);
          } else if (inner.kind == StmtKind::Loop) {
            stack.push_back(&inner.yes);
          }
        }
      }
      bool forward = false;
      for (const auto& row : seen) {
        if (!is_cursor(st.yes, row.first)) continue;
        forward = true;
        inc_n += count_gets(st.yes, row.first, is_cursor);
      }
      if (forward) ++forward_n;
    }
  }
}

void require_returns(const BcFunc& fn);

struct Lowerer {
  const Unit* unit = nullptr;
  BcFunc fn;
  std::unordered_map<std::string, uint32_t> slots;
  const std::unordered_map<std::string, uint32_t>* fn_of = nullptr;
  uint32_t nlab = 0;
  struct LoopFrame {
    uint32_t exit = 0;
    std::string name;
  };
  std::vector<LoopFrame> loops;

  uint32_t lab() { return nlab++; }

  void put(Insn in) { fn.code.push_back(std::move(in)); }

  void mark(uint32_t id) {
    Insn in;
    in.op = Op::Label;
    in.imm = id;
    put(in);
  }

  void jump(uint32_t id) {
    Insn in;
    in.op = Op::Jmp;
    in.imm = id;
    put(in);
  }

  void branch_false(uint32_t cond, uint32_t id, const Atom* atom) {
    Insn in;
    in.op = Op::Br;
    in.lhs = cond;
    in.imm = id;
    in.atom = atom;
    put(in);
  }

  uint32_t fresh(const std::string& name) {
    uint32_t id = fn.nslots++;
    fn.slot_name.push_back(name);
    fn.flag.push_back(0);
    return id;
  }

  void track(uint32_t slot, uint8_t flags) { fn.flag[slot] = flags; }

  static bool hasm(const VarShape& v, Mutator m) {
    for (Mutator x : v.mutators)
      if (x == m) return true;
    return false;
  }

  uint32_t imm_bits(const Atom* atom, uint64_t bits, int line) {
    uint32_t dst = fresh("imm");
    Insn in;
    in.op = Op::Imm;
    in.atom = atom;
    in.dst = dst;
    in.imm = bits;
    in.line = line;
    put(in);
    return dst;
  }

  uint32_t address_of_slot(uint32_t slot, int line) {
    uint32_t dst = fresh("addr");
    Insn in;
    in.op = Op::Addr;
    in.atom = unit->atom_i64;
    in.dst = dst;
    in.lhs = slot;
    in.line = line;
    put(in);
    return dst;
  }

  uint32_t slot_add(uint32_t lhs, uint32_t rhs, int line) {
    uint32_t dst = fresh("slot");
    Insn in;
    in.op = Op::SlotAdd;
    in.atom = unit->atom_i64;
    in.src = unit->atom_i64;
    in.dst = dst;
    in.lhs = lhs;
    in.rhs = rhs;
    in.line = line;
    put(in);
    return dst;
  }

  uint32_t address_expr(const Expr& e) {
    if (e.kind == ExprKind::Name) return address_of_slot(local(e.name), e.line);
    if (e.kind == ExprKind::Load) return emit_expr(*e.lhs);
    if (e.kind == ExprKind::Index) {
      uint32_t base = 0;
      if (e.lhs && e.lhs->kind == ExprKind::Name && (!e.lhs->atom || e.lhs->atom->family != Family::Pointer))
        base = address_of_slot(local(e.lhs->name), e.line);
      else
        base = emit_expr(*e.lhs);
      uint32_t ix = emit_expr(*e.rhs);
      if (!e.rhs->atom || e.rhs->atom->bits < 64) {
        uint32_t wide = fresh("idx");
        Insn cast;
        cast.op = Op::Cast;
        cast.atom = unit->atom_i64;
        cast.src = e.rhs->atom ? e.rhs->atom : unit->atom_i32;
        cast.dst = wide;
        cast.lhs = ix;
        cast.line = e.line;
        put(cast);
        ix = wide;
      }
      return slot_add(base, ix, e.line);
    }
    if (e.kind == ExprKind::Field) {
      uint32_t base = 0;
      if (e.aux == 1) base = emit_expr(*e.lhs);
      else if (e.lhs && e.lhs->kind == ExprKind::Name) base = address_of_slot(local(e.lhs->name), e.line);
      else base = address_expr(*e.lhs);
      if (e.bits == 0) return base;
      return slot_add(base, imm_bits(unit->atom_i64, e.bits, e.line), e.line);
    }
    throw Fail("bytecode", "not a place", e.line, e.col);
  }

  struct Located {
    bool direct = false;
    uint32_t id = 0;
  };

  Located locate(const Expr& e) {
    if (e.kind == ExprKind::Name) return {true, local(e.name)};
    if (e.kind == ExprKind::Index && e.lhs && e.lhs->kind == ExprKind::Name && e.rhs && e.rhs->kind == ExprKind::Imm && !e.rhs->is_float)
      return {true, local(e.lhs->name) + static_cast<uint32_t>(e.rhs->bits)};
    if (e.kind == ExprKind::Field && e.aux == 0 && e.lhs && e.lhs->kind == ExprKind::Name)
      return {true, local(e.lhs->name) + static_cast<uint32_t>(e.bits)};
    return {false, address_expr(e)};
  }

  uint32_t read_place(const Expr& e) {
    if (e.kind == ExprKind::Load) return emit_expr(e);
    Located loc = locate(e);
    uint32_t dst = fresh("ld");
    Insn in;
    in.dst = dst;
    in.line = e.line;
    in.atom = e.atom;
    if (loc.direct) {
      in.op = Op::Mov;
      in.lhs = loc.id;
    } else {
      in.op = Op::Load;
      in.src = unit->atom_i64;
      in.lhs = loc.id;
    }
    put(in);
    return dst;
  }

  void store_value(const Expr& place, uint32_t val, int line) {
    Located loc = locate(place);
    Insn in;
    in.atom = place.atom;
    in.line = line;
    if (loc.direct) {
      in.op = Op::Mov;
      in.dst = loc.id;
      in.lhs = val;
    } else {
      in.op = Op::Store;
      in.src = unit->atom_i64;
      in.lhs = loc.id;
      in.rhs = val;
      in.dst = 0xffffffffu;
    }
    put(in);
  }

  uint32_t emit_builtin(const Expr& e) {
    auto pick = [&](uint32_t lhs, uint32_t rhs, Op pred, uint32_t yes_v, uint32_t no_v) {
      uint32_t cond = fresh("cmp");
      Insn cmp;
      cmp.op = pred;
      cmp.atom = unit->atom_bool;
      cmp.src = e.args[0]->atom;
      cmp.dst = cond;
      cmp.lhs = lhs;
      cmp.rhs = rhs;
      cmp.line = e.line;
      put(cmp);
      uint32_t dst = fresh(e.name);
      uint32_t other = lab();
      uint32_t end = lab();
      branch_false(cond, other, unit->atom_bool);
      Insn y;
      y.op = Op::Mov;
      y.atom = e.atom;
      y.dst = dst;
      y.lhs = yes_v;
      y.line = e.line;
      put(y);
      jump(end);
      mark(other);
      Insn n;
      n.op = Op::Mov;
      n.atom = e.atom;
      n.dst = dst;
      n.lhs = no_v;
      n.line = e.line;
      put(n);
      mark(end);
      return dst;
    };
    if (e.name == "min" || e.name == "max") {
      uint32_t a = emit_expr(*e.args[0]);
      uint32_t b = emit_expr(*e.args[1]);
      if (e.name == "min") return pick(a, b, Op::Lt, a, b);
      return pick(a, b, Op::Gt, a, b);
    }
    if (e.name == "abs") {
      uint32_t a = emit_expr(*e.args[0]);
      uint32_t neg = fresh("neg");
      Insn n;
      n.op = Op::Neg;
      n.atom = e.atom;
      n.dst = neg;
      n.lhs = a;
      n.line = e.line;
      put(n);
      uint32_t zero = imm_bits(e.atom, 0, e.line);
      return pick(a, zero, Op::Lt, neg, a);
    }
    if (e.name == "clamp") {
      uint32_t x = emit_expr(*e.args[0]);
      uint32_t lo = emit_expr(*e.args[1]);
      uint32_t hi = emit_expr(*e.args[2]);
      uint32_t raised = pick(x, lo, Op::Gt, x, lo);
      return pick(raised, hi, Op::Lt, raised, hi);
    }
    if (e.name == "swap") {
      uint32_t p = emit_expr(*e.args[0]);
      uint32_t q = emit_expr(*e.args[1]);
      const Atom* pointee = e.args[0]->atom->pointee;
      uint32_t vp = fresh("sw");
      uint32_t vq = fresh("sw");
      Insn ld;
      ld.op = Op::Load;
      ld.atom = pointee;
      ld.src = e.args[0]->atom;
      ld.dst = vp;
      ld.lhs = p;
      ld.line = e.line;
      put(ld);
      Insn ld2 = ld;
      ld2.dst = vq;
      ld2.lhs = q;
      ld2.src = e.args[1]->atom;
      put(ld2);
      Insn st;
      st.op = Op::Store;
      st.atom = pointee;
      st.src = e.args[0]->atom;
      st.lhs = p;
      st.rhs = vq;
      st.dst = 0xffffffffu;
      st.line = e.line;
      put(st);
      Insn st2 = st;
      st2.lhs = q;
      st2.rhs = vp;
      st2.src = e.args[1]->atom;
      put(st2);
      return imm_bits(unit->atom_i32, 0, e.line);
    }
    throw Fail("bytecode", "builtin", e.line, e.col);
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
      case ExprKind::Not: {
        uint32_t lhs = emit_expr(*e.lhs);
        uint32_t dst = fresh("not");
        Insn in;
        in.op = Op::Not;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = lhs;
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Bin: {
        if (e.op == "&&" || e.op == "||") return emit_logic(e);
        if (e.op == "=") {
          uint32_t val = emit_expr(*e.rhs);
          store_value(*e.lhs, val, e.line);
          return val;
        }
        if (e.op == "$=" || e.op == "!$=") {
          emit_expr(*e.lhs);
          emit_expr(*e.rhs);
          return imm_bits(unit->atom_bool, e.op == "!$=" ? 1 : 0, e.line);
        }
        uint32_t lhs = emit_expr(*e.lhs);
        uint32_t rhs = emit_expr(*e.rhs);
        uint32_t dst = fresh("bin");
        Insn in;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = lhs;
        in.rhs = rhs;
        in.line = e.line;
        in.src = e.lhs->atom;
        if (e.op == "+") in.op = Op::Add;
        else if (e.op == "-") in.op = Op::Sub;
        else if (e.op == "*") in.op = Op::Mul;
        else if (e.op == "/") in.op = Op::Div;
        else if (e.op == "%") in.op = Op::Rem;
        else if (e.op == "/_/") in.op = Op::FloorDiv;
        else if (e.op == "**") in.op = Op::Pow;
        else if (e.op == "&") in.op = Op::And;
        else if (e.op == "|") in.op = Op::Or;
        else if (e.op == "^") in.op = Op::Xor;
        else if (e.op == "<<") in.op = Op::Shl;
        else if (e.op == ">>") in.op = Op::Shr;
        else if (e.op == "==") in.op = Op::Eq;
        else if (e.op == "!=") in.op = Op::Ne;
        else if (e.op == "<") in.op = Op::Lt;
        else if (e.op == "<=") in.op = Op::Le;
        else if (e.op == ">") in.op = Op::Gt;
        else if (e.op == ">=") in.op = Op::Ge;
        else throw Fail("bytecode", "no opcode for " + e.op, e.line, e.col);
        put(in);
        return dst;
      }
      case ExprKind::Neg: {
        uint32_t lhs = emit_expr(*e.lhs);
        uint32_t dst = fresh(e.op == "~" ? "not" : "neg");
        Insn in;
        in.op = e.op == "~" ? Op::BitNot : Op::Neg;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = lhs;
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Call: {
        if (e.aux == 2) {
          if (!fn_of) throw Fail("bytecode", "no function table", e.line, e.col);
          auto found = fn_of->find(e.name);
          if (found == fn_of->end()) throw Fail("bytecode", "'" + e.name + "' is not declared", e.line, e.col);
          uint32_t count = emit_expr(*e.args[0]);
          Insn fork;
          fork.op = Op::Fork;
          fork.imm = found->second;
          fork.lhs = count;
          fork.dst = 0xffffffffu;
          fork.line = e.line;
          put(fork);
          uint32_t dst = fresh("run");
          Insn started;
          started.op = Op::Start;
          started.atom = e.atom;
          started.dst = dst;
          started.imm = found->second;
          started.line = e.line;
          put(started);
          return dst;
        }
        if (e.aux == 1) {
          if (!fn_of) throw Fail("bytecode", "no function table", e.line, e.col);
          auto found = fn_of->find(e.name);
          if (found == fn_of->end()) throw Fail("bytecode", "'" + e.name + "' is not declared", e.line, e.col);
          uint32_t dst = fresh("start");
          Insn in;
          in.op = Op::Start;
          in.atom = e.atom;
          in.dst = dst;
          in.imm = found->second;
          in.line = e.line;
          put(in);
          return dst;
        }
        if (e.name == "saveArrayAsbmp") {
          uint32_t ptr = emit_expr(*e.args[0]);
          uint32_t width = emit_expr(*e.args[1]);
          uint32_t height = emit_expr(*e.args[2]);
          uint32_t dst = fresh("bmp");
          Insn in;
          in.op = Op::SaveBmp;
          in.atom = e.atom;
          in.src = e.args[0]->atom ? e.args[0]->atom->pointee : nullptr;
          in.dst = dst;
          in.lhs = ptr;
          in.args = {width, height};
          in.note = e.op;
          in.imm = e.bits;
          in.line = e.line;
          put(in);
          return dst;
        }
        if (e.name == "print") {
          uint32_t val = emit_expr(*e.args[0]);
          Insn in;
          in.op = Op::Print;
          in.atom = e.args[0]->atom;
          in.lhs = val;
          in.dst = 0xffffffffu;
          in.line = e.line;
          put(in);
          return imm_bits(unit->atom_i32, 0, e.line);
        }
        if (e.name == "min" || e.name == "max" || e.name == "clamp" || e.name == "abs" || e.name == "swap") return emit_builtin(e);
        if (e.name == "threads" || e.name == "freemem") {
          uint32_t dst = fresh(e.name);
          Insn in;
          in.op = e.name == "threads" ? Op::Threads : Op::Freemem;
          in.atom = e.atom;
          in.dst = dst;
          in.line = e.line;
          put(in);
          return dst;
        }
        if (!fn_of) throw Fail("bytecode", "no function table", e.line, e.col);
        auto found = fn_of->find(e.name);
        if (found == fn_of->end()) throw Fail("bytecode", "'" + e.name + "' is not declared", e.line, e.col);
        std::vector<uint32_t> args;
        args.reserve(e.args.size() + 1);
        if (e.lhs) {
          if (e.lhs->kind == ExprKind::Name && e.lhs->name == "HERE") {
            args.push_back(emit_expr(*e.lhs));
          } else if (e.lhs->kind == ExprKind::Name) {
            uint32_t dst = fresh("recv");
            Insn ad;
            ad.op = Op::Addr;
            ad.atom = unit->atom_i64;
            ad.dst = dst;
            ad.lhs = local(e.lhs->name);
            ad.line = e.line;
            put(ad);
            args.push_back(dst);
          } else {
            throw Fail("bytecode", "method receiver", e.line, e.col);
          }
        }
        for (const auto& arg : e.args) args.push_back(emit_expr(*arg));
        bool void_call = e.atom == nullptr;
        uint32_t dst = void_call ? 0xffffffffu : fresh(e.name);
        Insn in;
        in.op = Op::Call;
        in.atom = e.atom;
        in.dst = dst;
        in.imm = found->second;
        in.args = std::move(args);
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Addr: {
        uint32_t dst = fresh("addr");
        Insn in;
        in.op = Op::Addr;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = local(e.name);
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Load: {
        uint32_t ptr = emit_expr(*e.lhs);
        uint32_t dst = fresh("load");
        Insn in;
        in.op = Op::Load;
        in.atom = e.atom;
        in.src = e.lhs->atom;
        in.dst = dst;
        in.lhs = ptr;
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Index:
      case ExprKind::Field:
        return read_place(e);
      case ExprKind::Bump: {
        uint32_t old = read_place(*e.lhs);
        uint32_t one = imm_bits(e.atom, 1, e.line);
        uint32_t neu = fresh("bump");
        Insn in;
        in.op = e.op == "--" ? Op::Sub : Op::Add;
        in.atom = e.atom;
        in.src = e.atom;
        in.dst = neu;
        in.lhs = old;
        in.rhs = one;
        in.line = e.line;
        put(in);
        store_value(*e.lhs, neu, e.line);
        return e.aux == 1 ? neu : old;
      }
      case ExprKind::Cast: {
        uint32_t lhs = emit_expr(*e.lhs);
        uint32_t dst = fresh("as");
        Insn in;
        in.op = Op::Cast;
        in.atom = e.atom;
        in.src = e.lhs->atom;
        in.dst = dst;
        in.lhs = lhs;
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::State: {
        uint32_t dst = fresh("state");
        Insn in;
        in.op = Op::State;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = local(e.name);
        in.line = e.line;
        put(in);
        return dst;
      }
    }
    throw Fail("bytecode", "empty expression", e.line, e.col);
  }

  uint32_t emit_logic(const Expr& e) {
    uint32_t left = emit_expr(*e.lhs);
    uint32_t dst = fresh(e.op);
    uint32_t other = lab();
    uint32_t end = lab();
    if (e.op == "&&") {
      branch_false(left, other, e.atom);
      uint32_t right = emit_expr(*e.rhs);
      Insn mov;
      mov.op = Op::Mov;
      mov.atom = e.atom;
      mov.dst = dst;
      mov.lhs = right;
      put(mov);
      jump(end);
      mark(other);
      Insn zero;
      zero.op = Op::Imm;
      zero.atom = e.atom;
      zero.dst = dst;
      zero.imm = 0;
      put(zero);
      mark(end);
    } else {
      branch_false(left, other, e.atom);
      Insn one;
      one.op = Op::Imm;
      one.atom = e.atom;
      one.dst = dst;
      one.imm = 1;
      put(one);
      jump(end);
      mark(other);
      uint32_t right = emit_expr(*e.rhs);
      Insn mov;
      mov.op = Op::Mov;
      mov.atom = e.atom;
      mov.dst = dst;
      mov.lhs = right;
      put(mov);
      mark(end);
    }
    return dst;
  }

  void emit_stmts(const std::vector<Stmt>& body) {
    for (const Stmt& st : body) {
      switch (st.kind) {
        case StmtKind::Decl: {
          int n = st.var.length > 0 ? st.var.length : 1;
          bool scalar = st.var.atom && st.var.atom->family != Family::Object && st.var.length == 0;
          bool object = st.var.atom && st.var.atom->family == Family::Object;
          uint8_t flags = kFlagTracked;
          if (scalar) {
            if (hasm(st.var, Mutator::Ephemeral)) flags |= kFlagEphemeral;
            if (hasm(st.var, Mutator::Static) || hasm(st.var, Mutator::Config)) flags |= kFlagStatic;
            if (hasm(st.var, Mutator::Mutex)) flags |= kFlagMutex;
          }
          uint32_t base = fresh(st.var.name);
          slots[st.var.name] = base;
          track(base, flags);
          for (int i = 1; i < n; ++i) track(fresh(st.var.name), kFlagTracked);
          uint32_t skip = 0;
          if (flags & kFlagStatic) {
            skip = lab();
            Insn chk;
            chk.op = Op::StaticInit;
            chk.dst = base;
            chk.lhs = skip;
            chk.imm = 0;
            chk.atom = st.var.atom;
            chk.line = st.line;
            put(chk);
          }
          auto mov_at = [&](uint32_t slot, uint32_t val) {
            Insn mv;
            mv.op = Op::Mov;
            mv.atom = st.var.atom;
            mv.dst = slot;
            mv.lhs = val;
            mv.line = st.line;
            put(mv);
          };
          if (!st.list.empty()) {
            for (int i = 0; i < n; ++i) {
              uint32_t val = i < static_cast<int>(st.list.size()) ? emit_expr(*st.list[static_cast<size_t>(i)]) : imm_bits(st.var.atom, 0, st.line);
              mov_at(base + static_cast<uint32_t>(i), val);
            }
          } else if (st.expr) {
            mov_at(base, emit_expr(*st.expr));
          } else if (!object && st.var.length > 0) {
            for (int i = 0; i < n; ++i) mov_at(base + static_cast<uint32_t>(i), imm_bits(st.var.atom, 0, st.line));
          }
          if (flags & kFlagStatic) {
            Insn sav;
            sav.op = Op::StaticInit;
            sav.dst = base;
            sav.imm = 1;
            sav.atom = st.var.atom;
            sav.line = st.line;
            put(sav);
            mark(skip);
          }
          break;
        }
        case StmtKind::Assign: {
          uint32_t val = emit_expr(*st.expr);
          if (st.other) {
            store_value(*st.other, val, st.line);
          } else {
            Insn in;
            in.op = Op::Mov;
            in.atom = st.var.atom;
            in.dst = local(st.var.name);
            in.lhs = val;
            in.line = st.line;
            put(in);
          }
          break;
        }
        case StmtKind::Return: {
          if (!st.expr) {
            Insn in;
            in.op = Op::Ret;
            in.dst = 0xffffffffu;
            in.line = st.line;
            put(in);
            break;
          }
          uint32_t val = emit_expr(*st.expr);
          if (fn.shape.outputs.empty() || st.expr->atom != fn.shape.outputs[0].atom) {
            throw Fail("bytecode", "return atom does not match the function output", st.line, st.col);
          }
          Insn in;
          in.op = Op::Ret;
          in.atom = st.expr->atom;
          in.dst = val;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::If: {
          if (!st.expr || st.expr->atom->name != "bool") {
            throw Fail("bytecode", "if requires a bool", st.line, st.col);
          }
          RangePat pat;
          uint32_t other = lab();
          uint32_t end = lab();
          if (match_range(*st.expr, pat) && range_fits(pat)) {
            Insn in;
            in.op = Op::BrRange;
            in.atom = pat.atom;
            in.lhs = local(pat.name);
            in.imm = other;
            in.imm_lo = static_cast<uint64_t>(pat.lo);
            in.imm_hi = static_cast<uint64_t>(pat.hi);
            in.line = st.line;
            put(in);
          } else {
            uint32_t cond = emit_expr(*st.expr);
            branch_false(cond, other, st.expr->atom);
          }
          emit_stmts(st.yes);
          jump(end);
          mark(other);
          emit_stmts(st.no);
          mark(end);
          break;
        }
        case StmtKind::Loop: {
          uint32_t head = lab();
          uint32_t exit = lab();
          loops.push_back({exit, st.label});
          mark(head);
          emit_stmts(st.yes);
          jump(head);
          mark(exit);
          loops.pop_back();
          break;
        }
        case StmtKind::Leave: {
          if (loops.empty()) throw Fail("bytecode", "leave is not inside a loop", st.line, st.col);
          if (st.label.empty()) {
            jump(loops.back().exit);
            break;
          }
          bool found = false;
          for (size_t i = loops.size(); i > 0; --i) {
            if (loops[i - 1].name == st.label) {
              jump(loops[i - 1].exit);
              found = true;
              break;
            }
          }
          if (!found) throw Fail("bytecode", "leave is not inside a loop", st.line, st.col);
          break;
        }
        case StmtKind::Store: {
          uint32_t ptr = emit_expr(*st.expr);
          uint32_t val = emit_expr(*st.other);
          Insn in;
          in.op = Op::Store;
          in.lhs = ptr;
          in.rhs = val;
          in.atom = st.other->atom;
          in.src = st.expr->atom;
          in.dst = 0xffffffffu;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::CallStmt:
          emit_expr(*st.expr);
          break;
        case StmtKind::Do: {
          bool had_i = slots.count("i") != 0;
          uint32_t prev_i = had_i ? slots["i"] : 0;
          uint32_t idx = fresh("i");
          slots["i"] = idx;
          track(idx, kFlagTracked);
          Insn zero;
          zero.op = Op::Imm;
          zero.atom = unit->atom_i32;
          zero.dst = idx;
          zero.imm = 0;
          zero.line = st.line;
          put(zero);
          uint32_t count = emit_expr(*st.expr);
          uint32_t head = lab();
          uint32_t done = lab();
          loops.push_back({done, ""});
          mark(head);
          uint32_t cond = fresh("cmp");
          Insn lt;
          lt.op = Op::Lt;
          lt.atom = unit->atom_bool;
          lt.src = unit->atom_i32;
          lt.dst = cond;
          lt.lhs = idx;
          lt.rhs = count;
          lt.line = st.line;
          put(lt);
          branch_false(cond, done, unit->atom_bool);
          emit_stmts(st.yes);
          uint32_t one = imm_bits(unit->atom_i32, 1, st.line);
          uint32_t next = fresh("bump");
          Insn add;
          add.op = Op::Add;
          add.atom = unit->atom_i32;
          add.src = unit->atom_i32;
          add.dst = next;
          add.lhs = idx;
          add.rhs = one;
          add.line = st.line;
          put(add);
          Insn step;
          step.op = Op::Mov;
          step.atom = unit->atom_i32;
          step.dst = idx;
          step.lhs = next;
          step.line = st.line;
          put(step);
          jump(head);
          mark(done);
          loops.pop_back();
          emit_stmts(st.no);
          if (had_i) slots["i"] = prev_i;
          else slots.erase("i");
          break;
        }
        case StmtKind::Lock:
        case StmtKind::Unlock: {
          uint32_t addr = emit_expr(*st.expr);
          Insn in;
          in.op = st.kind == StmtKind::Lock ? Op::Lock : Op::Unlock;
          in.lhs = addr;
          in.dst = 0xffffffffu;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::Hold: {
          uint32_t addr = emit_expr(*st.expr);
          Insn in;
          in.op = Op::Lock;
          in.lhs = addr;
          in.dst = 0xffffffffu;
          in.line = st.line;
          put(in);
          emit_stmts(st.yes);
          in.op = Op::Unlock;
          put(in);
          break;
        }
        case StmtKind::Fork: {
          if (!fn_of || !unit) throw Fail("bytecode", "no function table", st.line, st.col);
          auto found = fn_of->find(st.label);
          if (found == fn_of->end()) throw Fail("bytecode", "'" + st.label + "' is not declared", st.line, st.col);
          const Function* worker = nullptr;
          for (const Function& f : unit->fns)
            if (f.shape.name == st.label) worker = &f;
          uint32_t count = 0;
          if (st.expr) count = emit_expr(*st.expr);
          else if (worker && worker->parallel > 0) count = imm_bits(unit->atom_i32, static_cast<uint64_t>(worker->parallel), st.line);
          else throw Fail("bytecode", "fork wants a count", st.line, st.col);
          std::vector<uint32_t> extras;
          for (const auto& arg : st.list) extras.push_back(emit_expr(*arg));
          Insn in;
          in.op = Op::Fork;
          in.imm = found->second;
          in.lhs = count;
          in.args = std::move(extras);
          in.dst = 0xffffffffu;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::Finish:
          break;
        case StmtKind::Time: {
          uint32_t t0 = fresh("t0");
          Insn c0;
          c0.op = Op::Clock;
          c0.atom = unit->atom_f64;
          c0.dst = t0;
          c0.line = st.line;
          put(c0);
          emit_stmts(st.yes);
          uint32_t t1 = fresh("t1");
          Insn c1 = c0;
          c1.dst = t1;
          put(c1);
          uint32_t dt = fresh("dt");
          Insn sub;
          sub.op = Op::Sub;
          sub.atom = unit->atom_f64;
          sub.src = unit->atom_f64;
          sub.dst = dt;
          sub.lhs = t1;
          sub.rhs = t0;
          sub.line = st.line;
          put(sub);
          uint32_t val = emit_expr(*st.expr);
          Insn watch;
          watch.op = Op::Watch;
          watch.atom = unit->atom_f64;
          watch.lhs = dt;
          watch.rhs = val;
          watch.dst = 0xffffffffu;
          watch.note = st.label;
          watch.line = st.line;
          put(watch);
          break;
        }
      }
    }
  }

  void run(const Function& src) {
    fn.shape = src.shape;
    fn.divzero = src.divzero;
    if (src.method && src.here) {
      VarShape here;
      here.name = "HERE";
      here.atom = src.here;
      fn.shape.inputs.insert(fn.shape.inputs.begin(), here);
    }
    tally_exprs(src.body, fn.drop_n, fn.reuse_n, fn.range_n);
    tally_loops(src.body, fn.inc_n, fn.forward_n);
    for (const VarShape& in : fn.shape.inputs) {
      uint32_t id = fresh(in.name);
      slots[in.name] = id;
      track(id, kFlagTracked);
    }
    if (src.require) {
      uint32_t cond = emit_expr(*src.require);
      uint32_t bad = lab();
      uint32_t ok = lab();
      branch_false(cond, bad, unit->atom_bool);
      jump(ok);
      mark(bad);
      Insn trap;
      trap.op = Op::Trap;
      trap.imm = 0;
      trap.line = src.require->line;
      put(trap);
      mark(ok);
    }
    emit_stmts(src.body);
    for (const VarShape& in : fn.shape.inputs) {
      if (in.atom && in.atom->family == Family::Pointer) fn.cell_ptr = true;
    }
    for (const Insn& in : fn.code) {
      if ((in.atom && in.atom->family == Family::Pointer) || (in.src && in.src->family == Family::Pointer)) fn.cell_ptr = true;
    }
    bool marked = false;
    for (Mutator m : fn.shape.mutators) {
      if (m == Mutator::Inlined) marked = true;
    }
    if (marked) {
      std::string why;
      auto scalar = [](const Atom* atom) {
        return atom && (atom->name == "i32" || atom->name == "i64" || atom->name == "bool");
      };
      if (!src.shape.outputs.empty() && !scalar(src.shape.outputs[0].atom)) why = "result";
      for (const VarShape& in : fn.shape.inputs) {
        if (in.atom && in.atom->family == Family::Pointer) why = "pointer";
        else if (!scalar(in.atom)) why = "parameter";
      }
      for (const Insn& in : fn.code) {
        if (in.op == Op::Call || in.op == Op::Print || in.op == Op::Watch || in.op == Op::SaveBmp) why = "call";
        else if (in.op == Op::Threads) why = "threads";
        else if (in.op == Op::Freemem) why = "freemem";
        else if (in.op == Op::Addr || in.op == Op::Load || in.op == Op::Store) why = "pointer";
      }
      if (!why.empty()) throw Fail("bytecode", "@inlined function is not pure (" + why + ")", src.body.empty() ? 0 : src.body[0].line, 0);
    }
    if (!src.shape.outputs.empty()) require_returns(fn);
  }
};

void require_returns(const BcFunc& fn) {
  std::vector<int> at;
  for (size_t i = 0; i < fn.code.size(); ++i) {
    if (fn.code[i].op != Op::Label) continue;
    if (fn.code[i].imm >= at.size()) at.resize(static_cast<size_t>(fn.code[i].imm) + 1, -1);
    at[fn.code[i].imm] = static_cast<int>(i);
  }
  std::vector<char> seen(fn.code.size(), 0);
  std::vector<size_t> work;
  if (!fn.code.empty()) work.push_back(0);
  bool off = fn.code.empty();
  bool ret = false;
  while (!work.empty()) {
    size_t ip = work.back();
    work.pop_back();
    if (ip >= fn.code.size()) {
      off = true;
      continue;
    }
    if (seen[ip]) continue;
    seen[ip] = 1;
    const Insn& in = fn.code[ip];
    if (in.op == Op::Ret || in.op == Op::Trap) {
      ret = true;
      continue;
    }
    if (in.op == Op::Jmp || in.op == Op::Br || in.op == Op::BrRange) {
      if (in.imm >= at.size() || at[static_cast<size_t>(in.imm)] < 0) {
        throw Fail("bytecode", fn.shape.name + " jumps to a missing label", in.line, 0);
      }
      if (in.op != Op::Jmp) work.push_back(ip + 1);
      work.push_back(static_cast<size_t>(at[static_cast<size_t>(in.imm)]));
      continue;
    }
    work.push_back(ip + 1);
  }
  if (off || !ret) throw Fail("bytecode", fn.shape.name + " does not return", 0, 0);
}

}  // namespace

Program lower(const Unit& unit) {
  std::unordered_map<std::string, uint32_t> fn_of;
  for (uint32_t i = 0; i < unit.fns.size(); ++i) fn_of[unit.fns[i].shape.name] = i;
  Program prog;
  for (const Function& fn : unit.fns) {
    Lowerer low;
    low.unit = &unit;
    low.fn_of = &fn_of;
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
    out += "device ";
    out += fn.shape.name;
    out += " run=cpu reason=";
    out += !fn.gpu_ops ? "host" : fn.scalar_ctrl ? "scalar" : "short";
    out += " gpu-ops=";
    out += fn.gpu_ops ? "yes" : "no";
    out += "\n";
    for (const Insn& in : fn.code) {
      out += "  t";
      out += std::to_string(in.dst);
      out += " = ";
      out += arch_op_name(in.op);
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
        case Op::Not:
        case Op::Neg:
        case Op::Ret:
          out += " t" + std::to_string(in.op == Op::Ret ? in.dst : in.lhs);
          break;
        case Op::Threads:
        case Op::Freemem:
          break;
        case Op::Call:
          out += " " + prog.fns[in.imm].shape.name;
          for (uint32_t arg : in.args) out += " t" + std::to_string(arg);
          break;
        case Op::BrRange:
          out += " L" + std::to_string(in.imm) + " t" + std::to_string(in.lhs) + " " +
                 std::to_string(static_cast<int64_t>(in.imm_lo)) + " " + std::to_string(static_cast<int64_t>(in.imm_hi));
          break;
        case Op::Jmp:
        case Op::Br:
        case Op::Label:
          out += " L" + std::to_string(in.imm);
          break;
        default:
          out += " t" + std::to_string(in.lhs) + " t" + std::to_string(in.rhs);
          break;
      }
      out += in.gpu ? " gpu=yes\n" : " gpu=no\n";
    }
  }
  return out;
}

}  // namespace tp
