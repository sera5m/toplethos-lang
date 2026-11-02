#!/bin/sh
# Real-world rows. The Horner battery stays in aot/battery.sh.
# The directory walk only reads. It does not create or edit anything.
# Toplethos has no directory calls, so that row is C++ and Rust only.
cd "$(dirname "$0")/.." || exit 1
ROOT="$(pwd)"
REAL="$ROOT/aot/real"
EX="$ROOT/examples"

cxx() { g++ -O2 -std=c++20 -ffp-contract=off -pthread -o "$2" "$1" || exit 1; }
rsc() { rustc -C opt-level=3 -C target-feature=-fma -o "$2" "$1" || exit 1; }
tpc() { python3 aot/tpc.py "$1" -o "$2" || exit 1; }

echo "--- dict ---"
cxx "$REAL/dict.cpp" /tmp/dict_cpp
rsc "$REAL/dict.rs" /tmp/dict_rs
tpc "$REAL/dict.tpth_s" /tmp/dict_tp
/usr/bin/time -f 'cpp  %e' /tmp/dict_cpp
/usr/bin/time -f 'rs   %e' /tmp/dict_rs
/usr/bin/time -f 'tp   %e' /tmp/dict_tp

echo "--- table ---"
cxx "$REAL/table.cpp" /tmp/table_cpp
rsc "$REAL/table.rs" /tmp/table_rs
tpc "$REAL/table.tpth_s" /tmp/table_tp
/usr/bin/time -f 'cpp  %e' /tmp/table_cpp
/usr/bin/time -f 'rs   %e' /tmp/table_rs
/usr/bin/time -f 'tp   %e' /tmp/table_tp

echo "--- glitch 40x30 ---"
cxx "$EX/glitch.cpp" /tmp/glitch_cpp
rsc "$EX/glitch.rs" /tmp/glitch_rs
tpc "$EX/glitch.tpth_s" /tmp/glitch_tp
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street40.rgb" 40 30
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street40.rgb" 40 30
/usr/bin/time -f 'tp   %e' /tmp/glitch_tp

echo "--- glitch 160x120 (cpp, rust) ---"
mkdir -p "$EX/out" "$REAL/out"
/usr/bin/time -f 'cpp  %e' /tmp/glitch_cpp "$EX/street160.rgb" 160 120 "$EX/out/glitch.ppm"
/usr/bin/time -f 'rs   %e' /tmp/glitch_rs "$EX/street160.rgb" 160 120 "$EX/out/glitch_rs.ppm"
cmp -s "$EX/out/glitch.ppm" "$EX/out/glitch_rs.ppm" && echo "glitch ppm match"
cp "$EX/out/glitch.ppm" "$REAL/out/glitch.ppm"

echo "--- blur 40x30 ---"
cxx "$REAL/blur.cpp" /tmp/blur_cpp
rsc "$REAL/blur.rs" /tmp/blur_rs
tpc "$REAL/blur.tpth_s" /tmp/blur_tp
/usr/bin/time -f 'cpp  %e' /tmp/blur_cpp "$EX/street40.rgb" 40 30
/usr/bin/time -f 'rs   %e' /tmp/blur_rs "$EX/street40.rgb" 40 30
/usr/bin/time -f 'tp   %e' /tmp/blur_tp

echo "--- blur 160x120 (cpp, rust) ---"
/usr/bin/time -f 'cpp  %e' /tmp/blur_cpp "$EX/street160.rgb" 160 120 "$REAL/out/blur.ppm"
/usr/bin/time -f 'rs   %e' /tmp/blur_rs "$EX/street160.rgb" 160 120 "$REAL/out/blur_rs.ppm"
cmp -s "$REAL/out/blur.ppm" "$REAL/out/blur_rs.ppm" && echo "blur ppm match"
python3 - "$EX" "$REAL" << 'PY'
import sys
from pathlib import Path
ex = Path(sys.argv[1])
real = Path(sys.argv[2])
out = real / "out"
try:
    from PIL import Image
except Exception:
    print("pictures are ppm in", ex / "out")
    raise SystemExit(0)

def load(path):
    data = Path(path).read_bytes().split(b"\n", 3)[3]
    return Image.frombytes("RGB", (160, 120), data)

src = Image.frombytes("RGB", (160, 120), (ex / "street160.rgb").read_bytes())
frames = [src, load(ex / "out" / "glitch.ppm"), load(out / "blur.ppm")]
big = [f.resize((640, 480), Image.Resampling.NEAREST) for f in frames]
sheet = Image.new("RGB", (640 * 3, 480))
for i, f in enumerate(big):
    sheet.paste(f, (i * 640, 0))
sheet.save(out / "street.png")
print("wrote", out / "street.png")
PY

echo "--- rdp ---"
cxx "$REAL/rdp.cpp" /tmp/rdp_cpp
rsc "$REAL/rdp.rs" /tmp/rdp_rs
tpc "$REAL/rdp.tpth_s" /tmp/rdp_tp
/usr/bin/time -f 'cpp  %e' /tmp/rdp_cpp "$REAL/out/butterfly.ppm"
/usr/bin/time -f 'rs   %e' /tmp/rdp_rs
/usr/bin/time -f 'tp   %e' /tmp/rdp_tp
echo "butterfly picture: $REAL/out/butterfly.ppm"

echo "--- words (skipped if the text file is absent) ---"
if [ -f /home/ash/Downloads/lipsum-cicero.txt ]; then
  cxx "$ROOT/aot/words.cpp" /tmp/words_cpp
  rsc "$ROOT/aot/words.rs" /tmp/words_rs
  python3 aot/tpc.py "$ROOT/aot/words.tpth_s" -o /tmp/words_tp || exit 1
  /usr/bin/time -f 'cpp  %e' /tmp/words_cpp > /tmp/words_cpp.txt
  /usr/bin/time -f 'rs   %e' /tmp/words_rs > /tmp/words_rs.txt
  /usr/bin/time -f 'tp   %e' /tmp/words_tp > /tmp/words_tp.txt
  diff -q /tmp/words_cpp.txt /tmp/words_rs.txt
  diff -q /tmp/words_cpp.txt /tmp/words_tp.txt
else
  echo "no /home/ash/Downloads/lipsum-cicero.txt"
fi

echo "--- walk, read only. tpth has no directory calls. ---"
echo "Default is / for 30 seconds. Pass a root and a second count to override."
echo "A live root can disagree between the two runs: each keeps its own 30 seconds."
cxx "$REAL/walk.cpp" /tmp/walk_cpp
rsc "$REAL/walk.rs" /tmp/walk_rs
if [ $# -ge 1 ]; then
  /usr/bin/time -f 'cpp  %e' /tmp/walk_cpp "$@"
  /usr/bin/time -f 'rs   %e' /tmp/walk_rs "$@"
else
  /usr/bin/time -f 'cpp  %e' /tmp/walk_cpp / 30
  /usr/bin/time -f 'rs   %e' /tmp/walk_rs / 30
fi
