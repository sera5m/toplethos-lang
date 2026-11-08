#include "tp/arith.hpp"
#include "tp/bytecode.hpp"
#include "tp/jit.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unistd.h>
#include <utility>
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
  std::vector<uint8_t> live;
  std::vector<uint8_t> flag;
  std::vector<char> is_mutex;
  std::vector<int> locks;
  std::unordered_map<std::string, uint64_t> sticky;
  std::unordered_map<uint32_t, std::pair<uint64_t, std::vector<uint64_t>>> forks;
  bool* printed = nullptr;

  void grow() {
    size_t n = cell.size();
    if (live.size() < n) live.resize(n, 1);
    if (flag.size() < n) flag.resize(n, 0);
    if (is_mutex.size() < n) is_mutex.resize(n, 0);
    if (locks.size() < n) locks.resize(n, 0);
  }

  uint32_t alloc(uint32_t n) {
    if (cell.empty()) cell.push_back(0);
    uint32_t base = static_cast<uint32_t>(cell.size());
    cell.resize(static_cast<size_t>(base) + n, 0);
    grow();
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
  for (uint32_t i = 0; i < fn.nslots; ++i) {
    uint8_t f = i < fn.flag.size() ? fn.flag[i] : 0;
    mem.flag[base + i] = f;
    mem.live[base + i] = (f & kFlagTracked) ? 2 : 1;
    mem.is_mutex[base + i] = 0;
    mem.locks[base + i] = 0;
  }
  for (size_t i = 0; i < args.size(); ++i) {
    mem.cell[base + i] = args[i];
    mem.live[base + i] = 1;
  }
  auto at_slot = [&](uint32_t slot) -> uint64_t& { return mem.cell[base + slot]; };
  auto read_check = [&](uint32_t abs, int line) {
    if (abs >= mem.flag.size()) return;
    uint8_t f = mem.flag[abs];
    if ((f & kFlagMutex) && mem.is_mutex[abs] && mem.locks[abs] == 0) throw Fail("interpret", "outside lock or hold", line, 0);
    if (f & kFlagTracked) {
      if (mem.live[abs] == 2) throw Fail("interpret", "absent", line, 0);
      if (mem.live[abs] == 0 && (f & kFlagEphemeral)) throw Fail("interpret", "a second use of @ephemeral", line, 0);
    }
  };
  auto consume = [&](uint32_t abs) {
    if (abs >= mem.flag.size()) return;
    uint8_t f = mem.flag[abs];
    if ((f & kFlagTracked) && (f & kFlagEphemeral) && mem.live[abs] == 1) mem.live[abs] = 0;
  };
  auto note_slot = [&](uint32_t slot, int line) {
    uint32_t abs = base + slot;
    uint8_t f = slot < fn.flag.size() ? fn.flag[slot] : 0;
    if ((f & kFlagMutex) && mem.is_mutex[abs] && mem.locks[abs] == 0) throw Fail("interpret", "outside lock or hold", line, 0);
    if (f & kFlagTracked) mem.live[abs] = 1;
    if (f & kFlagMutex) mem.is_mutex[abs] = 1;
    if ((f & kFlagStatic) && slot < fn.slot_name.size()) mem.sticky[fn.shape.name + ":" + fn.slot_name[slot]] = mem.cell[abs];
  };
  auto note_abs = [&](uint32_t abs, int line) {
    if (abs == 0 || abs >= mem.cell.size()) throw Fail("interpret", "absent pointer", line, 0);
    uint8_t f = mem.flag[abs];
    if ((f & kFlagMutex) && mem.is_mutex[abs] && mem.locks[abs] == 0) throw Fail("interpret", "outside lock or hold", line, 0);
    if (f & kFlagTracked) mem.live[abs] = 1;
    if (f & kFlagMutex) mem.is_mutex[abs] = 1;
  };
  auto apply_write = [&](const Insn& in) {
    const Atom* numeric = in.src ? in.src : in.atom;
    try {
      at_slot(in.dst) = apply(in.op, *numeric, at_slot(in.lhs), in.op == Op::BitNot || in.op == Op::Not || in.op == Op::Neg ? 0 : at_slot(in.rhs), "interpret");
    } catch (const Fail& err) {
      if (fn.divzero && std::string(err.what()).find("division by zero") != std::string::npos) {
        at_slot(in.dst) = div_stand_in(*numeric, fn.divzero);
      } else {
        throw Fail("interpret", err.what(), in.line, 0);
      }
    }
    note_slot(in.dst, in.line);
  };
  size_t ip = 0;
  while (ip < fn.code.size()) {
    if (++steps > 10000000) throw Fail("interpret", "stopped after 10000000 steps", 0, 0);
    const Insn& in = fn.code[ip];
    switch (in.op) {
      case Op::Imm:
        at_slot(in.dst) = in.imm;
        note_slot(in.dst, in.line);
        break;
      case Op::Mov: {
        uint32_t src = base + in.lhs;
        read_check(src, in.line);
        uint64_t value = mem.cell[src];
        consume(src);
        at_slot(in.dst) = value;
        note_slot(in.dst, in.line);
        break;
      }
      case Op::Not:
      case Op::Neg:
      case Op::BitNot:
        apply_write(in);
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
        note_slot(in.dst, in.line);
        break;
      case Op::Freemem:
        at_slot(in.dst) = host_freemem();
        note_slot(in.dst, in.line);
        break;
      case Op::Addr:
        at_slot(in.dst) = static_cast<uint64_t>(base) + in.lhs;
        note_slot(in.dst, in.line);
        break;
      case Op::Load: {
        uint64_t addr = at_slot(in.lhs);
        if (addr == 0 || addr >= mem.cell.size()) throw Fail("interpret", "absent pointer", in.line, 0);
        read_check(static_cast<uint32_t>(addr), in.line);
        at_slot(in.dst) = mem.cell[static_cast<size_t>(addr)];
        consume(static_cast<uint32_t>(addr));
        note_slot(in.dst, in.line);
        break;
      }
      case Op::Store: {
        uint64_t addr = at_slot(in.lhs);
        note_abs(static_cast<uint32_t>(addr), in.line);
        mem.cell[static_cast<size_t>(addr)] = at_slot(in.rhs);
        if ((mem.flag[static_cast<size_t>(addr)] & kFlagTracked)) mem.live[static_cast<size_t>(addr)] = 1;
        if (mem.flag[static_cast<size_t>(addr)] & kFlagMutex) mem.is_mutex[static_cast<size_t>(addr)] = 1;
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
        if (in.atom && in.dst < fn.nslots) {
          at_slot(in.dst) = got;
          note_slot(in.dst, in.line);
        }
        break;
      }
      case Op::Ret:
        if (in.dst >= fn.nslots) return 0;
        return at_slot(in.dst);
      case Op::Print: {
        if (mem.printed) *mem.printed = true;
        const Atom& atom = *in.atom;
        uint64_t bits = at_slot(in.lhs);
        if (atom.family == Family::Bool || atom.name == "bool") {
          std::cout << ((bits & 1ull) ? "true\n" : "false\n");
        } else if (atom.family == Family::Float) {
          double d = 0;
          std::memcpy(&d, &bits, 8);
          char buf[64];
          std::snprintf(buf, sizeof buf, "%.9f", d);
          std::cout << buf << '\n';
        } else if (atom.family == Family::SignedInt || atom.family == Family::Enum) {
          std::cout << static_cast<long long>(signed_result(bits, atom.family == Family::Enum ? atom : atom)) << '\n';
        } else {
          uint64_t m = mask_bits(atom.bits);
          std::cout << static_cast<unsigned long long>(bits & m) << '\n';
        }
        break;
      }
      case Op::Cast:
        try {
          at_slot(in.dst) = cast_bits(*in.src, *in.atom, at_slot(in.lhs), "interpret");
        } catch (const Fail& err) {
          throw Fail("interpret", err.what(), in.line, 0);
        }
        note_slot(in.dst, in.line);
        break;
      case Op::State:
        at_slot(in.dst) = mem.live[base + in.lhs];
        note_slot(in.dst, in.line);
        break;
      case Op::StaticInit: {
        std::string key = fn.shape.name + ":" + (in.dst < fn.slot_name.size() ? fn.slot_name[in.dst] : "");
        if (in.imm == 0) {
          auto hit = mem.sticky.find(key);
          if (hit != mem.sticky.end()) {
            at_slot(in.dst) = hit->second;
            if (in.dst < fn.flag.size() && (fn.flag[in.dst] & kFlagTracked)) mem.live[base + in.dst] = 1;
            ip = static_cast<size_t>(at.at(in.lhs));
            continue;
          }
        } else {
          mem.sticky[key] = at_slot(in.dst);
        }
        break;
      }
      case Op::Fork: {
        std::vector<uint64_t> extra;
        extra.reserve(in.args.size());
        for (uint32_t arg : in.args) extra.push_back(at_slot(arg));
        mem.forks[static_cast<uint32_t>(in.imm)] = {at_slot(in.lhs), std::move(extra)};
        break;
      }
      case Op::Start: {
        auto hit = mem.forks.find(static_cast<uint32_t>(in.imm));
        uint64_t count = hit == mem.forks.end() ? 0 : hit->second.first;
        const std::vector<uint64_t> extra = hit == mem.forks.end() ? std::vector<uint64_t>{} : hit->second.second;
        double sum = 0;
        for (uint64_t lane = 0; lane < count; ++lane) {
          std::vector<uint64_t> call_args;
          call_args.push_back(lane);
          call_args.insert(call_args.end(), extra.begin(), extra.end());
          uint64_t got = run_fn(prog, static_cast<int>(in.imm), call_args, index, active, steps, mem, jit);
          double d = 0;
          std::memcpy(&d, &got, 8);
          sum += d;
        }
        uint64_t bits = 0;
        std::memcpy(&bits, &sum, 8);
        at_slot(in.dst) = bits;
        note_slot(in.dst, in.line);
        break;
      }
      case Op::Lock:
      case Op::Unlock: {
        uint64_t addr = at_slot(in.lhs);
        if (addr == 0 || addr >= mem.locks.size()) throw Fail("interpret", "absent pointer", in.line, 0);
        if (in.op == Op::Lock) mem.locks[static_cast<size_t>(addr)]++;
        else if (mem.locks[static_cast<size_t>(addr)] > 0) mem.locks[static_cast<size_t>(addr)]--;
        break;
      }
      case Op::Trap:
        throw Fail("interpret", "require failed", in.line, 0);
      default:
        apply_write(in);
        break;
    }
    ++ip;
  }
  if (fn.shape.outputs.empty()) return 0;
  throw Fail("interpret", "'" + fn.shape.name + "' did not return", 0, 0);
}

}  // namespace

int64_t interpret(const Program& prog, const std::string& entry, Active* active, std::string* jit_log, bool* printed) {
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
  mem.printed = printed;
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
