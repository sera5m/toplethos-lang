#!/bin/sh
# Cicero word table. The inlined binary pastes mix into the scan loop.
# The call binary leaves that call. Checksums must match.
cd "$(dirname "$0")/../.." || exit 1
REAL="aot/real"
make -C compiler tpcc
g++ -O2 -std=c++20 -o /tmp/words_cpp "$REAL/words.cpp" || exit 1
rustc -C opt-level=3 -o /tmp/words_rs "$REAL/words.rs" || exit 1
javac -d /tmp "$REAL/words.java" || exit 1
compiler/tpcc --asm /tmp/words_tp.s "$REAL/words.tpth_s" || exit 1
gcc -o /tmp/words_tp /tmp/words_tp.s || exit 1
compiler/tpcc --asm /tmp/words_call.s "$REAL/words_call.tpth_s" || exit 1
gcc -o /tmp/words_call /tmp/words_call.s || exit 1
echo "call mix in pasted asm: $(grep -c 'call mix' /tmp/words_tp.s || true)"
echo "call mix in call asm: $(grep -c 'call mix' /tmp/words_call.s || true)"
if [ -x /usr/bin/time ]; then
  /usr/bin/time -f 'cpp   %e' /tmp/words_cpp
  /usr/bin/time -f 'rs    %e' /tmp/words_rs
  /usr/bin/time -f 'java  %e' java -cp /tmp words
  /usr/bin/time -f 'tp    %e  pasted' /tmp/words_tp
  /usr/bin/time -f 'tp    %e  call' /tmp/words_call
else
  python3 - /tmp/words_cpp /tmp/words_rs /tmp/words_tp /tmp/words_call << 'PY'
import subprocess, sys, time
labels = ["cpp ", "rs  ", "tp pasted", "tp call  "]
for label, cmd in zip(labels, sys.argv[1:]):
    t = time.perf_counter()
    r = subprocess.run([cmd], capture_output=True, text=True)
    dt = time.perf_counter() - t
    print(f"{label} {dt:.4f}  {r.stdout.strip()}")
    if r.returncode:
        sys.exit(r.returncode)
t = time.perf_counter()
r = subprocess.run(["java", "-cp", "/tmp", "words"], capture_output=True, text=True)
dt = time.perf_counter() - t
print(f"java {dt:.4f}  {r.stdout.strip()}")
if r.returncode:
    sys.exit(r.returncode)
PY
fi
echo "--- java, warmed 50-pass kernel, best of 7 ---"
java -cp /tmp words --kernel
