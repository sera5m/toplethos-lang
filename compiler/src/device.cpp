#include "tp/arith.hpp"
#include "tp/bytecode.hpp"
#include "tp/jit.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace tp {
namespace {

uint64_t mask_bits(int bits) {
  if (bits >= 64) return ~0ull;
  return (1ull << bits) - 1ull;
}

int64_t signed_result(uint64_t bits, const Atom& atom) {
  if (atom.family == Family::Float && atom.name == "f64") {
    double d;
    std::memcpy(&d, &bits, 8);
    return static_cast<int64_t>(d);
  }
  uint64_t m = mask_bits(atom.bits);
  uint64_t v = bits & m;
  if (atom.family == Family::SignedInt && atom.bits < 64) {
    uint64_t sign = 1ull << (atom.bits - 1);
    if (v & sign) v |= ~m;
  }
  return static_cast<int64_t>(v);
}

int find_fn(const Program& prog, const std::string& name) {
  for (size_t i = 0; i < prog.fns.size(); ++i) {
    if (prog.fns[i].shape.name == name) return static_cast<int>(i);
  }
  return -1;
}

uint64_t host_threads() {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1 || n > 0x7fffffffL) throw Fail("interpret", "threads: the host cannot ask", 0, 0);
  return static_cast<uint64_t>(n);
}

uint64_t host_freemem() {
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long page = sysconf(_SC_PAGESIZE);
  if (pages < 0 || page < 0) throw Fail("interpret", "freemem: the host cannot ask", 0, 0);
  __int128 bytes = static_cast<__int128>(pages) * static_cast<__int128>(page);
  if (bytes < 0 || bytes > static_cast<__int128>(INT64_MAX)) throw Fail("interpret", "freemem: the host cannot ask", 0, 0);
  return static_cast<uint64_t>(bytes);
}

void note_call(Active* active, const Program& prog, int caller, int callee) {
  if (!active) return;
  active->calls[static_cast<size_t>(callee)]++;
  if (caller < 0) return;
  const std::string& name = prog.fns[static_cast<size_t>(caller)].shape.name;
  auto& row = active->callers[static_cast<size_t>(callee)];
  if (std::find(row.begin(), row.end(), name) == row.end()) row.push_back(name);
}

bool outside_range(const Insn& in, uint64_t raw) {
  int bits = in.atom->bits;
  uint64_t m = mask_bits(bits);
  uint64_t x = raw & m;
  uint64_t lo = in.imm_lo & m;
  uint64_t hi = in.imm_hi & m;
  uint64_t off = (x - lo) & m;
  uint64_t span = (hi - lo) & m;
  return off > span;
}

struct Mem {
  std::vector<uint64_t> cell;
  uint32_t alloc(uint32_t n) {
    if (cell.empty()) cell.push_back(0);
    uint32_t base = static_cast<uint32_t>(cell.size());
    cell.resize(static_cast<size_t>(base) + n, 0);
    return base;
  }
};

uint64_t run_fn(const Program& prog, int index, const std::vector<uint64_t>& args, int caller, Active* active, int& steps,
                Mem& mem, Jit* jit) {
  const BcFunc& fn = prog.fns[static_cast<size_t>(index)];
  note_call(active, prog, caller, index);
  if (args.size() != fn.shape.inputs.size()) throw Fail("interpret", fn.shape.name + " argument count", 0, 0);
  std::vector<int> at(1, -1);
  for (size_t i = 0; i < fn.code.size(); ++i) {
    if (fn.code[i].op != Op::Label) continue;
    if (fn.code[i].imm >= at.size()) at.resize(fn.code[i].imm + 1, -1);
    at[fn.code[i].imm] = static_cast<int>(i);
  }
  uint32_t base = mem.alloc(fn.nslots);
  for (size_t i = 0; i < args.size(); ++i) mem.cell[base + i] = args[i];
  auto at_slot = [&](uint32_t slot) -> uint64_t& { return mem.cell[base + slot]; };
  size_t ip = 0;
  while (ip < fn.code.size()) {
    if (++steps > 10000000) throw Fail("interpret", "stopped after 10000000 steps", 0, 0);
    const Insn& in = fn.code[ip];
    switch (in.op) {
      case Op::Imm:
        at_slot(in.dst) = in.imm;
        break;
      case Op::Mov:
        at_slot(in.dst) = at_slot(in.lhs);
        break;
      case Op::Not:
      case Op::Neg:
        at_slot(in.dst) = apply(in.op, *in.atom, at_slot(in.lhs), 0, "interpret");
        break;
      case Op::Label:
        break;
      case Op::Jmp:
        ip = static_cast<size_t>(at.at(in.imm));
        continue;
      case Op::Br:
        if ((at_slot(in.lhs) & 1ull) == 0) {
          ip = static_cast<size_t>(at.at(in.imm));
          continue;
        }
        break;
      case Op::BrRange:
        if (outside_range(in, at_slot(in.lhs))) {
          ip = static_cast<size_t>(at.at(in.imm));
          continue;
        }
        break;
      case Op::Threads:
        at_slot(in.dst) = host_threads();
        break;
      case Op::Freemem:
        at_slot(in.dst) = host_freemem();
        break;
      case Op::Addr:
        at_slot(in.dst) = static_cast<uint64_t>(base) + in.lhs;
        break;
      case Op::Load: {
        uint64_t addr = at_slot(in.lhs);
        if (addr == 0 || addr >= mem.cell.size()) throw Fail("interpret", "absent pointer", in.line, 0);
        at_slot(in.dst) = mem.cell[static_cast<size_t>(addr)];
        break;
      }
      case Op::Store: {
        uint64_t addr = at_slot(in.lhs);
        if (addr == 0 || addr >= mem.cell.size()) throw Fail("interpret", "absent pointer", in.line, 0);
        mem.cell[static_cast<size_t>(addr)] = at_slot(in.rhs);
        break;
      }
      case Op::Call: {
        std::vector<uint64_t> call_args;
        call_args.reserve(in.args.size());
        for (uint32_t arg : in.args) call_args.push_back(at_slot(arg));
        int callee = static_cast<int>(in.imm);
        uint64_t got = 0;
        if (jit && jit_native(*jit, prog, callee, call_args, got)) {
          note_call(active, prog, index, callee);
        } else {
          timespec ts{};
          clock_gettime(CLOCK_MONOTONIC, &ts);
          int64_t t0 = static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
          got = run_fn(prog, callee, call_args, index, active, steps, mem, jit);
          clock_gettime(CLOCK_MONOTONIC, &ts);
          int64_t t1 = static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
          if (jit && callee >= 0 && static_cast<size_t>(callee) < jit->slot.size()) {
            jit->slot[static_cast<size_t>(callee)].interp_us += t1 - t0;
            jit->slot[static_cast<size_t>(callee)].interp_calls++;
            jit_consider(*jit, prog, callee, call_args, got);
          }
        }
        if (in.atom && in.dst < fn.nslots) at_slot(in.dst) = got;
        break;
      }
      case Op::Ret:
        return at_slot(in.dst);
      default:
        try {
          at_slot(in.dst) = apply(in.op, *(in.src ? in.src : in.atom), at_slot(in.lhs), at_slot(in.rhs), "interpret");
        } catch (const Fail& err) {
          throw Fail("interpret", err.what(), in.line, 0);
        }
        break;
    }
    ++ip;
  }
  if (fn.shape.outputs.empty()) return 0;
  throw Fail("interpret", "'" + fn.shape.name + "' did not return", 0, 0);
}

}  // namespace

int64_t interpret(const Program& prog, const std::string& entry, Active* active, std::string* jit_log) {
  int index = find_fn(prog, entry);
  if (index < 0) throw Fail("interpret", "no function '" + entry + "'", 0, 0);
  const BcFunc& fn = prog.fns[static_cast<size_t>(index)];
  if (!fn.shape.outputs.empty() && fn.shape.outputs[0].atom->family == Family::Float) {
    throw Fail("interpret", "this runner prints an integer result; " + fn.shape.outputs[0].atom->name + " stays in the bytecode", 0, 0);
  }
  if (active) {
    active->calls.assign(prog.fns.size(), 0);
    active->callers.assign(prog.fns.size(), {});
  }
  int steps = 0;
  Mem mem;
  Jit jit;
  jit_prepare(jit, prog);
  uint64_t bits = run_fn(prog, index, {}, -1, active, steps, mem, &jit);
  if (jit_log && jit.any) *jit_log = jit_report(prog, jit);
  if (fn.shape.outputs.empty()) return 0;
  return signed_result(bits, *fn.shape.outputs[0].atom);
}

std::string active_report(const Program& prog, const Active& active) {
  std::string out;
  for (size_t i = 0; i < prog.fns.size(); ++i) {
    out += "symbol " + prog.fns[i].shape.name + " calls=" + std::to_string(i < active.calls.size() ? active.calls[i] : 0);
    out += " callers=";
    if (i >= active.callers.size() || active.callers[i].empty()) {
      out += "-";
    } else {
      for (size_t c = 0; c < active.callers[i].size(); ++c) {
        if (c) out += ",";
        out += active.callers[i][c];
      }
    }
    out += " cost=unknown\n";
  }
  return out;
}

}  // namespace tp
