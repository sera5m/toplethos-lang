#include "tp/target.hpp"

namespace tp {

std::string emit_amd64(const Program& prog);
std::string emit_arm64(const Program& prog);
std::string emit_cpp(const Program& prog);
std::string emit_spirv(const Program& prog);

const char* target_name(Target target) {
  switch (target) {
    case Target::Amd64:
      return "amd64";
    case Target::Arm64:
      return "arm64";
    case Target::Cpp:
      return "cpp";
    case Target::Spirv:
      return "spirv";
  }
  return "unset";
}

std::string emit(Target target, const Program& prog) {
  switch (target) {
    case Target::Amd64:
      return emit_amd64(prog);
    case Target::Arm64:
      return emit_arm64(prog);
    case Target::Cpp:
      return emit_cpp(prog);
    case Target::Spirv:
      return emit_spirv(prog);
  }
  throw Fail("load", "no backend for that target", 0, 0);
}

}  // namespace tp
