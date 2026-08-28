# VOLT

Fault-tolerant software-defined vehicle compute platform, built in C++23.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Available presets: `dev`, `release`, `rt`, `asan`, `ubsan`, `tsan`, `coverage`, `sim`.
