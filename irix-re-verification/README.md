# IRIX Desktop source tree reconstruction

This tree is reconstructed from extracted IRIX desktop binaries, primarily DWARF `.debug_line`, `.debug_pubnames`, dynamic symbols, and dependency data.

Rules for this reconstruction:

- Preserve recovered source filenames and recovered directory boundaries exactly when known.
- Do not invent source files merely because a class exists.
- Do not invent C++ signatures, inheritance, member layout, or ABI details until supported by binary evidence.
- Keep observed facts separate from inferred structure.
- Treat `/xlv55/kudzu-*` paths as original build-tree evidence, not target installation paths.

The first reconstruction pass focuses on the later `userenv/desktop` IMD generation. The older `userenv/ozdesktop` family remains in the archaeology data for lineage comparison.

## Recovered library roots

- `libimdModel.so` - 37 recovered `.debug_line` entries
- `libimdIconPanel.so` - 81 recovered `.debug_line` entries
- `libimdUtil.so` - 20 recovered `.debug_line` entries
- `libimdXutil.so` - 72 recovered `.debug_line` entries
- `libimdFtrDB.so` - 13 recovered `.debug_line` entries
- `libimdFtrIcon.so` - 22 recovered `.debug_line` entries
- `libimdFtrGallery.so` - 19 recovered `.debug_line` entries
- `libimdApp.so` - 25 recovered `.debug_line` entries
- `libImdHostModel.so` - 3 recovered `.debug_line` entries
- `libImdURLModel.so` - 10 recovered `.debug_line` entries
