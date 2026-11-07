#include "tp/token.hpp"

#include <cctype>
#include <unordered_map>

namespace tp {
namespace {

enum class St { Start, Ident, Number, String, Char, Slash, LineComment, BlockComment };

const std::unordered_map<std::string, TokenKind>& keywords() {
  static const std::unordered_map<std::string, TokenKind> map = {
      {"fn", TokenKind::KwFn},     {"in", TokenKind::KwIn},     {"out", TokenKind::KwOut},
      {"return", TokenKind::KwReturn}, {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
      {"loop", TokenKind::KwLoop}, {"leave", TokenKind::KwLeave}, {"true", TokenKind::KwTrue},
      {"false", TokenKind::KwFalse}, {"as", TokenKind::KwAs},   {"do", TokenKind::KwDo},
  };
  return map;
}

class Lexer {
 public:
  explicit Lexer(const std::string& src) : s_(src) {}

  std::vector<Token> run() {
    St st = St::Start;
    std::string buf;
    int start_line = 1;
    int start_col = 1;
    bool frac = false;
    while (st != St::Start || i_ <= s_.size()) {
      switch (st) {
        case St::Start: {
          if (i_ >= s_.size()) {
            emit(TokenKind::End, "", line_, col_, false, 0);
            return toks_;
          }
          unsigned char c = static_cast<unsigned char>(s_[i_]);
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            get();
            break;
          }
          start_line = line_;
          start_col = col_;
          if (c == '/') {
            get();
            st = St::Slash;
            break;
          }
          if (std::isalpha(c) || c == '_') {
            buf.clear();
            st = St::Ident;
            break;
          }
          if (std::isdigit(c)) {
            buf.clear();
            frac = false;
            st = St::Number;
            break;
          }
          if (c == '"') {
            get();
            buf.clear();
            st = St::String;
            break;
          }
          if (c == '\'') {
            get();
            buf.clear();
            st = St::Char;
            break;
          }
          punct(start_line, start_col);
          break;
        }
        case St::Slash: {
          if (i_ + 1 < s_.size() && s_[i_] == '_' && s_[i_ + 1] == '/') {
            get();
            get();
            emit(TokenKind::SlashFloor, "/_/", start_line, start_col, false, 0);
            st = St::Start;
            break;
          }
          if (peek() == '/') {
            get();
            st = St::LineComment;
            break;
          }
          if (peek() == '*') {
            get();
            st = St::BlockComment;
            break;
          }
          emit(TokenKind::Slash, "/", start_line, start_col, false, 0);
          st = St::Start;
          break;
        }
        case St::LineComment: {
          if (i_ >= s_.size()) {
            st = St::Start;
            break;
          }
          if (get() == '\n') st = St::Start;
          break;
        }
        case St::BlockComment: {
          if (i_ >= s_.size()) throw Fail("lex", "unclosed comment", line_, col_);
          char c = get();
          if (c == '*' && peek() == '/') {
            get();
            st = St::Start;
          }
          break;
        }
        case St::Ident: {
          unsigned char c = i_ < s_.size() ? static_cast<unsigned char>(s_[i_]) : 0;
          if (std::isalnum(c) || c == '_') {
            buf.push_back(get());
            break;
          }
          auto it = keywords().find(buf);
          TokenKind kind = it == keywords().end() ? TokenKind::Ident : it->second;
          emit(kind, buf, start_line, start_col, false, 0);
          st = St::Start;
          break;
        }
        case St::Number: {
          if (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) {
            buf.push_back(get());
            break;
          }
          if (!frac && peek() == '.' && i_ + 1 < s_.size() &&
              std::isdigit(static_cast<unsigned char>(s_[i_ + 1]))) {
            frac = true;
            buf.push_back(get());
            break;
          }
          Token tok;
          tok.kind = TokenKind::Number;
          tok.text = buf;
          tok.line = start_line;
          tok.col = start_col;
          tok.is_float = frac;
          if (!frac) tok.bits = std::stoull(buf);
          toks_.push_back(std::move(tok));
          st = St::Start;
          break;
        }
        case St::String: {
          if (i_ >= s_.size()) throw Fail("lex", "unclosed string", start_line, start_col);
          char c = get();
          if (c == '"') {
            emit(TokenKind::String, buf, start_line, start_col, false, 0);
            st = St::Start;
            break;
          }
          if (c == '\\') {
            if (i_ >= s_.size()) throw Fail("lex", "unclosed string", start_line, start_col);
            char e = get();
            if (e == 'n') buf.push_back('\n');
            else buf.push_back(e);
            break;
          }
          buf.push_back(c);
          break;
        }
        case St::Char: {
          if (i_ >= s_.size()) throw Fail("lex", "unclosed character", start_line, start_col);
          char c = get();
          if (c == '\\') {
            if (i_ >= s_.size()) throw Fail("lex", "unclosed character", start_line, start_col);
            c = get();
            if (c == 'n') c = '\n';
          }
          if (get() != '\'') throw Fail("lex", "unclosed character", start_line, start_col);
          emit(TokenKind::CharLit, std::string(1, c), start_line, start_col, false, static_cast<unsigned char>(c));
          st = St::Start;
          break;
        }
      }
    }
    return toks_;
  }

 private:
  char peek() const { return i_ < s_.size() ? s_[i_] : 0; }

  char get() {
    if (i_ >= s_.size()) return 0;
    char c = s_[i_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  void emit(TokenKind kind, std::string text, int line, int col, bool is_float, uint64_t bits) {
    Token tok;
    tok.kind = kind;
    tok.text = std::move(text);
    tok.line = line;
    tok.col = col;
    tok.is_float = is_float;
    tok.bits = bits;
    toks_.push_back(std::move(tok));
  }

  void punct(int line, int col) {
    char c = get();
    char n = peek();
    auto two = [&](char want, TokenKind kind, const char* spell) {
      if (n != want) return false;
      get();
      emit(kind, spell, line, col, false, 0);
      return true;
    };
    switch (c) {
      case '(':
        emit(TokenKind::LParen, "(", line, col, false, 0);
        return;
      case ')':
        emit(TokenKind::RParen, ")", line, col, false, 0);
        return;
      case '[':
        emit(TokenKind::LBracket, "[", line, col, false, 0);
        return;
      case ']':
        emit(TokenKind::RBracket, "]", line, col, false, 0);
        return;
      case '{':
        emit(TokenKind::LBrace, "{", line, col, false, 0);
        return;
      case '}':
        emit(TokenKind::RBrace, "}", line, col, false, 0);
        return;
      case ',':
        emit(TokenKind::Comma, ",", line, col, false, 0);
        return;
      case ';':
        emit(TokenKind::Semicolon, ";", line, col, false, 0);
        return;
      case '~':
        emit(TokenKind::Tilde, "~", line, col, false, 0);
        return;
      case '+':
        if (two('+', TokenKind::PlusPlus, "++")) return;
        emit(TokenKind::Plus, "+", line, col, false, 0);
        return;
      case '-':
        if (two('-', TokenKind::MinusMinus, "--")) return;
        emit(TokenKind::Minus, "-", line, col, false, 0);
        return;
      case '*':
        if (two('*', TokenKind::StarStar, "**")) return;
        emit(TokenKind::Star, "*", line, col, false, 0);
        return;
      case '%':
        emit(TokenKind::Percent, "%", line, col, false, 0);
        return;
      case '&':
        if (two('&', TokenKind::AmpAmp, "&&")) return;
        emit(TokenKind::Amp, "&", line, col, false, 0);
        return;
      case '|':
        if (two('|', TokenKind::PipePipe, "||")) return;
        emit(TokenKind::Pipe, "|", line, col, false, 0);
        return;
      case '^':
        emit(TokenKind::Caret, "^", line, col, false, 0);
        return;
      case '<':
        if (two('<', TokenKind::Shl, "<<")) return;
        if (two('=', TokenKind::LtEq, "<=")) return;
        emit(TokenKind::Lt, "<", line, col, false, 0);
        return;
      case '>':
        if (two('>', TokenKind::Shr, ">>")) return;
        if (two('=', TokenKind::GtEq, ">=")) return;
        emit(TokenKind::Gt, ">", line, col, false, 0);
        return;
      case '=':
        if (two('=', TokenKind::EqEq, "==")) return;
        emit(TokenKind::Eq, "=", line, col, false, 0);
        return;
      case '!':
        if (two('=', TokenKind::NotEq, "!=")) return;
        emit(TokenKind::Not, "!", line, col, false, 0);
        return;
      default:
        throw Fail("lex", std::string("character not in the token set: ") + c, line, col);
    }
  }

  const std::string& s_;
  size_t i_ = 0;
  int line_ = 1;
  int col_ = 1;
  std::vector<Token> toks_;
};

}  // namespace

const char* token_name(TokenKind k) {
  switch (k) {
    case TokenKind::End:
      return "end";
    case TokenKind::Ident:
      return "ident";
    case TokenKind::Number:
      return "number";
    case TokenKind::String:
      return "string";
    case TokenKind::CharLit:
      return "char";
    case TokenKind::LParen:
      return "(";
    case TokenKind::RParen:
      return ")";
    case TokenKind::LBracket:
      return "[";
    case TokenKind::RBracket:
      return "]";
    case TokenKind::LBrace:
      return "{";
    case TokenKind::RBrace:
      return "}";
    case TokenKind::Comma:
      return ",";
    case TokenKind::Semicolon:
      return ";";
    case TokenKind::Plus:
      return "+";
    case TokenKind::Minus:
      return "-";
    case TokenKind::Star:
      return "*";
    case TokenKind::Slash:
      return "/";
    case TokenKind::Percent:
      return "%";
    case TokenKind::StarStar:
      return "**";
    case TokenKind::SlashFloor:
      return "/_/";
    case TokenKind::Amp:
      return "&";
    case TokenKind::Pipe:
      return "|";
    case TokenKind::Caret:
      return "^";
    case TokenKind::Tilde:
      return "~";
    case TokenKind::Lt:
      return "<";
    case TokenKind::Gt:
      return ">";
    case TokenKind::LtEq:
      return "<=";
    case TokenKind::GtEq:
      return ">=";
    case TokenKind::EqEq:
      return "==";
    case TokenKind::NotEq:
      return "!=";
    case TokenKind::Not:
      return "!";
    case TokenKind::Eq:
      return "=";
    case TokenKind::AmpAmp:
      return "&&";
    case TokenKind::PipePipe:
      return "||";
    case TokenKind::Shl:
      return "<<";
    case TokenKind::Shr:
      return ">>";
    case TokenKind::PlusPlus:
      return "++";
    case TokenKind::MinusMinus:
      return "--";
    case TokenKind::KwFn:
      return "fn";
    case TokenKind::KwIn:
      return "in";
    case TokenKind::KwOut:
      return "out";
    case TokenKind::KwReturn:
      return "return";
    case TokenKind::KwIf:
      return "if";
    case TokenKind::KwElse:
      return "else";
    case TokenKind::KwLoop:
      return "loop";
    case TokenKind::KwLeave:
      return "leave";
    case TokenKind::KwTrue:
      return "true";
    case TokenKind::KwFalse:
      return "false";
    case TokenKind::KwAs:
      return "as";
    case TokenKind::KwDo:
      return "do";
  }
  return "unset";
}

std::vector<Token> tokenize(const std::string& src) { return Lexer(src).run(); }

}  // namespace tp
