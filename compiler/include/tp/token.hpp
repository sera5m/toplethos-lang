#pragma once

#include "tp/diag.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tp {

enum class TokenKind : uint16_t {
  End,
  Ident,
  Number,
  String,
  CharLit,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
  Comma,
  Semicolon,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  StarStar,
  SlashFloor,
  Amp,
  Pipe,
  Caret,
  Tilde,
  Lt,
  Gt,
  LtEq,
  GtEq,
  EqEq,
  NotEq,
  Not,
  Eq,
  AmpAmp,
  PipePipe,
  Shl,
  Shr,
  PlusPlus,
  MinusMinus,
  KwFn,
  KwIn,
  KwOut,
  KwReturn,
  KwIf,
  KwElse,
  KwLoop,
  KwLeave,
  KwTrue,
  KwFalse,
  KwAs,
  KwDo
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  int line = 1;
  int col = 1;
  bool is_float = false;
  uint64_t bits = 0;
};

const char* token_name(TokenKind k);

/* State machine. Emits tokens. Does not decide i32 versus i64. */
std::vector<Token> tokenize(const std::string& src);

}  // namespace tp
