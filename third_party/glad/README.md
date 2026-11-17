# GLAD (OpenGL 3.3 Core)

This directory contains a **GLAD 2.0.8** loader for `gl:core=3.3` with **no extensions**, generated reproducibly for Xora2D.

## Regenerate

```bash
: > /tmp/glad-no-extensions.txt
python3 -m pip install 'glad2==2.0.8'
python3 -m glad --api 'gl:core=3.3' --extensions /tmp/glad-no-extensions.txt \
  --out-path third_party/glad --reproducible c
```

Use only GLAD 2.x (`glad2` on PyPI). Do not hand-edit `include/glad/gl.h` or `src/gl.c`.