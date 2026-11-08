#!/bin/sh
# 1. compiled Toplethos -> C -> ./bench 3
# 2. interpreted Toplethos -> C -> ./bench 3
#    stage jit in that run is the third path: g++ already finished
#    at the warmup, and @time["jit"] starts after that.
cd "$(dirname "$0")/.." || exit 1

if [ ! -x gpu/bench ]; then
  gcc -O2 -o gpu/bench gpu/bench.c -ldl || exit 1
fi

echo "=== compiled, interpreted, and jit are one program ==="
make -C compiler tpcc
compiler/tpcc --asm /tmp/ffi_gpu_test.s aot/ffi_gpu_test.tpth_s || exit 1
gcc -o /tmp/ffi_gpu_test /tmp/ffi_gpu_test.s || exit 1
/tmp/ffi_gpu_test || exit 1
compiler/tpcc --run aot/ffi_gpu_test.tpth_s || exit 1
compiler/tpcc --jit aot/ffi_gpu_test.tpth_s || exit 1
