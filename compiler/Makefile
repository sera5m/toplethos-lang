CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude

SRC = src/diag.cpp src/types.cpp src/lex.cpp src/bind.cpp src/lower.cpp src/device.cpp src/main.cpp

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
	./tpcc --dump-tokens ../examples/gputwo.tpth_s >/tmp/tpcc_tokens.txt
	grep -q 'fn' /tmp/tpcc_tokens.txt

clean:
	rm -f tpcc
