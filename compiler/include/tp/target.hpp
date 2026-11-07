#pragma once

#include "tp/bytecode.hpp"

namespace tp {

/* Backends read Program and nothing earlier. Tokens are not a target input. */
enum class Target { Amd64, Arm64, Cpp, Spirv };

const char* target_name(Target target);
std::string emit(Target target, const Program& prog);

}  // namespace tp
