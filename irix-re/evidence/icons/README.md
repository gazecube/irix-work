# IRIX icon/resource evidence corpus

This directory records provenance for the icon/resource corpus used by the IRIX reverse-engineering work.

Canonical archive path:

```
irix-re/evidence/icons.tar
```

Archive fingerprint:

```
filename: icons.tar
size: 3100544 bytes
sha256: 4e6a91ec6cc9d469bdf2f5e407f253c380bc74a6e0f01370e6b98adf5c49808b
entries: 1135
.icon files: 118
.rgb files: 4
```

The archive was supplied on 2026-09-04 and was built by extracting everything from the complete IRIX install sets (apps, f1, f2) whose file, directory, or extension contained the string `icon`.

Preserve the tar byte-for-byte. Do not repack, recompress, rename internal paths, or normalize timestamps before using it as reversing evidence.

Notable contents include authentic SGI image-format `.icon` assets and icon-related runtime/tooling material such as `libimdFtrIcon.so`, `libimdIconPanel.so`, `libdesktopFileicon.so`, `makeIconVisuals`, `iconsmith`, `iconcatalog`, and related resources.
