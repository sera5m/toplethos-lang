#include "tp/types.hpp"

namespace tp {
namespace {

void add_atom(std::vector<Atom>& out, std::string name, Family family, int bits, std::string specifics) {
  out.push_back(Atom{std::move(name), family, bits, std::move(specifics)});
}

}  // namespace

TypeMap::TypeMap() {
  add_atom(atoms_, "i4", Family::SignedInt, 4, "signed, wraps");
  add_atom(atoms_, "i8", Family::SignedInt, 8, "signed, wraps");
  add_atom(atoms_, "i16", Family::SignedInt, 16, "signed, wraps");
  add_atom(atoms_, "i32", Family::SignedInt, 32, "signed, wraps");
  add_atom(atoms_, "i64", Family::SignedInt, 64, "signed, wraps");
  add_atom(atoms_, "ui4", Family::UnsignedInt, 4, "unsigned, wraps");
  add_atom(atoms_, "ui8", Family::UnsignedInt, 8, "unsigned, wraps");
  add_atom(atoms_, "ui16", Family::UnsignedInt, 16, "unsigned, wraps");
  add_atom(atoms_, "ui32", Family::UnsignedInt, 32, "unsigned, wraps");
  add_atom(atoms_, "ui64", Family::UnsignedInt, 64, "unsigned, wraps");
  add_atom(atoms_, "f8e4m3", Family::Float, 8, "1 sign, 4 exponent, 3 mantissa");
  add_atom(atoms_, "f8e5m2", Family::Float, 8, "1 sign, 5 exponent, 2 mantissa");
  add_atom(atoms_, "f16", Family::Float, 16, "ieee binary16");
  add_atom(atoms_, "f32", Family::Float, 32, "ieee binary32");
  add_atom(atoms_, "f64", Family::Float, 64, "ieee binary64");
  add_atom(atoms_, "bf16", Family::Bfloat, 16, "f32 exponent, 7 mantissa");
  add_atom(atoms_, "uf8e4m3", Family::Ufloat, 8, "unsigned f8e4m3, sign bit must be 0");
  add_atom(atoms_, "uf8e5m2", Family::Ufloat, 8, "unsigned f8e5m2, sign bit must be 0");
  add_atom(atoms_, "uf32", Family::Ufloat, 32, "unsigned f32, sign bit must be 0");
  add_atom(atoms_, "uf64", Family::Ufloat, 64, "unsigned f64, sign bit must be 0");
  add_atom(atoms_, "t1", Family::Ternary, 2, "1 trit, 2 storage bits");
  add_atom(atoms_, "t8", Family::Ternary, 16, "8 trits, 2 bits each");
  add_atom(atoms_, "t16", Family::Ternary, 32, "16 trits, 2 bits each");
  add_atom(atoms_, "t32", Family::Ternary, 64, "32 trits, 2 bits each");
  add_atom(atoms_, "t64", Family::Ternary, 128, "64 trits, 2 bits each");
  add_atom(atoms_, "bool", Family::Bool, 8, "true or false");
  add_atom(atoms_, "char", Family::Char, 8, "one 8-bit code unit");
  add_atom(atoms_, "size", Family::Size, 64, "unsigned pointer width of this host");
  add_atom(atoms_, "string", Family::String, 0, "variable length, not an atom width");
  for (int i = 0; i < static_cast<int>(atoms_.size()); ++i) by_name_[atoms_[i].name] = i;
}

const Atom* TypeMap::find(std::string_view name) const {
  auto it = by_name_.find(std::string(name));
  if (it != by_name_.end()) return &atoms_[it->second];
  auto extra = extra_by_name_.find(std::string(name));
  if (extra != extra_by_name_.end()) return extra->second;
  return nullptr;
}

const Atom* TypeMap::pointer_to(const Atom* pointee) {
  if (!pointee) return nullptr;
  std::string name = pointee->name + "*";
  if (const Atom* found = find(name)) return found;
  extra_.push_back(Atom{});
  Atom& atom = extra_.back();
  atom.name = name;
  atom.family = Family::Pointer;
  atom.bits = 64;
  atom.specifics = "cell, aims at " + pointee->name;
  atom.pointee = pointee;
  atom.ptr_kind = "cell";
  extra_by_name_[atom.name] = &atom;
  return &atom;
}

const Atom* TypeMap::make(std::string name, Family family, int bits, std::string specifics) {
  if (const Atom* found = find(name)) return found;
  extra_.push_back(Atom{});
  Atom& atom = extra_.back();
  atom.name = std::move(name);
  atom.family = family;
  atom.bits = bits;
  atom.specifics = std::move(specifics);
  extra_by_name_[atom.name] = &atom;
  return &atom;
}

const char* TypeMap::family_name(Family f) {
  switch (f) {
    case Family::SignedInt:
      return "signed-int";
    case Family::UnsignedInt:
      return "unsigned-int";
    case Family::Float:
      return "float";
    case Family::Bfloat:
      return "bfloat";
    case Family::Ufloat:
      return "ufloat";
    case Family::Ternary:
      return "ternary";
    case Family::Bool:
      return "bool";
    case Family::Char:
      return "char";
    case Family::Size:
      return "size";
    case Family::String:
      return "string";
    case Family::Pointer:
      return "pointer";
    case Family::Enum:
      return "enum";
    case Family::Object:
      return "object";
  }
  return "unset";
}

const char* TypeMap::mutator_name(Mutator m) {
  switch (m) {
    case Mutator::Const:
      return "const";
    case Mutator::Mutex:
      return "mutex";
    case Mutator::Static:
      return "static";
    case Mutator::Ephemeral:
      return "ephemeral";
    case Mutator::Freeze:
      return "freeze";
    case Mutator::Inlined:
      return "inlined";
    case Mutator::Val:
      return "val";
    case Mutator::Config:
      return "config";
    case Mutator::Xor:
      return "xor";
  }
  return "unset";
}

std::string shape_var(const VarShape& v) {
  std::string out = "var ";
  out += v.atom ? v.atom->name : "?";
  out += " bits=";
  out += v.atom ? std::to_string(v.atom->bits) : "?";
  out += " name=";
  out += v.name.empty() ? "-" : v.name;
  out += " specifics=";
  out += v.atom ? v.atom->specifics : "-";
  out += " mutators=";
  if (v.mutators.empty()) {
    out += "-";
  } else {
    for (size_t i = 0; i < v.mutators.size(); ++i) {
      if (i) out += ",";
      out += TypeMap::mutator_name(v.mutators[i]);
    }
  }
  return out;
}

std::string shape_fn(const FnShape& f) {
  std::string out = "fn " + f.name + " out{";
  if (f.outputs.empty()) {
    out += "-";
  } else {
    for (size_t i = 0; i < f.outputs.size(); ++i) {
      if (i) out += ", ";
      out += shape_var(f.outputs[i]);
    }
  }
  out += "} in{";
  if (f.inputs.empty()) {
    out += "-";
  } else {
    for (size_t i = 0; i < f.inputs.size(); ++i) {
      if (i) out += ", ";
      out += shape_var(f.inputs[i]);
    }
  }
  out += "} mutators=";
  if (f.mutators.empty()) {
    out += "-";
  } else {
    for (size_t i = 0; i < f.mutators.size(); ++i) {
      if (i) out += ",";
      out += TypeMap::mutator_name(f.mutators[i]);
    }
  }
  return out;
}

}  // namespace tp
