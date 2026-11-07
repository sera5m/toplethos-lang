CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude

SRC = src/diag.cpp src/types.cpp src/lex.cpp src/bind.cpp src/lower.cpp src/device.cpp \
      src/emit.cpp src/emit_amd64.cpp src/emit_arm64.cpp src/emit_cpp.cpp src/emit_spirv.cpp src/main.cpp

.PHONY: all test clean

all: tpcc

tpcc: $(SRC) include/tp/diag.hpp include/tp/types.hpp include/tp/token.hpp include/tp/unit.hpp include/tp/bytecode.hpp
	$(CXX) $(CXXFLAGS) -o tpcc $(SRC)

test: tpcc
	./tpcc --run tests/add.tpth_s | grep -qx 'result 42'
	./tpcc --run tests/wide.tpth_s | grep -qx 'result 42'
	./tpcc --dump-bytecode tests/wide.tpth_s | grep -q 'i64 bits=64'
	./tpcc --asm /tmp/tpcc_add.s tests/add.tpth_s
	gcc -o /tmp/tpcc_add /tmp/tpcc_add.s
	set +e; /tmp/tpcc_add; code=$$?; set -e; test $$code -eq 42
	./tpcc --target cpp /tmp/tpcc_add.cpp tests/add.tpth_s
	g++ -o /tmp/tpcc_add_cpp /tmp/tpcc_add.cpp
	set +e; /tmp/tpcc_add_cpp; code=$$?; set -e; test $$code -eq 42
	./tpcc --target arm64 /tmp/tpcc_add.arm tests/add.tpth_s
	grep -q 'add w8, w8, w9' /tmp/tpcc_add.arm
	./tpcc --target spirv /tmp/tpcc_wide.spv tests/wide.tpth_s
	grep -q 'OpIAdd %i64' /tmp/tpcc_wide.spv
	./tpcc --dump-tokens ../examples/gputwo.tpth_s >/tmp/tpcc_tokens.txt
	grep -q 'fn' /tmp/tpcc_tokens.txt

clean:
	rm -f tpcc
