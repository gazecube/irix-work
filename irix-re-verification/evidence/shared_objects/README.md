# IRIX shared-object evidence corpus

This directory records the provenance of the primary IRIX shared-object corpus used for reverse engineering.

## Canonical archive

The archive must be preserved byte-for-byte. Do not recompress, repack, normalize paths, or otherwise rewrite it.

Archive fingerprint:

```
filename: shared_objects.tar
size: 157065061 bytes
sha256: 7e1ef829d11bb06b5ae504f80517721ef1885990a25800312a901b9b3141a98c
tar entries: 1280
```

The archive was supplied as a primary evidence artifact on 2026-09-04. It contains shared objects extracted from the IRIX installation sets and is intended to be treated as an immutable reversing corpus.

## Split archive storage

Because the canonical archive exceeds GitHub's normal 100 MiB Git-object limit, it was split byte-for-byte into two ordinary Git-sized chunks:

```
shared_objects.tar.part.0  78532531 bytes
shared_objects.tar.part.1  78532530 bytes
```

Recombine with:

```
cat shared_objects.tar.part.0 shared_objects.tar.part.1 > shared_objects.tar
```

The recombined archive was SHA-256 verified against the original before the split was accepted. See `SPLIT_MANIFEST.txt` for the per-chunk hashes and verification record.

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

Use `SHA256SUMS` in this directory to verify extracted high-value files before basing reconstruction claims on them.
