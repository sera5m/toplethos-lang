#include "tp/bytecode.hpp"
#include "tp/diag.hpp"
#include "tp/eval.hpp"
#include "tp/target.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

enum class Phase { Load, Lex, Bind, Bytecode, Eval, Interpret, Assemble, Done };

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw tp::Fail("load", "cannot read " + path, 0, 0);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void explain(const tp::Fail& err) {
  const tp::Stage& st = tp::stage_info(err.stage);
  std::cerr << "stage: " << err.stage << "\n";
  std::cerr << "does: " << st.does << "\n";
  if (err.line) std::cerr << "at: line " << err.line << " col " << err.col << "\n";
  std::cerr << "why: " << err.what() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  bool dump_types = false;
  bool dump_tokens = false;
  bool dump_bind = false;
  bool dump_bc = false;
  bool do_run = false;
  bool do_emit = false;
  tp::Target target = tp::Target::Amd64;
  std::string out_path;
  std::string file;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dump") {
      dump_types = dump_tokens = dump_bind = dump_bc = true;
    } else if (arg == "--dump-types") {
      dump_types = true;
    } else if (arg == "--dump-tokens") {
      dump_tokens = true;
    } else if (arg == "--dump-bind") {
      dump_bind = true;
    } else if (arg == "--dump-bytecode") {
      dump_bc = true;
    } else if (arg == "--run") {
      do_run = true;
    } else if (arg == "--asm" || arg == "--target") {
      do_emit = true;
      if (arg == "--target") {
        if (i + 1 >= argc) {
          std::cerr << "stage: load\nwhy: --target needs amd64, arm64, cpp, or spirv\n";
          return 1;
        }
        std::string name = argv[++i];
        if (name == "amd64") target = tp::Target::Amd64;
        else if (name == "arm64") target = tp::Target::Arm64;
        else if (name == "cpp") target = tp::Target::Cpp;
        else if (name == "spirv") target = tp::Target::Spirv;
        else {
          std::cerr << "stage: load\nwhy: unknown target " << name << "\n";
          return 1;
        }
      }
      if (i + 1 >= argc) {
        std::cerr << "stage: " << tp::target_name(target) << "\nwhy: needs an output path\n";
        return 1;
      }
      out_path = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "stage: load\nwhy: unknown flag " << arg << "\n";
      return 1;
    } else {
      file = arg;
    }
  }
  if (file.empty()) {
    std::cerr << "usage: tpcc [--dump] [--run] [--asm out.s | --target amd64|arm64|cpp|spirv out] file.tpth_s\n";
    return 2;
  }

  Phase phase = Phase::Load;
  try {
    std::string src;
    std::vector<tp::Token> toks;
    tp::TypeMap types;
    tp::Unit unit;
    tp::Program prog;
    switch (phase) {
      case Phase::Load:
        src = read_file(file);
        phase = Phase::Lex;
        [[fallthrough]];
      case Phase::Lex:
        toks = tp::tokenize(src);
        if (dump_tokens) std::cout << tp::dump_tokens(toks);
        if (dump_types) {
          std::cout << "stage types\n";
          std::cout << "The type map is data. A name is looked up. It is not a switch on the spelling.\n";
          for (const tp::Atom& atom : types.atoms()) {
            std::cout << "atom " << atom.name << " bits=" << atom.bits << " family=" << tp::TypeMap::family_name(atom.family)
                      << " specifics=" << atom.specifics << "\n";
          }
        }
        if (dump_tokens && !dump_bind && !dump_bc && !do_run && !do_emit && !dump_types) return 0;
        phase = Phase::Bind;
        [[fallthrough]];
      case Phase::Bind:
        unit = tp::bind_unit(toks, types);
        if (dump_bind) std::cout << tp::dump_unit(unit);
        phase = Phase::Bytecode;
        [[fallthrough]];
      case Phase::Bytecode:
        prog = tp::lower(unit);
        phase = Phase::Eval;
        [[fallthrough]];
      case Phase::Eval: {
        tp::Evaluated ev = tp::evaluate(std::move(prog));
        prog = std::move(ev.prog);
        if (dump_bc) {
          std::cout << ev.report;
          std::cout << tp::dump_program(prog);
        }
        phase = do_emit ? Phase::Assemble : Phase::Interpret;
        if (!do_run && !do_emit) return 0;
        [[fallthrough]];
      }
      case Phase::Interpret:
        if (do_run) {
          int64_t value = tp::interpret(prog, "main");
          std::cout << "result " << value << "\n";
        }
        if (!do_emit) return 0;
        phase = Phase::Assemble;
        [[fallthrough]];
      case Phase::Assemble: {
        std::string text = tp::emit(target, prog);
        std::ofstream out(out_path);
        if (!out) throw tp::Fail(tp::target_name(target), "cannot write " + out_path, 0, 0);
        out << text;
        return 0;
      }
      case Phase::Done:
        return 0;
    }
  } catch (const tp::Fail& err) {
    explain(err);
    return 1;
  } catch (const std::exception& err) {
    std::cerr << "stage: " << "unset" << "\nwhy: " << err.what() << "\n";
    return 1;
  }
  return 0;
}
