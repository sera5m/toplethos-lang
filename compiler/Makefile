CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude

SRC = src/diag.cpp src/types.cpp src/lex.cpp src/bind.cpp src/lower.cpp src/arith.cpp src/eval.cpp src/device.cpp \
      src/jit.cpp src/emit.cpp src/emit_amd64.cpp src/emit_arm64.cpp src/emit_cpp.cpp src/emit_spirv.cpp src/main.cpp

.PHONY: all test clean

all: tpcc

tpcc: $(SRC) include/tp/diag.hpp include/tp/types.hpp include/tp/token.hpp include/tp/unit.hpp include/tp/bytecode.hpp include/tp/arch.hpp include/tp/pointer.hpp include/tp/arith.hpp include/tp/eval.hpp include/tp/target.hpp include/tp/jit.hpp
	$(CXX) $(CXXFLAGS) -o tpcc $(SRC) -ldl

test: tpcc
	./tpcc --run tests/add.tpth_s | grep -qx 'result 42'
	./tpcc --run tests/wide.tpth_s | grep -qx 'result 42'
	./tpcc --dump-bytecode tests/wide.tpth_s | grep -q 'i64 bits=64'
	./tpcc --asm /tmp/tpcc_add.s tests/add.tpth_s
	gcc -o /tmp/tpcc_add /tmp/tpcc_add.s
	set +e; /tmp/tpcc_add; code=$$?; set -e; test $$code -eq 42
	grep -q '$$42' /tmp/tpcc_add.s
	if grep -q ' add ' /tmp/tpcc_add.s; then echo 'asm ran before eval'; exit 1; fi
	./tpcc --target cpp /tmp/tpcc_add.cpp tests/add.tpth_s
	g++ -o /tmp/tpcc_add_cpp /tmp/tpcc_add.cpp
	set +e; /tmp/tpcc_add_cpp; code=$$?; set -e; test $$code -eq 42
	./tpcc --target arm64 /tmp/tpcc_add.arm tests/add.tpth_s
	grep -q '#42' /tmp/tpcc_add.arm
	./tpcc --target spirv /tmp/tpcc_wide.spv tests/wide.tpth_s
	grep -q 'OpConstant %i64 42' /tmp/tpcc_wide.spv
	if grep -q OpIAdd /tmp/tpcc_wide.spv; then echo 'spirv ran before eval'; exit 1; fi
	if ./tpcc --asm /tmp/tpcc_ui.s tests/ui.tpth_s >/tmp/tpcc_ui.err 2>&1; then echo 'amd64 accepted ui32'; exit 1; fi
	grep -q 'no amd64 row for ui32' /tmp/tpcc_ui.err
	./tpcc --target cpp /tmp/tpcc_ui.cpp tests/ui.tpth_s
	grep -q uint32_t /tmp/tpcc_ui.cpp
	./tpcc --run tests/loop.tpth_s | grep -qx 'result 6'
	./tpcc --asm /tmp/tpcc_loop.s tests/loop.tpth_s
	gcc -o /tmp/tpcc_loop /tmp/tpcc_loop.s
	set +e; /tmp/tpcc_loop; code=$$?; set -e; test $$code -eq 6
	./tpcc --target cpp /tmp/tpcc_loop.cpp tests/loop.tpth_s
	g++ -o /tmp/tpcc_loop_cpp /tmp/tpcc_loop.cpp
	set +e; /tmp/tpcc_loop_cpp; code=$$?; set -e; test $$code -eq 6
	./tpcc --target spirv /tmp/tpcc_loop.spv tests/loop.tpth_s
	grep -q OpBranchConditional /tmp/tpcc_loop.spv
	grep -q OpIAdd /tmp/tpcc_loop.spv
	./tpcc --asm /tmp/tpcc_id.s tests/id.tpth_s
	grep -q '%edi' /tmp/tpcc_id.s
	if ./tpcc --run tests/fall.tpth_s >/tmp/tpcc_fall.err 2>&1; then echo 'missing return was accepted'; exit 1; fi
	grep -q 'does not return' /tmp/tpcc_fall.err
	if ./tpcc --run tests/onepath.tpth_s >/tmp/tpcc_one.err 2>&1; then echo 'one path return was accepted'; exit 1; fi
	grep -q 'does not return' /tmp/tpcc_one.err
	if ./tpcc --run tests/shiftbad.tpth_s >/tmp/tpcc_shbad.err 2>&1; then echo 'shift 32 was accepted'; exit 1; fi
	grep -q 'shift count is outside' /tmp/tpcc_shbad.err
	if ./tpcc --run tests/negmin.tpth_s >/tmp/tpcc_negmin.err 2>&1; then echo 'neg min was accepted'; exit 1; fi
	grep -q 'lowest signed value' /tmp/tpcc_negmin.err
	if ./tpcc --run tests/divmin.tpth_s >/tmp/tpcc_divmin.err 2>&1; then echo 'div min was accepted'; exit 1; fi
	grep -q 'lowest signed value divided by -1' /tmp/tpcc_divmin.err
	./tpcc --run tests/shift.tpth_s | grep -qx 'result 8'
	./tpcc --run tests/neg.tpth_s | grep -qx 'result -5'
	./tpcc --run tests/wide_shl.tpth_s | grep -qx 'result 4'
	./tpcc --dump-bytecode tests/wide_shl.tpth_s | grep -q 'i64 bits=64'
	./tpcc --run tests/range.tpth_s | grep -qx 'result 7'
	./tpcc --asm /tmp/tpcc_range.s tests/range.tpth_s
	grep -q ' ja ' /tmp/tpcc_range.s
	if grep -q setge /tmp/tpcc_range.s; then echo 'range if still setcc'; exit 1; fi
	gcc -o /tmp/tpcc_range /tmp/tpcc_range.s
	set +e; /tmp/tpcc_range; code=$$?; set -e; test $$code -eq 7
	./tpcc --target cpp /tmp/tpcc_range.cpp tests/range.tpth_s
	g++ -o /tmp/tpcc_range_cpp /tmp/tpcc_range.cpp
	set +e; /tmp/tpcc_range_cpp; code=$$?; set -e; test $$code -eq 7
	./tpcc --target spirv /tmp/tpcc_range.spv tests/range.tpth_s
	grep -q OpUGreaterThan /tmp/tpcc_range.spv
	./tpcc --run tests/rangeout.tpth_s | grep -qx 'result 9'
	./tpcc --run tests/stored.tpth_s | grep -qx 'result 5'
	./tpcc --asm /tmp/tpcc_stored.s tests/stored.tpth_s
	grep -q setge /tmp/tpcc_stored.s
	./tpcc --run tests/call.tpth_s | grep -qx 'result 42'
	./tpcc --dump-bytecode tests/call.tpth_s | grep -q 'drop=1'
	./tpcc --asm /tmp/tpcc_call.s tests/call.tpth_s
	gcc -o /tmp/tpcc_call /tmp/tpcc_call.s
	set +e; /tmp/tpcc_call; code=$$?; set -e; test $$code -eq 42
	./tpcc --target cpp /tmp/tpcc_call.cpp tests/call.tpth_s
	g++ -o /tmp/tpcc_call_cpp /tmp/tpcc_call.cpp
	set +e; /tmp/tpcc_call_cpp; code=$$?; set -e; test $$code -eq 42
	if ./tpcc --target spirv /tmp/tpcc_call.spv tests/call.tpth_s >/tmp/tpcc_callspv.err 2>&1; then echo 'spirv accepted a call'; exit 1; fi
	grep -q 'calling convention' /tmp/tpcc_callspv.err
	./tpcc --run tests/nest.tpth_s | grep -qx 'result 6'
	./tpcc --asm /tmp/tpcc_nest.s tests/nest.tpth_s
	gcc -o /tmp/tpcc_nest /tmp/tpcc_nest.s
	set +e; /tmp/tpcc_nest; code=$$?; set -e; test $$code -eq 6
	./tpcc --target cpp /tmp/tpcc_nest.cpp tests/nest.tpth_s
	g++ -o /tmp/tpcc_nest_cpp /tmp/tpcc_nest.cpp
	set +e; /tmp/tpcc_nest_cpp; code=$$?; set -e; test $$code -eq 6
	./tpcc --dump-bytecode tests/loop.tpth_s | grep -q 'inc=3'
	./tpcc --dump-bytecode tests/loop.tpth_s | grep -q 'forward=1'
	./tpcc --dump-bytecode tests/loop.tpth_s | grep -q 'cost=unknown'
	./tpcc --active tests/active.tpth_s | grep -q 'symbol id calls=2 callers=main'
	./tpcc --active tests/active.tpth_s | grep -q 'symbol main calls=1 callers=-'
	./tpcc --active tests/active.tpth_s | grep -q 'cost=unknown'
	nproc=$$(getconf _NPROCESSORS_ONLN); ./tpcc --run tests/threads.tpth_s | grep -qx "result $$nproc"
	./tpcc --asm /tmp/tpcc_threads.s tests/threads.tpth_s
	gcc -o /tmp/tpcc_threads /tmp/tpcc_threads.s
	set +e; /tmp/tpcc_threads; code=$$?; set -e; test $$code -eq $$(getconf _NPROCESSORS_ONLN)
	./tpcc --target cpp /tmp/tpcc_threads.cpp tests/threads.tpth_s
	g++ -o /tmp/tpcc_threads_cpp /tmp/tpcc_threads.cpp
	set +e; /tmp/tpcc_threads_cpp; code=$$?; set -e; test $$code -eq $$(getconf _NPROCESSORS_ONLN)
	if ./tpcc --target spirv /tmp/tpcc_threads.spv tests/threads.tpth_s >/tmp/tpcc_thspv.err 2>&1; then echo 'spirv accepted threads'; exit 1; fi
	grep -q 'no spirv row' /tmp/tpcc_thspv.err
	./tpcc --run tests/freemem.tpth_s | awk '{ if ($$2+0 < 1048576) exit 1 }'
	./tpcc --dump-bytecode tests/freemem.tpth_s | grep -q freemem
	./tpcc --target cpp /tmp/tpcc_free.cpp tests/freemem.tpth_s
	g++ -o /tmp/tpcc_free /tmp/tpcc_free.cpp
	./tpcc --asm /tmp/tpcc_shlive.s tests/shlive.tpth_s
	grep -q 'jae .Ltp_fail' /tmp/tpcc_shlive.s
	gcc -o /tmp/tpcc_shlive /tmp/tpcc_shlive.s
	set +e; /tmp/tpcc_shlive; code=$$?; set -e; test $$code -eq 8
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr cell'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr ffi'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr inlined'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr jit-fn'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr jit-var'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr neuron'
	./tpcc --dump-types tests/add.tpth_s | grep -q 'ptr manager'
	./tpcc --dump-bytecode tests/add.tpth_s | grep -q 'pointers cell=- ffi=- inlined=- jit-fn=- jit-var=- neuron=- manager=-'
	./tpcc --run tests/ptr.tpth_s | grep -qx 'result 3'
	./tpcc --dump-bytecode tests/ptr.tpth_s | grep -q 'pointers cell=yes ffi=- inlined=- jit-fn=- jit-var=- neuron=- manager=-'
	./tpcc --asm /tmp/tpcc_ptr.s tests/ptr.tpth_s
	gcc -o /tmp/tpcc_ptr /tmp/tpcc_ptr.s
	set +e; /tmp/tpcc_ptr; code=$$?; set -e; test $$code -eq 3
	./tpcc --target cpp /tmp/tpcc_ptr.cpp tests/ptr.tpth_s
	g++ -o /tmp/tpcc_ptr_cpp /tmp/tpcc_ptr.cpp
	set +e; /tmp/tpcc_ptr_cpp; code=$$?; set -e; test $$code -eq 3
	./tpcc --target arm64 /tmp/tpcc_ptr.arm tests/ptr.tpth_s
	grep -q 'bl bump' /tmp/tpcc_ptr.arm
	if ./tpcc --target spirv /tmp/tpcc_ptr.spv tests/ptr.tpth_s >/tmp/tpcc_ptrspv.err 2>&1; then echo 'spirv accepted a pointer parameter'; exit 1; fi
	grep -q 'calling convention' /tmp/tpcc_ptrspv.err
	./tpcc --run tests/cell.tpth_s | grep -qx 'result 2'
	./tpcc --asm /tmp/tpcc_cell.s tests/cell.tpth_s
	gcc -o /tmp/tpcc_cell /tmp/tpcc_cell.s
	set +e; /tmp/tpcc_cell; code=$$?; set -e; test $$code -eq 2
	./tpcc --target cpp /tmp/tpcc_cell.cpp tests/cell.tpth_s
	g++ -o /tmp/tpcc_cell_cpp /tmp/tpcc_cell.cpp
	set +e; /tmp/tpcc_cell_cpp; code=$$?; set -e; test $$code -eq 2
	if ./tpcc --target spirv /tmp/tpcc_cell.spv tests/cell.tpth_s >/tmp/tpcc_cellspv.err 2>&1; then echo 'spirv accepted a cell pointer'; exit 1; fi
	grep -q 'no spirv row for cell' /tmp/tpcc_cellspv.err
	grep -q 'shader does not keep a host pointer' /tmp/tpcc_cellspv.err
	if ./tpcc --run tests/ptrord.tpth_s >/tmp/tpcc_ptrord.err 2>&1; then echo 'ordered pointer compare was accepted'; exit 1; fi
	grep -q 'ordered compare of pointers' /tmp/tpcc_ptrord.err
	./tpcc --dump-bytecode tests/jit.tpth_s | grep -q 'device hot run=cpu reason=scalar gpu-ops=yes'
	./tpcc --dump-bytecode tests/jit.tpth_s | grep -q 'add i32 bits=32 t' 
	./tpcc --dump-bytecode tests/jit.tpth_s | grep -q 'gpu=yes'
	./tpcc --dump-bytecode tests/threads.tpth_s | grep -q 'gpu=no'
	./tpcc --dump-bytecode tests/jit.tpth_s | grep -q 'inlined=yes'
	if ./tpcc --jit tests/jitbad.tpth_s >/tmp/tpcc_jitbad.err 2>&1; then echo 'impure inlined was accepted'; exit 1; fi
	grep -q 'not pure (call)' /tmp/tpcc_jitbad.err
	./tpcc --jit tests/jit.tpth_s >/tmp/tpcc_jit.out
	grep -q 'jit hot run=cpu reason=scalar gpu-ops=yes dirty=yes jit-fn=yes check=same' /tmp/tpcc_jit.out
	grep -q 'jit sum_n run=cpu reason=scalar gpu-ops=yes dirty=- jit-fn=- check=-' /tmp/tpcc_jit.out
	grep -qx 'result 0' /tmp/tpcc_jit.out || grep -q '^result 0$$' /tmp/tpcc_jit.out
	awk '/^jit sum_n / { for (i = 1; i <= NF; i++) if ($$i ~ /^interp_us=/) interp = substr($$i, 11) } /^jit hot / { for (i = 1; i <= NF; i++) if ($$i ~ /^native_us=/) native = substr($$i, 11) } END { if (native+0 <= 0 || interp+0 <= native+0) exit 1 }' /tmp/tpcc_jit.out
	./tpcc --run tests/mix.tpth_s | grep -qx 'result 0'
	./tpcc --asm /tmp/tpcc_mix.s tests/mix.tpth_s
	gcc -o /tmp/tpcc_mix /tmp/tpcc_mix.s
	set +e; /tmp/tpcc_mix; code=$$?; set -e; test $$code -eq 0
	./tpcc --target cpp /tmp/tpcc_mix.cpp tests/mix.tpth_s
	g++ -o /tmp/tpcc_mix_cpp /tmp/tpcc_mix.cpp
	set +e; /tmp/tpcc_mix_cpp; code=$$?; set -e; test $$code -eq 0
	./tpcc --jit tests/mix.tpth_s >/tmp/tpcc_mixjit.out
	grep -q 'jit hot run=cpu reason=scalar gpu-ops=yes dirty=yes jit-fn=yes check=same' /tmp/tpcc_mixjit.out
	grep -q 'jit mix ' /tmp/tpcc_mixjit.out
	grep -qx 'result 0' /tmp/tpcc_mixjit.out
	awk '/^jit mix / { for (i = 1; i <= NF; i++) if ($$i ~ /^interp_us=/) interp = substr($$i, 11) } /^jit hot / { for (i = 1; i <= NF; i++) if ($$i ~ /^native_us=/) native = substr($$i, 11) } END { if (native+0 <= 0 || interp+0 <= native+0) exit 1 }' /tmp/tpcc_mixjit.out
	./tpcc --dump-tokens ../examples/gputwo.tpth_s >/tmp/tpcc_tokens.txt
	grep -q 'fn' /tmp/tpcc_tokens.txt

clean:
	rm -f tpcc
