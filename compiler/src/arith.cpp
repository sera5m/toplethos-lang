#include "tp/arith.hpp"

#include <cstring>

namespace tp {
namespace {

uint64_t mask_bits(int bits) {
  if (bits >= 64) return ~0ull;
  return (1ull << bits) - 1ull;
}

}  // namespace

uint64_t apply(Op op, const Atom& atom, uint64_t lhs, uint64_t rhs, const char* stage) {
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
        case Op::Div: {
          if (b == 0) throw Fail(stage, "division by zero", 0, 0);
          if (atom.family == Family::UnsignedInt) return (a / b) & m;
          int64_t da = static_cast<int64_t>(a);
          int64_t db = static_cast<int64_t>(b);
          if (atom.bits < 64) {
            int64_t sign = 1ll << (atom.bits - 1);
            if (da & sign) da |= ~static_cast<int64_t>(m);
            if (db & sign) db |= ~static_cast<int64_t>(m);
          }
          return static_cast<uint64_t>(da / db) & m;
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
    default:
      throw Fail(stage, "no row for " + atom.name, 0, 0);
  }
}

}  // namespace tp
