#include "tp/arch.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace tp {
namespace {

bool is_f64(const Atom* atom) { return atom && atom->family == Family::Float; }

std::string hex64(uint64_t v) {
  static const char* d = "0123456789abcdef";
  std::string s = "0x";
  bool nz = false;
  for (int i = 15; i >= 0; --i) {
    int nib = static_cast<int>((v >> (i * 4)) & 0xf);
    if (nib || nz || i == 0) {
      s.push_back(d[nib]);
      nz = true;
    }
  }
  return s;
}

std::string gas_string(const std::string& text) {
  std::string out;
  for (unsigned char c : text) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    } else if (c >= 32 && c < 127) {
      out.push_back(static_cast<char>(c));
    } else {
      out += "\\";
      out.push_back("01234567"[(c >> 6) & 7]);
      out.push_back("01234567"[(c >> 3) & 7]);
      out.push_back("01234567"[c & 7]);
    }
  }
  return out;
}

}  // namespace

std::string emit_amd64(const Program& prog) {
  std::string out = "# target amd64\n# ";
  out += stage_info("amd64").does;
  out += "\n    .text\n";
  bool needs_fail = false;
  bool saw_host = false;
  bool saw_bmp = false;
  bool saw_fork = false;
  bool saw_load = false;
  std::string bss;
  int fn_i = 0;
  auto clean = [](std::string s) {
    for (char& c : s) {
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
      if (!ok) c = '_';
    }
    return s;
  };
  int chk = 0;
  int run_n = 0;
  std::vector<std::string> strs;
  static const char* r32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
  static const char* r64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  static const char* xmm[] = {"%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
  double nano = 1e-9;
  uint64_t nano_bits = 0;
  std::memcpy(&nano_bits, &nano, 8);
  std::string nano_imm = hex64(nano_bits);
  for (const BcFunc& fn : prog.fns) {
    for (const Insn& in : fn.code) arch_require(Target::Amd64, in);
    std::string tag = clean(fn.shape.name) + "_" + std::to_string(fn_i++);
    std::vector<std::string> memop(fn.nslots);
    std::vector<std::string> guard(fn.nslots);
    int frame_n = 0;
    for (uint32_t s = 0; s < fn.nslots;) {
      bool st = s < fn.flag.size() && (fn.flag[s] & kFlagStatic);
      if (!st) {
        memop[s] = "-" + std::to_string(8 * (frame_n + 1)) + "(%rbp)";
        ++frame_n;
        ++s;
        continue;
      }
      uint32_t e = s + 1;
      const std::string name = s < fn.slot_name.size() ? fn.slot_name[s] : std::string();
      while (e < fn.nslots && e < fn.flag.size() && (fn.flag[e] & kFlagStatic) && e < fn.slot_name.size() &&
             fn.slot_name[e] == name) {
        ++e;
      }
      uint32_t count = e - s;
      std::string slab = ".Ltp_st_" + tag + "_" + std::to_string(s);
      std::string glab = ".Ltp_og_" + tag + "_" + std::to_string(s);
      guard[s] = glab;
      bss += "    .align 8\n" + slab + ":\n    .zero " + std::to_string(static_cast<unsigned long long>(count) * 8ull) + "\n";
      bss += glab + ":\n    .zero 1\n";
      for (uint32_t k = s; k < e; ++k) {
        int off = 8 * (static_cast<int>(count) - 1 - static_cast<int>(k - s));
        memop[k] = off == 0 ? slab + "(%rip)" : slab + "+" + std::to_string(off) + "(%rip)";
      }
      s = e;
    }
    int frame = ((frame_n * 8) + 15) & ~15;
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    push %rbp\n    mov %rsp, %rbp\n";
    out += "    sub $" + std::to_string(frame) + ", %rsp\n";
    auto slot = [&](uint32_t id) { return memop.at(id); };
    auto lab = [&](uint64_t id) { return ".L" + fn.shape.name + "_" + std::to_string(id); };
    if (fn.shape.inputs.size() > 14) throw Fail("amd64", "more than 6 arguments", 0, 0);
    int ni = 0;
    int nx = 0;
    for (size_t i = 0; i < fn.shape.inputs.size(); ++i) {
      const Atom* atom = fn.shape.inputs[i].atom;
      bool ptr = atom && atom->family == Family::Pointer;
      if (is_f64(atom)) {
        if (nx >= 8) throw Fail("amd64", "more than 6 arguments", 0, 0);
        out += "    movsd " + std::string(xmm[nx]) + ", " + slot(static_cast<uint32_t>(i)) + "\n";
        ++nx;
        continue;
      }
      if (!atom || (!ptr && (atom->family != Family::SignedInt || (atom->bits != 32 && atom->bits != 64)))) {
        throw Fail("amd64", "no abi row for this argument", 0, 0);
      }
      if (ni >= 6) throw Fail("amd64", "more than 6 arguments", 0, 0);
      const char* from = (ptr || atom->bits == 64) ? r64[ni] : r32[ni];
      out += "    mov " + std::string(from) + ", " + slot(static_cast<uint32_t>(i)) + "\n";
      ++ni;
    }
    auto fbin = [&](const Insn& in, const char* how) {
      out += "    movsd " + slot(in.lhs) + ", %xmm0\n";
      out += "    " + std::string(how) + " " + slot(in.rhs) + ", %xmm0\n";
      out += "    movsd %xmm0, " + slot(in.dst) + "\n";
    };
    for (const Insn& in : fn.code) {
      if (in.op == Op::Label) {
        out += lab(in.imm) + ":\n";
        continue;
      }
      if (in.op == Op::Jmp) {
        out += "    jmp " + lab(in.imm) + "\n";
        continue;
      }
      if (in.op == Op::Br) {
        out += "    cmpl $0, " + slot(in.lhs) + "\n";
        out += "    je " + lab(in.imm) + "\n";
        continue;
      }
      bool wide = in.atom && in.atom->bits == 64 && !is_f64(in.atom);
      const char* reg = wide ? "%rax" : "%eax";
      auto binary = [&](const char* how) {
        out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
        out += "    " + std::string(how) + " " + slot(in.rhs) + ", " + reg + "\n";
        out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
      };
      switch (in.op) {
        case Op::Imm:
          if (in.atom && in.atom->bits == 64) out += "    movabs $" + std::to_string(static_cast<int64_t>(in.imm)) + ", %rax\n";
          else out += "    mov $" + std::to_string(static_cast<int32_t>(in.imm)) + ", %eax\n";
          out += "    mov " + std::string(in.atom && in.atom->bits == 64 ? "%rax" : "%eax") + ", " + slot(in.dst) + "\n";
          break;
        case Op::Mov:
          if (is_f64(in.atom) || (in.atom && in.atom->bits == 64)) {
            out += "    mov " + slot(in.lhs) + ", %rax\n";
            out += "    mov %rax, " + slot(in.dst) + "\n";
          } else {
            out += "    mov " + slot(in.lhs) + ", %eax\n";
            out += "    mov %eax, " + slot(in.dst) + "\n";
          }
          break;
        case Op::Add:
          if (is_f64(in.atom)) fbin(in, "addsd");
          else binary("add");
          break;
        case Op::Sub:
          if (is_f64(in.atom)) fbin(in, "subsd");
          else binary("sub");
          break;
        case Op::Mul:
          if (is_f64(in.atom)) fbin(in, "mulsd");
          else binary("imul");
          break;
        case Op::And:
          binary("and");
          break;
        case Op::Or:
          binary("or");
          break;
        case Op::Xor:
          binary("xor");
          break;
        case Op::Div:
        case Op::Rem: {
          if (is_f64(in.atom)) {
            if (in.op != Op::Div) throw Fail("amd64", "no row for this opcode yet", in.line, 0);
            fbin(in, "divsd");
            break;
          }
          bool srcwide = in.atom->bits == 64;
          needs_fail = true;
          std::string skip = ".Lchk" + std::to_string(chk++);
          out += "    cmp" + std::string(srcwide ? "q" : "l") + " $0, " + slot(in.rhs) + "\n";
          out += "    je .Ltp_fail\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += "    cmp" + std::string(srcwide ? "q" : "l") + " $-1, " + slot(in.rhs) + "\n";
          out += "    jne " + skip + "\n";
          if (srcwide) out += "    movabs $-9223372036854775808, %r11\n    cmp %r11, %rax\n";
          else out += "    cmpl $-2147483648, %eax\n";
          out += "    je .Ltp_fail\n";
          out += skip + ":\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += srcwide ? "    cqo\n    idivq " : "    cdq\n    idivl ";
          out += slot(in.rhs) + "\n";
          out += "    mov " + std::string(in.op == Op::Div ? reg : (srcwide ? "%rdx" : "%edx")) + ", " + slot(in.dst) + "\n";
          break;
        }
        case Op::Shl:
        case Op::Shr:
          needs_fail = true;
          out += "    mov " + slot(in.rhs) + ", %ecx\n";
          out += "    cmpl $" + std::to_string(in.atom->bits) + ", %ecx\n";
          out += "    jae .Ltp_fail\n";
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += in.op == Op::Shl ? "    shl %cl, " : "    sar %cl, ";
          out += std::string(reg) + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::Neg:
          if (is_f64(in.atom)) {
            out += "    xorpd %xmm0, %xmm0\n";
            out += "    movsd " + slot(in.lhs) + ", %xmm1\n";
            out += "    subsd %xmm1, %xmm0\n";
            out += "    movsd %xmm0, " + slot(in.dst) + "\n";
            break;
          }
          needs_fail = true;
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          if (wide) out += "    movabs $-9223372036854775808, %r11\n    cmp %r11, %rax\n";
          else out += "    cmpl $-2147483648, %eax\n";
          out += "    je .Ltp_fail\n";
          out += "    neg " + std::string(reg) + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::BitNot:
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          out += "    not " + std::string(reg) + "\n";
          out += "    mov " + std::string(reg) + ", " + slot(in.dst) + "\n";
          break;
        case Op::BrRange: {
          uint64_t mask = wide ? ~0ull : 0xffffffffull;
          uint64_t lo = in.imm_lo & mask;
          uint64_t span = (in.imm_hi - in.imm_lo) & mask;
          out += "    mov " + slot(in.lhs) + ", " + reg + "\n";
          if (wide) {
            out += "    movabs $" + std::to_string(static_cast<int64_t>(lo)) + ", %r11\n";
            out += "    sub %r11, %rax\n";
            out += "    movabs $" + std::to_string(static_cast<int64_t>(span)) + ", %r11\n";
            out += "    cmp %r11, %rax\n";
          } else {
            out += "    subl $" + std::to_string(static_cast<int32_t>(lo)) + ", %eax\n";
            out += "    cmpl $" + std::to_string(static_cast<int32_t>(span)) + ", %eax\n";
          }
          out += "    ja " + lab(in.imm) + "\n";
          break;
        }
        case Op::Call: {
          const BcFunc& callee = prog.fns[in.imm];
          if (in.args.size() > 14) throw Fail("amd64", "more than 6 arguments", in.line, 0);
          if (in.args.size() != callee.shape.inputs.size()) throw Fail("amd64", "argument count", in.line, 0);
          int ai = 0;
          int ax = 0;
          for (size_t i = 0; i < in.args.size(); ++i) {
            const Atom* atom = callee.shape.inputs[i].atom;
            if (is_f64(atom)) {
              if (ax >= 8) throw Fail("amd64", "more than 6 arguments", in.line, 0);
              out += "    movsd " + slot(in.args[i]) + ", " + xmm[ax] + "\n";
              ++ax;
              continue;
            }
            if (ai >= 6) throw Fail("amd64", "more than 6 arguments", in.line, 0);
            bool argwide = atom->bits == 64 || atom->family == Family::Pointer;
            const char* to = argwide ? r64[ai] : r32[ai];
            out += "    mov " + slot(in.args[i]) + ", " + to + "\n";
            ++ai;
          }
          out += "    call " + callee.shape.name + "\n";
          if (in.atom && in.dst < fn.nslots) {
            if (is_f64(in.atom)) out += "    movsd %xmm0, " + slot(in.dst) + "\n";
            else {
              bool retwide = in.atom->bits == 64 || in.atom->family == Family::Pointer;
              out += "    mov " + std::string(retwide ? "%rax" : "%eax") + ", " + slot(in.dst) + "\n";
            }
          }
          break;
        }
        case Op::Threads:
          needs_fail = true;
          out += "    mov $84, %edi\n    call sysconf@PLT\n    cmp $1, %rax\n    jl .Ltp_fail\n";
          out += "    mov %eax, " + slot(in.dst) + "\n";
          break;
        case Op::Freemem:
          needs_fail = true;
          out += "    sub $16, %rsp\n    mov $30, %edi\n    call sysconf@PLT\n    mov %rax, (%rsp)\n";
          out += "    mov $86, %edi\n    call sysconf@PLT\n    mov (%rsp), %rcx\n    add $16, %rsp\n";
          out += "    cmp $0, %rax\n    jl .Ltp_fail\n    cmp $0, %rcx\n    jl .Ltp_fail\n";
          out += "    imul %rcx, %rax\n    mov %rax, " + slot(in.dst) + "\n";
          break;
        case Op::Addr:
          out += "    lea " + slot(in.lhs) + ", %rax\n    mov %rax, " + slot(in.dst) + "\n";
          break;
        case Op::Load: {
          bool widev = in.atom && in.atom->bits == 64;
          out += "    mov " + slot(in.lhs) + ", %rax\n";
          out += widev ? "    mov (%rax), %rax\n" : "    mov (%rax), %eax\n";
          out += "    mov " + std::string(widev ? "%rax" : "%eax") + ", " + slot(in.dst) + "\n";
          break;
        }
        case Op::Store: {
          bool widev = in.atom && in.atom->bits == 64;
          out += "    mov " + slot(in.lhs) + ", %rax\n";
          out += "    mov " + slot(in.rhs) + ", " + (widev ? "%rcx" : "%ecx") + "\n";
          out += widev ? "    mov %rcx, (%rax)\n" : "    mov %ecx, (%rax)\n";
          break;
        }
        case Op::Eq:
        case Op::Ne:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge:
        case Op::Not: {
          if (in.op != Op::Not && in.src && in.src->family == Family::Float) {
            out += "    movsd " + slot(in.lhs) + ", %xmm0\n";
            out += "    ucomisd " + slot(in.rhs) + ", %xmm0\n";
            const char* set = "sete";
            if (in.op == Op::Ne) set = "setne";
            if (in.op == Op::Lt) set = "setb";
            if (in.op == Op::Le) set = "setbe";
            if (in.op == Op::Gt) set = "seta";
            if (in.op == Op::Ge) set = "setae";
            out += "    " + std::string(set) + " %al\n    movzbl %al, %eax\n";
            out += "    mov %eax, " + slot(in.dst) + "\n";
            break;
          }
          bool srcwide = in.src && in.src->bits == 64;
          const char* sreg = srcwide ? "%rax" : "%eax";
          if (in.op == Op::Not) {
            out += "    cmpl $0, " + slot(in.lhs) + "\n";
          } else {
            out += "    mov " + slot(in.lhs) + ", " + sreg + "\n";
            out += "    cmp " + slot(in.rhs) + ", " + sreg + "\n";
          }
          const char* set = "sete";
          if (in.op == Op::Ne) set = "setne";
          if (in.op == Op::Lt) set = "setl";
          if (in.op == Op::Le) set = "setle";
          if (in.op == Op::Gt) set = "setg";
          if (in.op == Op::Ge) set = "setge";
          out += "    " + std::string(set) + " %al\n    movzbl %al, %eax\n";
          out += "    mov %eax, " + slot(in.dst) + "\n";
          break;
        }
        case Op::Ret:
          if (is_f64(in.atom)) out += "    movsd " + slot(in.dst) + ", %xmm0\n";
          else out += "    mov " + slot(in.dst) + ", " + std::string(in.atom && in.atom->bits == 64 ? "%rax" : "%eax") + "\n";
          out += "    leave\n    ret\n";
          break;
        case Op::Cast: {
          bool to_f = is_f64(in.atom);
          bool from_int = in.src && (in.src->family == Family::SignedInt || in.src->family == Family::UnsignedInt);
          if (to_f && from_int) {
            if (in.src->bits >= 64) {
              out += "    mov " + slot(in.lhs) + ", %rax\n";
              out += "    cvtsi2sdq %rax, %xmm0\n";
            } else if (in.src->family == Family::UnsignedInt) {
              out += "    mov " + slot(in.lhs) + ", %eax\n";
              out += "    cvtsi2sdq %rax, %xmm0\n";
            } else {
              out += "    mov " + slot(in.lhs) + ", %eax\n";
              out += "    cvtsi2sdl %eax, %xmm0\n";
            }
            out += "    movsd %xmm0, " + slot(in.dst) + "\n";
            break;
          }
          if (from_int && in.atom && in.atom->family == Family::SignedInt && in.src->bits < 64 && in.atom->bits == 64) {
            out += "    movslq " + slot(in.lhs) + ", %rax\n";
            out += "    mov %rax, " + slot(in.dst) + "\n";
            break;
          }
          throw Fail("amd64", "no row for this opcode yet", in.line, 0);
        }
        case Op::SlotAdd:
          out += "    mov " + slot(in.lhs) + ", %rax\n";
          out += "    mov " + slot(in.rhs) + ", %rcx\n";
          out += "    shl $3, %rcx\n";
          out += "    sub %rcx, %rax\n";
          out += "    mov %rax, " + slot(in.dst) + "\n";
          break;
        case Op::Clock:
          out += "    sub $32, %rsp\n";
          out += "    mov $1, %edi\n";
          out += "    lea (%rsp), %rsi\n";
          out += "    call clock_gettime@PLT\n";
          out += "    cvtsi2sdq (%rsp), %xmm0\n";
          out += "    cvtsi2sdq 8(%rsp), %xmm1\n";
          out += "    movabs $" + nano_imm + ", %rax\n";
          out += "    movq %rax, %xmm2\n";
          out += "    mulsd %xmm2, %xmm1\n";
          out += "    addsd %xmm1, %xmm0\n";
          out += "    movsd %xmm0, " + slot(in.dst) + "\n";
          out += "    add $32, %rsp\n";
          break;
        case Op::Watch: {
          saw_host = true;
          int id = static_cast<int>(strs.size());
          strs.push_back(in.note);
          out += "    lea .Ltp_fmt_w(%rip), %rdi\n";
          out += "    lea .Ltp_s" + std::to_string(id) + "(%rip), %rsi\n";
          out += "    movsd " + slot(in.lhs) + ", %xmm0\n";
          out += "    movsd " + slot(in.rhs) + ", %xmm1\n";
          out += "    mov $2, %al\n";
          out += "    call printf@PLT\n";
          break;
        }
        case Op::SaveBmp: {
          saw_host = true;
          saw_bmp = true;
          int id = static_cast<int>(strs.size());
          strs.push_back(in.note);
          out += "    mov " + slot(in.lhs) + ", %rdi\n";
          out += "    movslq " + slot(in.args[0]) + ", %rsi\n";
          out += "    movslq " + slot(in.args[1]) + ", %rdx\n";
          out += "    lea .Ltp_s" + std::to_string(id) + "(%rip), %rcx\n";
          out += "    mov $" + std::to_string(static_cast<unsigned>(in.imm)) + ", %r8\n";
          out += "    call .Ltp_save_bmp\n";
          if (in.imm_hi) {
            needs_fail = true;
            out += "    test %eax, %eax\n    jz .Ltp_fail\n";
          } else {
            out += "    mov %eax, " + slot(in.dst) + "\n";
          }
          break;
        }
        case Op::LoadBytes: {
          saw_host = true;
          saw_load = true;
          int id = static_cast<int>(strs.size());
          strs.push_back(in.note);
          out += "    mov " + slot(in.lhs) + ", %rdi\n";
          out += "    mov $" + std::to_string(in.imm) + ", %rsi\n";
          out += "    lea .Ltp_s" + std::to_string(id) + "(%rip), %rdx\n";
          out += "    mov $" + std::to_string(in.imm_lo) + ", %rcx\n";
          out += "    call .Ltp_load_bytes\n";
          out += "    mov %eax, " + slot(in.dst) + "\n";
          break;
        }
        case Op::LoadBmp: {
          saw_host = true;
          saw_load = true;
          int id = static_cast<int>(strs.size());
          strs.push_back(in.note);
          out += "    mov " + slot(in.lhs) + ", %rdi\n";
          out += "    mov $" + std::to_string(in.imm) + ", %rsi\n";
          out += "    lea .Ltp_s" + std::to_string(id) + "(%rip), %rdx\n";
          out += "    call .Ltp_load_bmp\n";
          break;
        }
        case Op::Print: {
          saw_host = true;
          if (in.atom && (in.atom->family == Family::Bool || in.atom->name == "bool")) {
            std::string yes = ".Lpb" + std::to_string(chk++);
            std::string no = ".Lpb" + std::to_string(chk++);
            out += "    cmpl $0, " + slot(in.lhs) + "\n";
            out += "    je " + no + "\n";
            out += "    lea .Ltp_true(%rip), %rdi\n";
            out += "    jmp " + yes + "\n";
            out += no + ":\n    lea .Ltp_false(%rip), %rdi\n";
            out += yes + ":\n    xor %eax, %eax\n    call printf@PLT\n";
            break;
          }
          if (is_f64(in.atom)) {
            out += "    lea .Ltp_fmt_f(%rip), %rdi\n";
            out += "    movsd " + slot(in.lhs) + ", %xmm0\n";
            out += "    mov $1, %al\n";
            out += "    call printf@PLT\n";
            break;
          }
          if (in.atom && in.atom->bits == 64) {
            out += "    lea .Ltp_fmt_ll(%rip), %rdi\n";
            out += "    mov " + slot(in.lhs) + ", %rsi\n";
            out += "    xor %eax, %eax\n    call printf@PLT\n";
            break;
          }
          out += "    lea .Ltp_fmt_d(%rip), %rdi\n";
          out += "    mov " + slot(in.lhs) + ", %esi\n";
          out += "    xor %eax, %eax\n    call printf@PLT\n";
          break;
        }
        case Op::Fork: {
          saw_fork = true;
          const BcFunc& callee = prog.fns[in.imm];
          if (in.args.size() + 1 != callee.shape.inputs.size()) throw Fail("amd64", "argument count", in.line, 0);
          out += "    movslq " + slot(in.lhs) + ", %rax\n";
          out += "    mov %rax, .Ltp_fork_n+" + std::to_string(8 * in.imm) + "(%rip)\n";
          for (size_t i = 0; i < in.args.size(); ++i) {
            const Atom* atom = callee.shape.inputs[i + 1].atom;
            if (atom && atom->bits < 64 && atom->family != Family::Pointer && !is_f64(atom)) {
              out += "    movslq " + slot(in.args[i]) + ", %rax\n";
            } else {
              out += "    mov " + slot(in.args[i]) + ", %rax\n";
            }
            size_t off = 8 * 8 * static_cast<size_t>(in.imm) + 8 * i;
            out += "    mov %rax, .Ltp_fork_x+" + std::to_string(off) + "(%rip)\n";
          }
          break;
        }
        case Op::Start: {
          saw_fork = true;
          const BcFunc& callee = prog.fns[in.imm];
          std::string head = ".Lrun" + std::to_string(run_n);
          std::string done = ".Lrund" + std::to_string(run_n);
          ++run_n;
          out += "    xorpd %xmm0, %xmm0\n    movsd %xmm0, " + slot(in.dst) + "\n";
          out += "    push %r12\n    push %r13\n";
          out += "    mov .Ltp_fork_n+" + std::to_string(8 * in.imm) + "(%rip), %r12\n";
          out += "    xor %r13, %r13\n";
          out += head + ":\n    cmp %r12, %r13\n    jge " + done + "\n";
          int si = 0;
          int sx = 0;
          for (size_t a = 0; a < callee.shape.inputs.size(); ++a) {
            const Atom* atom = callee.shape.inputs[a].atom;
            if (is_f64(atom)) {
              if (a == 0) out += "    cvtsi2sdq %r13, " + std::string(xmm[sx]) + "\n";
              else {
                size_t off = 8 * 8 * static_cast<size_t>(in.imm) + 8 * (a - 1);
                out += "    movsd .Ltp_fork_x+" + std::to_string(off) + "(%rip), " + xmm[sx] + "\n";
              }
              ++sx;
              continue;
            }
            bool argwide = atom && (atom->bits == 64 || atom->family == Family::Pointer);
            if (a == 0) {
              out += "    mov %r13, %rax\n";
              out += "    mov " + std::string(argwide ? "%rax" : "%eax") + ", " + (argwide ? r64[si] : r32[si]) + "\n";
            } else {
              size_t off = 8 * 8 * static_cast<size_t>(in.imm) + 8 * (a - 1);
              out += "    mov .Ltp_fork_x+" + std::to_string(off) + "(%rip), %rax\n";
              out += "    mov " + std::string(argwide ? "%rax" : "%eax") + ", " + (argwide ? r64[si] : r32[si]) + "\n";
            }
            ++si;
          }
          out += "    call " + callee.shape.name + "\n";
          if (!callee.shape.outputs.empty() && is_f64(callee.shape.outputs[0].atom)) {
            out += "    addsd " + slot(in.dst) + ", %xmm0\n";
            out += "    movsd %xmm0, " + slot(in.dst) + "\n";
          }
          out += "    inc %r13\n    jmp " + head + "\n";
          out += done + ":\n    pop %r13\n    pop %r12\n";
          break;
        }
        case Op::Lock:
        case Op::Unlock:
          break;
        case Op::StaticInit:
          if (in.dst >= guard.size() || guard[in.dst].empty()) throw Fail("amd64", "static has no cell", in.line, 0);
          if (in.imm == 0) {
            out += "    cmpb $0, " + guard[in.dst] + "(%rip)\n";
            out += "    jne " + lab(in.lhs) + "\n";
          } else {
            out += "    movb $1, " + guard[in.dst] + "(%rip)\n";
          }
          break;
        case Op::Jmp:
        case Op::Br:
        case Op::Label:
          break;
        default:
          throw Fail("amd64", "no row for this opcode yet", in.line, 0);
      }
    }
    if (fn.shape.outputs.empty()) out += "    leave\n    ret\n";
  }
  if (saw_bmp) {
    /* rdi ptr, rsi width, rdx height, rcx path, r8 bits. Element i is at
       ptr - i*8: the frame grows down. No cell-count check. */
    out += R"asm(.Ltp_save_bmp:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $88, %rsp
    mov %rdi, %r12
    mov %rsi, %r13
    mov %rdx, %r14
    mov %rcx, %rbx
    mov %r8, %r15
    movq $0, 72(%rsp)
    movq $0, 80(%rsp)
    test %rbx, %rbx
    jz .Ltp_bmp_fail
    cmpb $0, (%rbx)
    je .Ltp_bmp_fail
    cmp $1, %r13
    jl .Ltp_bmp_fail
    cmp $1048576, %r13
    jg .Ltp_bmp_fail
    cmp $1, %r14
    jl .Ltp_bmp_fail
    cmp $1048576, %r14
    jg .Ltp_bmp_fail
    cmp $1, %r15
    jl .Ltp_bmp_fail
    cmp $64, %r15
    jg .Ltp_bmp_fail
    mov %r13, %rax
    lea (%rax,%rax,2), %rax
    add $3, %rax
    and $-4, %rax
    mov %rax, 64(%rsp)
    mul %r14
    test %rdx, %rdx
    jnz .Ltp_bmp_fail
    movabs $4294967242, %rcx
    cmp %rcx, %rax
    ja .Ltp_bmp_fail
    mov %rax, %r11
    xor %eax, %eax
    mov $54, %ecx
    lea (%rsp), %rdi
.Ltp_bmp_hz:
    movb $0, (%rdi)
    inc %rdi
    dec %ecx
    jnz .Ltp_bmp_hz
    movb $66, (%rsp)
    movb $77, 1(%rsp)
    mov %r11d, %eax
    add $54, %eax
    mov %eax, 2(%rsp)
    movl $54, 10(%rsp)
    movl $40, 14(%rsp)
    mov %r13d, 18(%rsp)
    mov %r14d, 22(%rsp)
    movw $1, 26(%rsp)
    movw $24, 28(%rsp)
    mov %r11d, 34(%rsp)
    movl $2835, 38(%rsp)
    movl $2835, 42(%rsp)
    mov %rbx, %rdi
    lea .Ltp_wb(%rip), %rsi
    call fopen@PLT
    test %rax, %rax
    jz .Ltp_bmp_fail
    mov %rax, 72(%rsp)
    lea (%rsp), %rdi
    mov $1, %rsi
    mov $54, %rdx
    mov 72(%rsp), %rcx
    call fwrite@PLT
    cmp $54, %rax
    jne .Ltp_bmp_fail
    mov 64(%rsp), %rdi
    call malloc@PLT
    test %rax, %rax
    jz .Ltp_bmp_fail
    mov %rax, 80(%rsp)
    mov %r14, %rax
    dec %rax
    mov %rax, 56(%rsp)
.Ltp_bmp_row:
    mov 80(%rsp), %rdi
    mov 64(%rsp), %rcx
.Ltp_bmp_z:
    test %rcx, %rcx
    jz .Ltp_bmp_zd
    movb $0, (%rdi)
    inc %rdi
    dec %rcx
    jmp .Ltp_bmp_z
.Ltp_bmp_zd:
    xor %r11, %r11
.Ltp_bmp_pix:
    cmp %r13, %r11
    jge .Ltp_bmp_pixd
    mov 56(%rsp), %rax
    imul %r13, %rax
    add %r11, %rax
    shl $3, %rax
    mov %r12, %rsi
    sub %rax, %rsi
    mov (%rsi), %rax
    cmp $64, %r15
    je .Ltp_bmp_nomask
    mov %r15d, %ecx
    mov $1, %rdx
    shl %cl, %rdx
    dec %rdx
    and %rdx, %rax
.Ltp_bmp_nomask:
    cmp $24, %r15
    jl .Ltp_bmp_narrow
    mov %rax, %r8
    mov %rax, %r9
    shr $16, %r8
    shr $8, %r9
    mov %al, %dl
    jmp .Ltp_bmp_store
.Ltp_bmp_narrow:
    cmp $8, %r15
    jl .Ltp_bmp_tiny
    mov %al, %dl
    mov %dl, %r8b
    mov %dl, %r9b
    jmp .Ltp_bmp_store
.Ltp_bmp_tiny:
    mov %r15d, %ecx
    mov $1, %rdi
    shl %cl, %rdi
    dec %rdi
    imul $255, %rax, %rax
    xor %edx, %edx
    div %edi
    mov %al, %dl
    mov %al, %r8b
    mov %al, %r9b
.Ltp_bmp_store:
    mov %r11, %rax
    lea (%rax,%rax,2), %rax
    add 80(%rsp), %rax
    movb %dl, (%rax)
    movb %r9b, 1(%rax)
    movb %r8b, 2(%rax)
    inc %r11
    jmp .Ltp_bmp_pix
.Ltp_bmp_pixd:
    mov 80(%rsp), %rdi
    mov 64(%rsp), %rsi
    mov $1, %rdx
    mov 72(%rsp), %rcx
    call fwrite@PLT
    cmp $1, %rax
    jne .Ltp_bmp_fail
    mov 56(%rsp), %rax
    dec %rax
    mov %rax, 56(%rsp)
    jns .Ltp_bmp_row
    mov 80(%rsp), %rdi
    call free@PLT
    movq $0, 80(%rsp)
    mov 72(%rsp), %rdi
    call fclose@PLT
    movq $0, 72(%rsp)
    test %eax, %eax
    jnz .Ltp_bmp_fail
    mov $1, %eax
    jmp .Ltp_bmp_leave
.Ltp_bmp_fail:
    mov 80(%rsp), %rdi
    test %rdi, %rdi
    jz .Ltp_bmp_nofree
    call free@PLT
.Ltp_bmp_nofree:
    mov 72(%rsp), %rdi
    test %rdi, %rdi
    jz .Ltp_bmp_noclose
    call fclose@PLT
.Ltp_bmp_noclose:
    xor %eax, %eax
.Ltp_bmp_leave:
    add $88, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
)asm";
  }
  if (saw_load) {
    out += R"asm(.Ltp_load_bytes:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $40, %rsp
    mov %rdi, %r12
    mov %rsi, %r13
    mov %rcx, %r14
    xor %r15, %r15
    mov %rdx, %rdi
    lea .Ltp_rb(%rip), %rsi
    call fopen@PLT
    test %rax, %rax
    jz .Ltp_lby_fail
    mov %rax, %r15
    mov %r15, %rdi
    xor %esi, %esi
    mov $2, %edx
    call fseek@PLT
    test %eax, %eax
    jnz .Ltp_lby_fail
    mov %r15, %rdi
    call ftell@PLT
    cmp $0, %rax
    jl .Ltp_lby_fail
    mov %rax, %rbx
    mov %r15, %rdi
    xor %esi, %esi
    xor %edx, %edx
    call fseek@PLT
    test %eax, %eax
    jnz .Ltp_lby_fail
    cmp %r13, %rbx
    jbe .Ltp_lby_len
    test %r14, %r14
    jz .Ltp_lby_fail
    mov %r13, %rbx
.Ltp_lby_len:
    mov %rbx, (%rsp)
    xor %rbx, %rbx
.Ltp_lby_loop:
    cmp (%rsp), %rbx
    jge .Ltp_lby_done
    mov %r15, %rdi
    call fgetc@PLT
    cmp $-1, %eax
    je .Ltp_lby_fail
    mov %rbx, %rcx
    shl $3, %rcx
    mov %r12, %rdx
    sub %rcx, %rdx
    mov %eax, (%rdx)
    inc %rbx
    jmp .Ltp_lby_loop
.Ltp_lby_done:
    mov %r15, %rdi
    call fclose@PLT
    mov (%rsp), %eax
    add $40, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.Ltp_lby_fail:
    test %r15, %r15
    jz .Ltp_lby_exit
    mov %r15, %rdi
    call fclose@PLT
.Ltp_lby_exit:
    mov $1, %edi
    call exit@PLT
.Ltp_load_bmp:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $88, %rsp
    mov %rdi, %r12
    mov %rsi, %rbx
    xor %r15, %r15
    movq $0, 72(%rsp)
    mov %rdx, %rdi
    lea .Ltp_rb(%rip), %rsi
    call fopen@PLT
    test %rax, %rax
    jz .Ltp_lbmp_fail
    mov %rax, %r15
    lea (%rsp), %rdi
    mov $1, %rsi
    mov $54, %rdx
    mov %r15, %rcx
    call fread@PLT
    cmp $54, %rax
    jne .Ltp_lbmp_fail
    cmpb $66, (%rsp)
    jne .Ltp_lbmp_fail
    cmpb $77, 1(%rsp)
    jne .Ltp_lbmp_fail
    movzwl 28(%rsp), %eax
    cmp $24, %eax
    jne .Ltp_lbmp_fail
    movl 30(%rsp), %eax
    test %eax, %eax
    jnz .Ltp_lbmp_fail
    mov 18(%rsp), %r13d
    mov 22(%rsp), %r14d
    movl $0, 56(%rsp)
    test %r14d, %r14d
    jns .Ltp_lbmp_pos
    negl %r14d
    movl $1, 56(%rsp)
.Ltp_lbmp_pos:
    test %r13d, %r13d
    jle .Ltp_lbmp_fail
    test %r14d, %r14d
    jle .Ltp_lbmp_fail
    mov %r13, %rax
    imul %r14, %rax
    jo .Ltp_lbmp_fail
    cmp %rbx, %rax
    jne .Ltp_lbmp_fail
    mov %r15, %rdi
    mov 10(%rsp), %esi
    xor %edx, %edx
    call fseek@PLT
    test %eax, %eax
    jnz .Ltp_lbmp_fail
    mov %r13, %rax
    lea (%rax,%rax,2), %rax
    add $3, %rax
    and $-4, %rax
    mov %rax, 64(%rsp)
    mov %rax, %rdi
    call malloc@PLT
    test %rax, %rax
    jz .Ltp_lbmp_fail
    mov %rax, 72(%rsp)
    movq $0, 80(%rsp)
.Ltp_lbmp_row:
    mov 72(%rsp), %rdi
    mov $1, %rsi
    mov 64(%rsp), %rdx
    mov %r15, %rcx
    call fread@PLT
    cmp 64(%rsp), %rax
    jne .Ltp_lbmp_fail
    mov 80(%rsp), %rax
    cmpl $0, 56(%rsp)
    jne .Ltp_lbmp_top
    mov %r14, %rax
    sub 80(%rsp), %rax
    dec %rax
    jmp .Ltp_lbmp_got
.Ltp_lbmp_top:
    mov 80(%rsp), %rax
.Ltp_lbmp_got:
    imul %r13, %rax
    mov %rax, %r10
    xor %r11, %r11
.Ltp_lbmp_pix:
    cmp %r13, %r11
    jge .Ltp_lbmp_pixd
    mov %r11, %rcx
    lea (%rcx,%rcx,2), %rcx
    mov 72(%rsp), %rdx
    movzbl (%rdx,%rcx), %esi
    movzbl 1(%rdx,%rcx), %edi
    movzbl 2(%rdx,%rcx), %eax
    shl $16, %eax
    shl $8, %edi
    or %edi, %eax
    or %esi, %eax
    mov %r10, %rdx
    add %r11, %rdx
    shl $3, %rdx
    mov %r12, %rcx
    sub %rdx, %rcx
    mov %eax, (%rcx)
    inc %r11
    jmp .Ltp_lbmp_pix
.Ltp_lbmp_pixd:
    incq 80(%rsp)
    mov 80(%rsp), %rax
    cmp %r14, %rax
    jl .Ltp_lbmp_row
    mov 72(%rsp), %rdi
    call free@PLT
    movq $0, 72(%rsp)
    mov %r15, %rdi
    call fclose@PLT
    add $88, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.Ltp_lbmp_fail:
    mov 72(%rsp), %rdi
    test %rdi, %rdi
    jz .Ltp_lbmp_nofree
    call free@PLT
.Ltp_lbmp_nofree:
    test %r15, %r15
    jz .Ltp_lbmp_exit
    mov %r15, %rdi
    call fclose@PLT
.Ltp_lbmp_exit:
    mov $1, %edi
    call exit@PLT
)asm";
  }
  if (needs_fail) out += ".Ltp_fail:\n    mov $1, %edi\n    call exit@PLT\n";
  if (saw_host) {
    out += "    .section .rodata\n";
    out += ".Ltp_fmt_d: .asciz \"%d\\n\"\n";
    out += ".Ltp_fmt_ll: .asciz \"%lld\\n\"\n";
    out += ".Ltp_fmt_f: .asciz \"%.9f\\n\"\n";
    out += ".Ltp_fmt_w: .asciz \"%s %.6f %.9f\\n\"\n";
    out += ".Ltp_true: .asciz \"true\\n\"\n";
    out += ".Ltp_false: .asciz \"false\\n\"\n";
    for (size_t i = 0; i < strs.size(); ++i) {
      out += ".Ltp_s" + std::to_string(i) + ": .asciz \"" + gas_string(strs[i]) + "\"\n";
    }
    if (saw_bmp) out += ".Ltp_wb: .asciz \"wb\"\n";
    if (saw_load) out += ".Ltp_rb: .asciz \"rb\"\n";
  }
  if (saw_fork || !bss.empty()) {
    out += "    .bss\n";
    if (saw_fork) {
      size_t n = prog.fns.size();
      if (n < 1) n = 1;
      out += "    .align 8\n";
      out += ".Ltp_fork_n: .zero " + std::to_string(8 * n) + "\n";
      out += ".Ltp_fork_x: .zero " + std::to_string(64 * n) + "\n";
    }
    out += bss;
  }
  out += "    .section .note.GNU-stack,\"\",@progbits\n";
  return out;
}

}  // namespace tp
