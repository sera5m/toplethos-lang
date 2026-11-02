#!/bin/sh
# Each Toplethos trial prints: name seconds sum
# Compiled N is 2000000, the same as C++ and Rust.
# The interpreter is the same file with N scaled to 20000, so its
# polynomial sums are smaller. The mutex sum stays 200000.
cd "$(dirname "$0")/.." || exit 1

echo "--- c++ ---"
g++ -O2 -ffp-contract=off -pthread -o /tmp/battery_cpp aot/battery.cpp || exit 1
/usr/bin/time -f 'cpp wall %e sec' /tmp/battery_cpp

echo "--- rust ---"
rustc -C opt-level=3 -o /tmp/battery_rs aot/battery.rs || exit 1
/usr/bin/time -f 'rust wall %e sec' /tmp/battery_rs

echo "--- toplethos compiled, N=2000000 ---"
python3 aot/tpc.py aot/battery.tpth_s -o /tmp/battery_tp || exit 1
/usr/bin/time -f 'tp wall %e sec' /tmp/battery_tp

echo "--- toplethos interpreted, N=20000 ---"
sed 's/2000000/20000/g' aot/battery.tpth_s > /tmp/battery_interp.tpth_s
/usr/bin/time -f 'interp wall %e sec' python3 toplethos_run.py /tmp/battery_interp.tpth_s
