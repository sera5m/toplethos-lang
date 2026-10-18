GTK IDE sources (`toplethos_ide.py`, `latex_view.py`, `graph_preview.py`, `companion.py`, `toplethos_latex.py`) ship from the same desktop tree used while developing with the watch.

On a machine that already has that folder:

```
cp -a /path/to/toplethos_ide/*.py .
python3 toplethos_ide.py examples/sig_gen.vul
```

`toplethos_run.py` in this repo is enough to embed Toplethos in other desktop projects.
