#!/bin/sh
# Time the night-street rings. Every program opens examples/street.bmp.
cd "$(dirname "$0")/.." || exit 1
EX="$(pwd)/examples"
mkdir -p "$EX/out"
g++ -O2 -std=c++20 -ffp-contract=off -o /tmp/glitch_cpp "$EX/glitch.cpp" || exit 1
rustc -C opt-level=3 -C target-feature=-fma -o /tmp/glitch_rs "$EX/glitch.rs" || exit 1
python3 aot/tpc.py "$EX/glitch.tpth_s" -o /tmp/glitch_tp || exit 1

echo "--- street.bmp ---"
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street.bmp" "$EX/out/glitch.ppm"
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street.bmp" "$EX/out/glitch_rs.ppm"
/usr/bin/time -f 'tp   %e' /tmp/glitch_tp
cmp -s "$EX/out/glitch.ppm" "$EX/out/glitch_rs.ppm" && echo "ppm match"
echo "photo   $EX/street.bmp"
echo "picture $EX/out/glitch.ppm"