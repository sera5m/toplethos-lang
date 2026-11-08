#include "tp/arith.hpp"

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

}  // namespace

uint64_t apply(Op op, const Atom& atom, uint64_t lhs, uint64_t rhs, const char* stage) {
  if (op == Op::Not) {
    if (atom.family != Family::Bool) throw Fail(stage, "not is a bool row", 0, 0);
    return (lhs & 1ull) == 0 ? 1ull : 0ull;
  }
  if (op == Op::Neg) {
    if (atom.family != Family::SignedInt) throw Fail(stage, "no row for unary minus", 0, 0);
    uint64_t m = mask_bits(atom.bits);
    int64_t da = sign_extend(lhs, atom);
    if (da == signed_min(atom)) throw Fail(stage, "unary minus of the lowest signed value", 0, 0);
    return static_cast<uint64_t>(-da) & m;
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
        case Op::Rem: {
          if (b == 0) throw Fail(stage, "division by zero", 0, 0);
          if (atom.family == Family::UnsignedInt) return (op == Op::Div ? a / b : a % b) & m;
          int64_t da = sign_extend(a, atom);
          int64_t db = sign_extend(b, atom);
          if (da == signed_min(atom) && db == -1) throw Fail(stage, "lowest signed value divided by -1", 0, 0);
          int64_t q = da / db;
          return static_cast<uint64_t>(op == Op::Div ? q : da - q * db) & m;
        }
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
