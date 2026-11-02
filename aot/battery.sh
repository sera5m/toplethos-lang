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

echo "--- toplethos naive, N=2000000 ---"
python3 aot/tpc.py aot/naive.tpth_s -o /tmp/battery_tp || exit 1
/usr/bin/time -f 'naive wall %e sec' /tmp/battery_tp

echo "--- toplethos smart, N=2000000 ---"
python3 aot/tpc.py aot/smart.tpth_s -o /tmp/battery_smart || exit 1
/usr/bin/time -f 'smart wall %e sec' /tmp/battery_smart

echo "--- toplethos naive interpreted, N=20000 ---"
sed 's/2000000/20000/g' aot/naive.tpth_s > /tmp/battery_interp.tpth_s
/usr/bin/time -f 'naive interp %e sec' python3 toplethos_run.py /tmp/battery_interp.tpth_s

echo "--- toplethos smart interpreted, N=20000 ---"
sed 's/2000000/20000/g' aot/smart.tpth_s > /tmp/battery_smart_interp.tpth_s
/usr/bin/time -f 'smart interp %e sec' python3 toplethos_run.py /tmp/battery_smart_interp.tpth_s
