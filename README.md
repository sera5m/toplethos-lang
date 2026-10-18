# toplethos-lang

Desktop Toplethos: language runner + GTK IDE. **Not** the ESP32 watch firmware OR the ai both of which are tangentally related and dependant on this

Watch OS **requires** this repo: https://github.com/sera5m/OIRIA_OS_espIDF (`third_party/toplethos-lang` submodule). Firmware VM is `os_code/core/rs_vm` in that tree — same language, C on device.
Package index: https://github.com/sera5m/oiria-index

## Pieces

| file | what |
|------|------|
| `toplethos_run.py` | interpreter (LUT sin/cos, fns, `@latex_internal`) |
| `toplethos_latex.py` | LaTeX body → Toplethos |
| `toplethos_ide.py` | GTK4 editor, graphs, companion |
| `graph_preview.py` | `@sig_gen_graph_preview` plots |
| `companion.py` | capability-limited offload host |
| `examples/` | `.vul` samples |

`toplethos_run.py` is plain Python. Use it in other projects with no GTK.

```bash
python3 toplethos_run.py examples/sin_demo.vul
python3 -c "from toplethos_run import run_source; print(run_source('print(sin(90));'))"
```

## Linux

```bash
sudo pacman -S python python-gobject gtk4 python-pyserial python-matplotlib
python3 toplethos_ide.py examples/sig_gen.vul
```
(only tested on arch linux/wayland (endeavoros titan)
## Windows

Interpreter:

```bat
py -3 toplethos_run.py examples\sin_demo.vul
```

IDE: MSYS2 UCRT64 GTK4 + PyGObject. GDK uses Win32 (untested)

## Tags

- `@latex_internal` — body is LaTeX, rewritten before run
- `@sig_gen_graph_preview` — Debugger / Graphs
