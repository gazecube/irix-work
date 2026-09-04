# Building the reconstructed fossils

This directory now contains intentionally minimal buildable shared-library targets.

```sh
cmake -S . -B build
cmake --build build
```

The first dependency slice is:

```text
libimdUtil.so
    |
    +-- libimdModel.so
```

At this stage the shared objects are scaffolds, not ABI-compatible replacements. Their purpose is to establish a buildable source tree using only recovered library and source-unit evidence. APIs, inheritance, data members, and behavior are added only as binary evidence supports them.

A successful build should produce `libimdUtil.so` and `libimdModel.so` (plus normal CMake SONAME symlinks on platforms that create them).
