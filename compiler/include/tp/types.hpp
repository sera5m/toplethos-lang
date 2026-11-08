#pragma once

#include <deque>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tp {

/* One row of the type map. Lookup is by name. There is no switch on spellings. */
enum class Family : uint8_t {
  SignedInt,
  UnsignedInt,
  Float,
  Bfloat,
  Ufloat,
  Ternary,
  Bool,
  Char,
  Size,
  String,
  Pointer
};

enum class Mutator : uint8_t { Const, Mutex, Static, Ephemeral, Freeze, Inlined };

struct Atom {
  std::string name;
  Family family;
  int bits;
  std::string specifics;
  const Atom* pointee = nullptr;
  const char* ptr_kind = nullptr; /* null, or a name in kPointers. cell for T*. */
};

struct VarShape {
  std::string name;
  const Atom* atom = nullptr;
  std::vector<Mutator> mutators;
};

struct FnShape {
  std::string name;
  std::vector<VarShape> inputs;
  std::vector<VarShape> outputs;
  std::vector<Mutator> mutators;
};

class TypeMap {
 public:
  TypeMap();
  const Atom* find(std::string_view name) const;
  /* T* is a cell pointer. The address width is the host pointer width, 64. */
  const Atom* pointer_to(const Atom* pointee);
  const std::vector<Atom>& atoms() const { return atoms_; }
  static const char* family_name(Family f);
  static const char* mutator_name(Mutator m);

 private:
  std::vector<Atom> atoms_;
  std::deque<Atom> extra_;
  std::unordered_map<std::string, int> by_name_;
  std::unordered_map<std::string, const Atom*> extra_by_name_;
};

std::string shape_var(const VarShape& v);
std::string shape_fn(const FnShape& f);

}  // namespace tp
