#!/bin/sh
# naive.tpth_s is the default battery. smart.tpth_s is the same math
# with @val and @static coefficients. Compiled N is 2000000.
cd "$(dirname "$0")/.." || exit 1

echo "--- c++ ---"
g++ -O2 -ffp-contract=off -pthread -o /tmp/battery_cpp aot/battery.cpp || exit 1
/usr/bin/time -f 'cpp wall %e sec' /tmp/battery_cpp

echo "--- rust ---"
rustc -C opt-level=3 -o /tmp/battery_rs aot/battery.rs || exit 1
/usr/bin/time -f 'rust wall %e sec' /tmp/battery_rs

echo "--- toplethos, UMMES ---"
make -C compiler tpcc
compiler/tpcc --asm /tmp/battery_tp.s aot/naive.tpth_s || exit 1
gcc -o /tmp/battery_tp /tmp/battery_tp.s || exit 1
/usr/bin/time -f 'naive wall %e sec' /tmp/battery_tp
compiler/tpcc --run aot/naive.tpth_s
compiler/tpcc --asm /tmp/battery_smart.s aot/smart.tpth_s || exit 1
gcc -o /tmp/battery_smart /tmp/battery_smart.s || exit 1
/usr/bin/time -f 'smart wall %e sec' /tmp/battery_smart

