# libimdUtil.so reconstruction

`libimdUtil.so` is one of the foundational libraries in the recovered later IMD desktop stack.

The current target is deliberately minimal: it builds a real shared object named `libimdUtil.so` while making no unsupported ABI claims. The placeholder `c++init.cxx` filename is recovered from DWARF `.debug_line` evidence.

Recovered source units include `c++init.cxx`, `Attribute.c++`, `Collections.c++`, `DeleterFor.c++`, `DictionaryOf.c++`, `FileUtil.c++`, `HandleTo.c++`, `Hashable.c++`, `InitFile.c++`, `MemoryMap.c++`, `SessionDir.c++`, `ConvertPath.c++`, `String.c++`, `IsDesktopOn.c++`, and `i18n.c++`.

Implementation files should only be added when their existence is supported by recovered source-unit data. Public declarations should only be added when signatures can be supported by symbol/debug/disassembly evidence.
