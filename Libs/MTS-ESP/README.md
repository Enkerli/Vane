# MTS-ESP (libMTS)

This directory should contain the ODDSound MTS-ESP client library.

```bash
git clone https://github.com/ODDSound/MTS-ESP .
```

CMake detects `libMTS.h` here automatically. Without it, Vane compiles
with equal temperament fallback and `TuningClient::hasMaster()` returns false.
