#pragma once

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
  String
};

enum class Mutator : uint8_t { Const, Mutex, Static, Ephemeral, Freeze, Inlined };

struct Atom {
  std::string name;
  Family family;
  int bits;
  std::string specifics;
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
  const std::vector<Atom>& atoms() const { return atoms_; }
  static const char* family_name(Family f);
  static const char* mutator_name(Mutator m);

 private:
  std::vector<Atom> atoms_;
  std::unordered_map<std::string, int> by_name_;
};

std::string shape_var(const VarShape& v);
std::string shape_fn(const FnShape& f);

}  // namespace tp
