#!/bin/sh
# Old Python host versus C++ and Rust. Not the unified compiler.
# Interpreter, amd64, and JIT of one bytecode are compiler/tests/mix.tpth_s.
# Mixed ops. One checksum, three languages. N = 1000000.
# The integer and float formulas are the same in mix.cpp, mix.rs, and mix.tpth_s.
cd "$(dirname "$0")/.." || exit 1

echo "--- mixed ops, N=1000000 ---"
g++ -O2 -ffp-contract=off -o /tmp/mix_cpp aot/mix.cpp || exit 1
rustc -C opt-level=3 -C llvm-args=--fp-contract=off -o /tmp/mix_rs aot/mix.rs || exit 1
python3 aot/tpc.py aot/mix.tpth_s -o /tmp/mix_tp || exit 1

/usr/bin/time -f 'cpp  %e' -o /tmp/mix_cpp.time /tmp/mix_cpp > /tmp/mix_cpp.out
/usr/bin/time -f 'rs   %e' -o /tmp/mix_rs.time /tmp/mix_rs > /tmp/mix_rs.out
/usr/bin/time -f 'tp   %e' -o /tmp/mix_tp.time /tmp/mix_tp > /tmp/mix_tp.out

cat /tmp/mix_cpp.out
cat /tmp/mix_cpp.time
cat /tmp/mix_rs.out
cat /tmp/mix_rs.time
cat /tmp/mix_tp.out
cat /tmp/mix_tp.time

c=$(awk '{print $3}' /tmp/mix_cpp.out)
r=$(awk '{print $3}' /tmp/mix_rs.out)
t=$(awk '{print $3}' /tmp/mix_tp.out)
if [ "$c" = "$r" ] && [ "$c" = "$t" ]; then
  echo "mix match $c"
else
  echo "mix MISMATCH cpp=$c rust=$r tp=$t" >&2
  exit 1
fi
