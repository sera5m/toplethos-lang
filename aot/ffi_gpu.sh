#!/bin/sh
# 1. compiled Toplethos -> C -> ./bench 3
# 2. interpreted Toplethos -> C -> ./bench 3
#    stage jit in that run is the third path: g++ already finished
#    at the warmup, and @time["jit"] starts after that.
cd "$(dirname "$0")/.." || exit 1

if [ ! -x gpu/bench ]; then
  gcc -O2 -o gpu/bench gpu/bench.c -ldl || exit 1
fi

echo "=== 1 compiled -> ffi c -> gpu ==="
python3 aot/tpc.py aot/ffi_gpu_test.tpth_s -o /tmp/ffi_gpu_test --run || exit 1

echo "=== 2 interpreted -> ffi c -> gpu ==="
echo "plain and call are the interpreter. The pause before the first line is g++."
echo "=== 3 jit is the jit line: the inlined loop, after that compile ==="
python3 lang/host.py aot/ffi_gpu_test.tpth_s || exit 1
