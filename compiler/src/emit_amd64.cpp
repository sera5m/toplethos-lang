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
  bool saw_fork = false;
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
    int frame = static_cast<int>(((fn.nslots * 8) + 15) & ~15);
    if (frame < 16) frame = 16;
    out += "    .globl " + fn.shape.name + "\n";
    out += fn.shape.name + ":\n";
    out += "    push %rbp\n    mov %rsp, %rbp\n";
    out += "    sub $" + std::to_string(frame) + ", %rsp\n";
    auto slot = [](uint32_t id) { return "-" + std::to_string(8 * (id + 1)) + "(%rbp)"; };
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
        case Op::StaticInit:
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
  }
  if (saw_fork) {
    size_t n = prog.fns.size();
    if (n < 1) n = 1;
    out += "    .bss\n    .align 8\n";
    out += ".Ltp_fork_n: .zero " + std::to_string(8 * n) + "\n";
    out += ".Ltp_fork_x: .zero " + std::to_string(64 * n) + "\n";
  }
  out += "    .section .note.GNU-stack,\"\",@progbits\n";
  return out;
}

}  // namespace tp
