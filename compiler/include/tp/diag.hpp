#pragma once

#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace tp {

/* Shared dump buffer. One owner writes a stage, the driver reads it.
   Each line is a std::string. The mutex is on this buffer only: locking
   every string in the compiler would serialize work that has one owner. */
class Log {
 public:
  void add(std::string line);
  std::string text() const;

 private:
  mutable std::mutex mu_;
  std::string buf_;
};

struct Fail : std::runtime_error {
  std::string stage;
  int line = 0;
  int col = 0;
  Fail(std::string stage, std::string why, int line, int col)
      : std::runtime_error(std::move(why)), stage(std::move(stage)), line(line), col(col) {}
};

struct Stage {
  const char* name;
  const char* does;
};

const Stage& stage_info(const std::string& name);

}  // namespace tp
