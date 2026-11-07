#include "tp/diag.hpp"

namespace tp {

void Log::add(std::string line) {
  std::lock_guard<std::mutex> lock(mu_);
  buf_ += line;
  if (line.empty() || line.back() != '\n') buf_ += '\n';
}

std::string Log::text() const {
  std::lock_guard<std::mutex> lock(mu_);
  return buf_;
}

const Stage& stage_info(const std::string& name) {
  static const Stage rows[] = {
      {"lex", "Cuts the source into tokens. A number is still just digits. It is not i32 or i64 yet."},
      {"bind", "Looks each type name up in the type map and builds a typed unit. No registers, no opcodes."},
      {"bytecode", "Lowers the typed unit to three-address ops. Each op keeps the atom it computed."},
      {"interpret", "Runs the bytecode on this CPU, one opcode at a time. The atom picks the wrap. This is the oracle, not a GPU."},
      {"amd64", "Prints amd64 from the bytecode. It never reads a Toplethos keyword."},
      {"arm64", "Prints AArch64 from the same bytecode. The stage is the same walk as amd64, with different instructions."},
      {"cpp", "Prints C++ from the bytecode. The C++ is a target, not a second intermediate form."},
      {"spirv", "Prints SPIR-V operations from the bytecode. Vulkan later uploads that module. The card does not interpret the bytecode."},
  };
  for (const Stage& row : rows) {
    if (name == row.name) return row;
  }
  static const Stage unknown = {"unknown", "No stage by that name."};
  return unknown;
}

}  // namespace tp
