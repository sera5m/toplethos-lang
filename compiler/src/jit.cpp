#include "tp/jit.hpp"
#include "tp/target.hpp"

#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <time.h>
#include <unistd.h>

namespace tp {
namespace {

int64_t now_us() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

bool inlined_fn(const BcFunc& fn) {
  for (Mutator m : fn.shape.mutators) {
    if (m == Mutator::Inlined) return true;
  }
  return false;
}

bool scalar_atom(const Atom* atom) {
  return atom && (atom->name == "i32" || atom->name == "i64" || atom->name == "bool");
}

uint64_t call_native(void* code, const BcFunc& fn, const std::vector<uint64_t>& args) {
  bool wide_ret = !fn.shape.outputs.empty() && fn.shape.outputs[0].atom && fn.shape.outputs[0].atom->bits == 64;
  if (args.empty()) {
    if (fn.shape.outputs.empty()) {
      reinterpret_cast<void (*)()>(code)();
      return 0;
    }
    if (wide_ret) return static_cast<uint64_t>(reinterpret_cast<int64_t (*)()>(code)());
    return static_cast<uint32_t>(reinterpret_cast<int32_t (*)()>(code)());
  }
  bool wide_arg = fn.shape.inputs[0].atom && fn.shape.inputs[0].atom->bits == 64;
  if (fn.shape.outputs.empty()) {
    if (wide_arg) reinterpret_cast<void (*)(int64_t)>(code)(static_cast<int64_t>(args[0]));
    else reinterpret_cast<void (*)(int32_t)>(code)(static_cast<int32_t>(args[0]));
    return 0;
  }
  if (wide_ret && wide_arg) return static_cast<uint64_t>(reinterpret_cast<int64_t (*)(int64_t)>(code)(static_cast<int64_t>(args[0])));
  if (wide_ret) return static_cast<uint64_t>(reinterpret_cast<int64_t (*)(int32_t)>(code)(static_cast<int32_t>(args[0])));
  if (wide_arg) return static_cast<uint32_t>(reinterpret_cast<int32_t (*)(int64_t)>(code)(static_cast<int64_t>(args[0])));
  return static_cast<uint32_t>(reinterpret_cast<int32_t (*)(int32_t)>(code)(static_cast<int32_t>(args[0])));
}

uint64_t result_mask(const BcFunc& fn) {
  if (fn.shape.outputs.empty() || !fn.shape.outputs[0].atom || fn.shape.outputs[0].atom->bits >= 64) return ~0ull;
  return (1ull << fn.shape.outputs[0].atom->bits) - 1ull;
}

}  // namespace

void jit_prepare(Jit& jit, const Program& prog) {
  jit.slot.assign(prog.fns.size(), {});
  jit.any = false;
  for (size_t i = 0; i < prog.fns.size(); ++i) {
    jit.slot[i].inlined = inlined_fn(prog.fns[i]);
    if (jit.slot[i].inlined) jit.any = true;
  }
}

void jit_consider(Jit& jit, const Program& prog, int index, const std::vector<uint64_t>& args, uint64_t oracle) {
  if (index < 0 || static_cast<size_t>(index) >= jit.slot.size()) return;
  JitSlot& slot = jit.slot[static_cast<size_t>(index)];
  if (!slot.inlined || slot.dirty || slot.dropped) return;
  const BcFunc& fn = prog.fns[static_cast<size_t>(index)];
  if (args.size() > 1 || (!fn.shape.outputs.empty() && !scalar_atom(fn.shape.outputs[0].atom))) {
    slot.dropped = true;
    slot.check = "abi";
    return;
  }
  for (const VarShape& in : fn.shape.inputs) {
    if (!scalar_atom(in.atom)) {
      slot.dropped = true;
      slot.check = "abi";
      return;
    }
  }
  Program one;
  one.fns.push_back(fn);
  std::string text;
  try {
    text = emit(Target::Amd64, one);
  } catch (const Fail&) {
    slot.dropped = true;
    slot.check = "emit";
    return;
  }
  std::string stem = "/tmp/tpjit_" + std::to_string(static_cast<long long>(getpid())) + "_" + fn.shape.name;
  std::string asm_path = stem + ".s";
  std::string so_path = stem + ".so";
  {
    std::ofstream out(asm_path);
    if (!out) {
      slot.dropped = true;
      slot.check = "compile-failed";
      return;
    }
    out << text;
  }
  int64_t t0 = now_us();
  std::string cmd = "gcc -shared -fPIC -o " + so_path + " " + asm_path + " >/tmp/tpjit_gcc.err 2>&1";
  int rc = std::system(cmd.c_str());
  slot.compile_us = now_us() - t0;
  if (rc != 0) {
    slot.dropped = true;
    slot.check = "compile-failed";
    return;
  }
  void* page = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!page) {
    slot.dropped = true;
    slot.check = "compile-failed";
    return;
  }
  void* code = dlsym(page, fn.shape.name.c_str());
  if (!code) {
    dlclose(page);
    slot.dropped = true;
    slot.check = "compile-failed";
    return;
  }
  uint64_t got = call_native(code, fn, args);
  if ((got & result_mask(fn)) != (oracle & result_mask(fn))) {
    dlclose(page);
    slot.dropped = true;
    slot.check = "dropped";
    return;
  }
  slot.page = page;
  slot.code = code;
  slot.dirty = true;
  slot.check = "same";
}

bool jit_native(Jit& jit, const Program& prog, int index, const std::vector<uint64_t>& args, uint64_t& result) {
  if (index < 0 || static_cast<size_t>(index) >= jit.slot.size()) return false;
  JitSlot& slot = jit.slot[static_cast<size_t>(index)];
  if (!slot.dirty || !slot.code) return false;
  int64_t t0 = now_us();
  result = call_native(slot.code, prog.fns[static_cast<size_t>(index)], args);
  slot.native_us += now_us() - t0;
  slot.native_calls++;
  return true;
}

std::string jit_report(const Program& prog, const Jit& jit) {
  std::string out;
  for (size_t i = 0; i < prog.fns.size() && i < jit.slot.size(); ++i) {
    const JitSlot& slot = jit.slot[i];
    if (!slot.inlined && slot.interp_calls == 0 && slot.native_calls == 0) continue;
    const BcFunc& fn = prog.fns[i];
    const char* reason = !fn.gpu_ops ? "host" : fn.scalar_ctrl ? "scalar" : "short";
    out += "jit " + fn.shape.name + " run=cpu reason=" + reason;
    out += std::string(" gpu-ops=") + (fn.gpu_ops ? "yes" : "no");
    out += std::string(" dirty=") + (slot.inlined ? (slot.dirty ? "yes" : "-") : "-");
    out += std::string(" jit-fn=") + (slot.dirty ? "yes" : "-");
    out += " check=" + slot.check;
    out += " compile_us=" + std::to_string(slot.compile_us);
    out += " native_us=" + std::to_string(slot.native_us);
    out += " interp_us=" + std::to_string(slot.interp_us);
    out += " native_calls=" + std::to_string(slot.native_calls);
    out += "\n";
  }
  return out;
}

}  // namespace tp
