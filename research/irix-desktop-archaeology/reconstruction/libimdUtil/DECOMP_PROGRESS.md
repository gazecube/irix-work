# libimdUtil.so decompilation progress

Canonical original-binary target: **252 defined functions**.

Current reconstructed functions compiled into the Linux scaffold: **52 / 252 (20.63%)**.

- `MemoryMap.c++`: 2 portable reconstructed functions (`imdMkdirP`, `imdPathcanon`).
- `RecoveredBatch001.c++`: 50 additional functions reconstructed from MIPS N32 machine code.

`RecoveredBatch001.c++` deliberately models observed 32-bit object slots for methods whose complete C++ class declarations have not yet been recovered. This avoids inventing modern layouts. Calls into functions not yet decompiled remain external references until their original bodies are recovered in later batches.

A local GCC 14.2.0 CMake build completed successfully with all 52 reconstructed function symbols present in `libimdUtil.so`.

## Batch 001

The batch includes small accessors, collection/list operations, handle reference operations, dictionary forwarding methods, string comparison forwarding methods, constructors with directly observed stores, and iterator wrappers. Each function is annotated with its original MIPS address and size.

Next work should replace external references with reconstructed callees, then split transitional batch code back into the recovered original source-unit boundaries as DWARF evidence permits.
