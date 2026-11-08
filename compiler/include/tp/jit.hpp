#pragma once

#include "tp/bytecode.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tp {

/* One dispatch slot. The program names the function. This record holds the
   other page and the flip. It is not a pointer the program can load. */
struct JitSlot {
  bool inlined = false;
  bool dirty = false;
  bool dropped = false;
  void* page = nullptr;
  void* code = nullptr;
  int64_t compile_us = 0;
  int64_t native_us = 0;
  int64_t interp_us = 0;
  uint64_t native_calls = 0;
  uint64_t interp_calls = 0;
  std::string check = "-";
};

struct Jit {
  std::vector<JitSlot> slot;
  bool any = false;
};

void jit_prepare(Jit& jit, const Program& prog);
/* After the interpreter has run this call. Compiles an @inlined function
   once, checks the native result against that oracle, then flips the slot. */
void jit_consider(Jit& jit, const Program& prog, int index, const std::vector<uint64_t>& args, uint64_t oracle);
bool jit_native(Jit& jit, const Program& prog, int index, const std::vector<uint64_t>& args, uint64_t& result);
std::string jit_report(const Program& prog, const Jit& jit);

}  // namespace tp
