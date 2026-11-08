#!/bin/sh
# Time the night-street rings. Every program opens examples/street.bmp.
cd "$(dirname "$0")/.." || exit 1
EX="$(pwd)/examples"
mkdir -p "$EX/out"
make -C compiler tpcc
g++ -O2 -std=c++20 -ffp-contract=off -o /tmp/glitch_cpp "$EX/glitch.cpp" || exit 1
rustc -C opt-level=3 -C target-feature=-fma -o /tmp/glitch_rs "$EX/glitch.rs" || exit 1
compiler/tpcc --asm /tmp/glitch_tp.s "$EX/glitch.tpth_s" || exit 1
gcc -o /tmp/glitch_tp /tmp/glitch_tp.s || exit 1

echo "--- street.bmp ---"
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street.bmp" "$EX/out/glitch.ppm"
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street.bmp" "$EX/out/glitch_rs.ppm"
/usr/bin/time -f 'tp   %e' /tmp/glitch_tp
cmp -s "$EX/out/glitch.ppm" "$EX/out/glitch_rs.ppm" && echo "ppm match"
echo "photo   $EX/street.bmp"
echo "picture $EX/out/glitch.ppm"