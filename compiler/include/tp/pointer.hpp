#pragma once

#include <string>

namespace tp {

/* Expanded pointer list. Lookup is by kind name.
   The program loads and stores only `cell`. The other rows exist so FFI,
   @inlined, and the JIT can be told apart. They are not new spellings,
   and they are not capability pointers. SPIR-V keeps none of them:
   a shader does not keep a host pointer. `window` and `gather` are not rows. */

struct PtrKind {
  const char* name;
  const char* aims;
  const char* holder;
  bool program; /* the program may load and store through it */
  bool host;    /* the CPU host may hold the address */
  bool spirv;   /* a shader may keep it */
};

inline constexpr PtrKind kPointers[] = {
    {"cell", "a value cell: &cell, T*, *p. Absent is a failure. *p = v stores.", "the program", true, true, false},
    {"ffi", "the cell address handed to a foreign call. It stays valid for that call. The foreign function does not keep it unless the cell outlives the call.", "the host, for that call", false, true, false},
    {"inlined", "the compiled symbol of an @inlined function. The interpreter calls that symbol. A method, an effect, a pointer, or a call is not inlined.", "the host", false, true, false},
    {"jit-fn", "the dispatch slot of a JIT-compiled function. The other page is checked, then this pointer flips. The program names the function. It does not load this address.", "the JIT", false, true, false},
    {"jit-var", "a variable the JIT placed. Not a source name.", "the JIT", false, true, false},
    {"neuron", "a watched function. The watcher holds this. The program names the function.", "the watcher", false, false, false},
    {"manager", "the local record of one neuron: where it is, the dirty flag, the other page.", "the JIT", false, false, false},
};

inline const PtrKind* pointer_kind(const char* name) {
  if (!name) return nullptr;
  for (const PtrKind& row : kPointers) {
    if (std::string(name) == row.name) return &row;
  }
  return nullptr;
}

inline std::string dump_pointer_kinds() {
  std::string out = "stage pointers\n";
  out += "The pointer list is data. A kind is looked up. The program does not spell these names.\n";
  for (const PtrKind& row : kPointers) {
    out += "ptr ";
    out += row.name;
    out += " holder=";
    out += row.holder;
    out += " program=";
    out += row.program ? "yes" : "no";
    out += " host=";
    out += row.host ? "yes" : "no";
    out += " spirv=";
    out += row.spirv ? "yes" : "no";
    out += "\n";
    out += "  aims: ";
    out += row.aims;
    out += "\n";
  }
  return out;
}

}  // namespace tp
