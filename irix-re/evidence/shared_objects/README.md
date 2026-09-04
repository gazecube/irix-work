# IRIX shared-object evidence corpus

This directory records the provenance of the primary IRIX shared-object corpus used for reverse engineering.

## Canonical archive

Expected repository/LFS path:

```
irix-re/evidence/shared_objects/shared_objects.tar
```

The archive must be preserved byte-for-byte. Do not recompress, repack, normalize paths, or otherwise rewrite it.

Archive fingerprint:

```
filename: shared_objects.tar
size: 157286400 bytes (150 MiB)
sha256: 7e1ef829d11bb06b5ae504f80517721ef1885990a25800312a901b9b3141a98c
tar entries: 1280
```

The archive was supplied as a primary evidence artifact on 2026-09-04. It contains shared objects extracted from the IRIX installation sets and is intended to be treated as an immutable reversing corpus.

## Important N32 objects in the corpus

```
usr/lib32/internal/libimdIconPanel.so
usr/lib32/internal/libimdModel.so
usr/lib32/internal/libimdUtil.so
usr/lib32/internal/libimdXutil.so
usr/lib32/internal/libimdFtrIcon.so
usr/lib32/internal/libimdFtrDB.so
usr/lib32/internal/libimdFtrGallery.so
usr/lib32/internal/libimdApp.so
usr/lib32/libvk.so.1
usr/lib32/libSgm.so.1
usr/lib32/libXm.so.1
```

Use `SHA256SUMS` in this directory to verify extracted files before basing reconstruction claims on them.

## GitHub storage note

The canonical tar is 150 MiB, above GitHub's normal 100 MiB Git-object limit. Store the exact archive with Git LFS at the path above. The SHA-256 fingerprint in this file is authoritative for verifying that the LFS object is the original evidence archive.
