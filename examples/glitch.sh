#!/bin/sh
# Time the night-street rings. The photo is examples/street.jpg.
# Toplethos cannot open it, so the 40x30 frame is baked into glitch.tpth_s.
# C++ and Rust read examples/street160.rgb, which is that photo at 160x120.
cd "$(dirname "$0")/.." || exit 1
EX="$(pwd)/examples"
mkdir -p "$EX/out"
g++ -O2 -std=c++20 -ffp-contract=off -o /tmp/glitch_cpp "$EX/glitch.cpp" || exit 1
rustc -C opt-level=3 -C target-feature=-fma -o /tmp/glitch_rs "$EX/glitch.rs" || exit 1
python3 aot/tpc.py "$EX/glitch.tpth_s" -o /tmp/glitch_tp || exit 1

echo "--- 40x30 ---"
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street40.rgb" 40 30
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street40.rgb" 40 30
/usr/bin/time -f 'tp   %e' /tmp/glitch_tp

echo "--- 160x120 ---"
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street160.rgb" 160 120 "$EX/out/glitch.ppm"
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street160.rgb" 160 120 "$EX/out/glitch_rs.ppm"
cmp -s "$EX/out/glitch.ppm" "$EX/out/glitch_rs.ppm" && echo "ppm match"
echo "photo   $EX/street.jpg"
echo "picture $EX/out/glitch.ppm"
