# toplethos-lang

Language spec **1.0** is [USE.md](USE.md). That file wins. [MATRIX.md](MATRIX.md) is the older decision log. The tag is `v1.0`. The older `halloween` tag is not 1.0.

`examples/glitch.tpth_s` is a 1.0 program. It opens `examples/street.bmp` and paints rings on that night photo. C++ and Rust open the same file.

```bash
sh examples/glitch.sh
python3 aot/tpc.py file.tpth_s -o binary
```

The Horner battery is `sh aot/battery.sh`. The other rows are `sh aot/tasks.sh`.

This repo is the language. It is not the ESP32 watch firmware and not the IDE. The watch links it from [OIRIA_OS_espIDF](https://github.com/sera5m/OIRIA_OS_espIDF). The desktop editor is [toplethos-ide](https://github.com/sera5m/toplethos-ide).
