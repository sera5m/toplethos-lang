#pragma once

#include "tp/bytecode.hpp"

#include <string>

namespace tp {

struct Evaluated {
  Program prog;
  std::string report;
};

/* Passes run on the bytecode, in order, before any backend. */
Evaluated evaluate(Program prog);

}  // namespace tp
