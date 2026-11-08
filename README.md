# toplethos-lang

Language spec **1.0** is [USE.md](USE.md). That file wins. [MATRIX.md](MATRIX.md) is the older decision log. The compiler is `compiler/tpcc`. One program interprets the bytecode, compiles it, and JIT-compiles an `@inlined` function from that same bytecode while the interpreter is still running.

```bash
make -C compiler tpcc
compiler/tpcc --run file.tpth_s
compiler/tpcc --asm out.s file.tpth_s
compiler/tpcc --jit file.tpth_s
```

`python3 conformance/check.py` is the 1.0 suite. A green run is release **1.2 tripar**. This tree is that release. What the name covers, and what it does not, is [notes/TRIPAR.md](notes/TRIPAR.md).

The Horner battery is `sh aot/battery.sh`. The mixed-ops checksum is `sh aot/mix.sh`. The other cross-language rows are `sh aot/tasks.sh`. A 1.0 Toplethos program in those scripts is compiled with `tpcc`. The conformance suite is the release gate; those batteries are not.

This repo is the language. It is not the ESP32 watch firmware and not the IDE. The watch links it from [OIRIA_OS_espIDF](https://github.com/sera5m/OIRIA_OS_espIDF). The desktop editor is [toplethos-ide](https://github.com/sera5m/toplethos-ide).

v1.1 work that is not built is [notes/FIXES_NEEDED.md](notes/FIXES_NEEDED.md). Later goals are [notes/GOALS.md](notes/GOALS.md).
