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
      break;
    case ExprKind::Addr:
      if (!e.name.empty()) reads[e.name]++;
      break;
    case ExprKind::Load:
      if (e.lhs) collect_names(*e.lhs, reads);
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
      break;
    case ExprKind::Load:
      if (e.lhs) range_walk(*e.lhs, range_n);
      break;
    case ExprKind::Imm:
    case ExprKind::Name:
    case ExprKind::Addr:
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
  BcFunc fn;
  std::unordered_map<std::string, uint32_t> slots;
  const std::unordered_map<std::string, uint32_t>* fn_of = nullptr;
  uint32_t nlab = 0;
  std::vector<uint32_t> breaks;

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
    return id;
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
        uint32_t dst = fresh("neg");
        Insn in;
        in.op = Op::Neg;
        in.atom = e.atom;
        in.dst = dst;
        in.lhs = lhs;
        in.line = e.line;
        put(in);
        return dst;
      }
      case ExprKind::Call: {
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
        args.reserve(e.args.size());
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
          uint32_t id = fresh(st.var.name);
          slots[st.var.name] = id;
          uint32_t val = emit_expr(*st.expr);
          Insn in;
          in.op = Op::Mov;
          in.atom = st.var.atom;
          in.dst = id;
          in.lhs = val;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::Assign: {
          uint32_t val = emit_expr(*st.expr);
          Insn in;
          in.op = Op::Mov;
          in.atom = st.var.atom;
          in.dst = local(st.var.name);
          in.lhs = val;
          in.line = st.line;
          put(in);
          break;
        }
        case StmtKind::Return: {
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
          breaks.push_back(exit);
          mark(head);
          emit_stmts(st.yes);
          jump(head);
          mark(exit);
          breaks.pop_back();
          break;
        }
        case StmtKind::Leave:
          if (breaks.empty()) throw Fail("bytecode", "leave is not inside a loop", st.line, st.col);
          jump(breaks.back());
          break;
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
      }
    }
  }

  void run(const Function& src) {
    fn.shape = src.shape;
    tally_exprs(src.body, fn.drop_n, fn.reuse_n, fn.range_n);
    tally_loops(src.body, fn.inc_n, fn.forward_n);
    for (const VarShape& in : src.shape.inputs) {
      uint32_t id = fresh(in.name);
      slots[in.name] = id;
    }
    emit_stmts(src.body);
    for (const VarShape& in : src.shape.inputs) {
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
      for (const VarShape& in : src.shape.inputs) {
        if (in.atom && in.atom->family == Family::Pointer) why = "pointer";
        else if (!scalar(in.atom)) why = "parameter";
      }
      for (const Insn& in : fn.code) {
        if (in.op == Op::Call) why = "call";
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
    if (in.op == Op::Ret) {
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
