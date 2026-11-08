#include "tp/arith.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace tp {
namespace {

uint64_t mask_bits(int bits) {
  if (bits >= 64) return ~0ull;
  return (1ull << bits) - 1ull;
}

int64_t sign_extend(uint64_t bits, const Atom& atom) {
  uint64_t m = mask_bits(atom.bits);
  uint64_t v = bits & m;
  if (atom.family == Family::SignedInt && atom.bits < 64) {
    uint64_t sign = 1ull << (atom.bits - 1);
    if (v & sign) v |= ~m;
  }
  return static_cast<int64_t>(v);
}

bool ordered(Op op, int64_t a, int64_t b) {
  switch (op) {
    case Op::Eq:
      return a == b;
    case Op::Ne:
      return a != b;
    case Op::Lt:
      return a < b;
    case Op::Le:
      return a <= b;
    case Op::Gt:
      return a > b;
    case Op::Ge:
      return a >= b;
    default:
      return false;
  }
}

int64_t signed_min(const Atom& atom) {
  if (atom.bits >= 64) return std::numeric_limits<int64_t>::min();
  return -(int64_t(1) << (atom.bits - 1));
}

int64_t signed_max(const Atom& atom) {
  if (atom.bits >= 64) return std::numeric_limits<int64_t>::max();
  return (int64_t(1) << (atom.bits - 1)) - 1;
}

bool signed_fits(int bits, int64_t v) {
  if (bits >= 64) return true;
  int64_t lim = int64_t(1) << (bits - 1);
  return v >= -lim && v < lim;
}

uint64_t pow_int(const Atom& atom, int64_t base, int64_t exp, const char* stage) {
  if (exp < 0) throw Fail(stage, "negative power", 0, 0);
  uint64_t m = mask_bits(atom.bits);
  if (exp == 0) return 1ull;
  if (base == 0) return 0;
  if (base == 1) return 1ull;
  if (base == -1) return (exp % 2 == 0 ? 1ull : static_cast<uint64_t>(-1)) & m;
  if (exp > atom.bits) throw Fail(stage, "power overflow", 0, 0);
  __int128 acc = 1;
  __int128 b = base;
  int64_t hi = signed_max(atom);
  int64_t lo = signed_min(atom);
  for (int64_t i = 0; i < exp; ++i) {
    __int128 next = acc * b;
    if (next > hi || next < lo) throw Fail(stage, "power overflow", 0, 0);
    acc = next;
  }
  return static_cast<uint64_t>(static_cast<int64_t>(acc)) & m;
}

}  // namespace

uint64_t div_stand_in(const Atom& atom, int mode) {
  if (mode != 2) return 0;
  if (atom.family == Family::UnsignedInt) return mask_bits(atom.bits);
  return static_cast<uint64_t>(signed_max(atom));
}

uint64_t cast_bits(const Atom& src, const Atom& dst, uint64_t bits, const char* stage) {
  auto sx = [](const Atom& atom, uint64_t raw) { return sign_extend(raw, atom); };
  auto enum_sx = [](const Atom& atom, uint64_t raw) {
    if (atom.bits >= 64) return static_cast<int64_t>(raw);
    uint64_t m = (1ull << atom.bits) - 1ull;
    uint64_t v = raw & m;
    if (v & (1ull << (atom.bits - 1))) v |= ~m;
    return static_cast<int64_t>(v);
  };
  if (src.name == dst.name) return bits;
  bool src_int = src.family == Family::SignedInt || src.family == Family::UnsignedInt;
  bool dst_int = dst.family == Family::SignedInt || dst.family == Family::UnsignedInt;
  if (src.family == Family::Enum && dst_int) {
    int64_t v = enum_sx(src, bits);
    if (!signed_fits(dst.bits, v)) throw Fail(stage, "as with no row", 0, 0);
    return static_cast<uint64_t>(v) & mask_bits(dst.bits);
  }
  if (dst.family == Family::Enum && src_int) {
    int64_t v = sx(src, bits);
    if (!signed_fits(dst.bits, v)) throw Fail(stage, "enum value does not fit", 0, 0);
    return static_cast<uint64_t>(v) & mask_bits(dst.bits);
  }
  if (src_int && dst.name == "f64") {
    double d = static_cast<double>(sx(src, bits));
    uint64_t out = 0;
    std::memcpy(&out, &d, 8);
    return out;
  }
  if (src_int && dst_int) {
    int64_t v = sx(src, bits);
    if (!signed_fits(dst.bits, v)) throw Fail(stage, "as with no row", 0, 0);
    return static_cast<uint64_t>(v) & mask_bits(dst.bits);
  }
  throw Fail(stage, "as with no row", 0, 0);
}

uint64_t apply(Op op, const Atom& atom, uint64_t lhs, uint64_t rhs, const char* stage) {
  if (op == Op::Not) {
    if (atom.family != Family::Bool) throw Fail(stage, "not is a bool row", 0, 0);
    return (lhs & 1ull) == 0 ? 1ull : 0ull;
  }
  if (op == Op::Neg) {
    if (atom.family == Family::Float && atom.name == "f64") {
      double da;
      std::memcpy(&da, &lhs, 8);
      da = -da;
      uint64_t bits = 0;
      std::memcpy(&bits, &da, 8);
      return bits;
    }
    if (atom.family != Family::SignedInt) throw Fail(stage, "no row for unary minus", 0, 0);
    uint64_t m = mask_bits(atom.bits);
    int64_t da = sign_extend(lhs, atom);
    if (da == signed_min(atom)) throw Fail(stage, "unary minus of the lowest signed value", 0, 0);
    return static_cast<uint64_t>(-da) & m;
  }
  if (atom.family == Family::Enum) {
    if (op == Op::Eq) return lhs == rhs ? 1ull : 0ull;
    if (op == Op::Ne) return lhs != rhs ? 1ull : 0ull;
    throw Fail(stage, "no row for " + atom.name, 0, 0);
  }
  switch (atom.family) {
    case Family::SignedInt:
    case Family::UnsignedInt: {
      uint64_t m = mask_bits(atom.bits);
      uint64_t a = lhs & m;
      uint64_t b = rhs & m;
      switch (op) {
        case Op::Add:
          return (a + b) & m;
        case Op::Sub:
          return (a - b) & m;
        case Op::Mul:
          return (a * b) & m;
        case Op::And:
          return (a & b) & m;
        case Op::Or:
          return (a | b) & m;
        case Op::Xor:
          return (a ^ b) & m;
        case Op::Shl:
        case Op::Shr: {
          if (b >= static_cast<uint64_t>(atom.bits)) throw Fail(stage, "shift count is outside 0 .. width-1", 0, 0);
          if (op == Op::Shl) return (a << b) & m;
          if (atom.family == Family::UnsignedInt) return (a >> b) & m;
          return static_cast<uint64_t>(sign_extend(a, atom) >> b) & m;
        }
        case Op::Div:
        case Op::Rem:
        case Op::FloorDiv: {
          if (b == 0) throw Fail(stage, "division by zero", 0, 0);
          if (atom.family == Family::UnsignedInt) return (op == Op::Rem ? a % b : a / b) & m;
          int64_t da = sign_extend(a, atom);
          int64_t db = sign_extend(b, atom);
          if (da == signed_min(atom) && db == -1) throw Fail(stage, "lowest signed value divided by -1", 0, 0);
          int64_t q = da / db;
          int64_t r = da % db;
          if (op == Op::FloorDiv && r != 0 && ((da < 0) != (db < 0))) --q;
          return static_cast<uint64_t>(op == Op::Rem ? r : q) & m;
        }
        case Op::BitNot:
          return (~a) & m;
        case Op::Pow:
          return pow_int(atom, sign_extend(a, atom), sign_extend(b, atom), stage);
        case Op::Eq:
        case Op::Ne:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge: {
          bool r = atom.family == Family::UnsignedInt ? ordered(op, static_cast<int64_t>(a), static_cast<int64_t>(b))
                                                     : ordered(op, sign_extend(a, atom), sign_extend(b, atom));
          if (atom.family == Family::UnsignedInt && (op == Op::Lt || op == Op::Le || op == Op::Gt || op == Op::Ge)) {
            switch (op) {
              case Op::Lt:
                r = a < b;
                break;
              case Op::Le:
                r = a <= b;
                break;
              case Op::Gt:
                r = a > b;
                break;
              case Op::Ge:
                r = a >= b;
                break;
              default:
                break;
            }
          }
          return r ? 1ull : 0ull;
        }
        default:
          throw Fail(stage, "not an arithmetic opcode", 0, 0);
      }
    }
    case Family::Float:
      if (atom.name != "f64") throw Fail(stage, "no row for " + atom.name + " yet", 0, 0);
      {
        double da, db;
        std::memcpy(&da, &lhs, 8);
        std::memcpy(&db, &rhs, 8);
        double r = 0;
        switch (op) {
          case Op::Add:
            r = da + db;
            break;
          case Op::Sub:
            r = da - db;
            break;
          case Op::Mul:
            r = da * db;
            break;
          case Op::Div:
            if (db == 0.0) throw Fail(stage, "division by zero", 0, 0);
            r = da / db;
            break;
          case Op::Pow:
            r = std::pow(da, db);
            break;
          case Op::FloorDiv:
            if (db == 0.0) throw Fail(stage, "division by zero", 0, 0);
            r = std::floor(da / db);
            break;
          default:
            throw Fail(stage, "not an arithmetic opcode", 0, 0);
        }
        uint64_t bits = 0;
        std::memcpy(&bits, &r, 8);
        return bits;
      }
    case Family::Pointer:
      if (op == Op::Eq) return lhs == rhs ? 1ull : 0ull;
      if (op == Op::Ne) return lhs != rhs ? 1ull : 0ull;
      throw Fail(stage, "no arithmetic row for a pointer", 0, 0);
    default:
      throw Fail(stage, "no row for " + atom.name, 0, 0);
  }
}

}  // namespace tp
