#!/bin/sh
# Time the same three rows. C++ and Rust print their own rows.
# Toplethos prints three sums: direct, call, object.
# The interpreter runs 20000 steps, not 2000000. Compare iterations
# per second, not the raw seconds.
cd "$(dirname "$0")/.." || exit 1

echo "--- c++ ---"
g++ -O2 -ffp-contract=off -pthread -o /tmp/battery_cpp aot/battery.cpp || exit 1
/usr/bin/time -f 'cpp wall %e sec' /tmp/battery_cpp

echo "--- rust ---"
rustc -C opt-level=3 -o /tmp/battery_rs aot/battery.rs || exit 1
/usr/bin/time -f 'rust wall %e sec' /tmp/battery_rs

echo "--- toplethos compiled, N=2000000, three sums ---"
python3 aot/tpc.py aot/battery.tpth_s -o /tmp/battery_tp || exit 1
/usr/bin/time -f 'tp wall %e sec' /tmp/battery_tp

echo "--- toplethos interpreted, N=20000, three sums ---"
sed 's/do n\[2000000\]/do n[20000]/' aot/battery.tpth_s > /tmp/battery_interp.tpth_s
/usr/bin/time -f 'interp wall %e sec' python3 toplethos_run.py /tmp/battery_interp.tpth_s
