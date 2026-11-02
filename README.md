# toplethos-lang

Language spec **1.0** is [USE.md](USE.md). That file wins. [MATRIX.md](MATRIX.md) is the older decision log. The tag is `v1.0`. The older `halloween` tag is not 1.0.

`toplethos_run.py` and the watch VM are an older runner. They do not implement 1.0. Programs named `examples/*.vul` are runner programs.

`examples/glitch.tpth_s` is a 1.0 program. It opens `examples/street.bmp` and paints rings on that night photo. C++ and Rust open the same file.

```bash
sh examples/glitch.sh
```

This repo is the language. It is not the ESP32 watch firmware and not the IDE. The watch links it from [OIRIA_OS_espIDF](https://github.com/sera5m/OIRIA_OS_espIDF). The desktop editor is [toplethos-ide](https://github.com/sera5m/toplethos-ide).

```bash
python3 toplethos_run.py examples/sin_demo.vul
```

That command runs the old runner, not a 1.0 program.
