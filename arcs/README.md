# ARCS-UEFI

Native x86-64 UEFI port of the SGI ARCS/PROM standalone environment.

The goal is to port original SGI standalone/ARCS source where provenance is SGI-clean, preserving original filenames, symbols, structures, control flow, UI layout, assets, and behavior wherever practical. Reconstructed code is reserved for platform glue and places where the original implementation cannot be reused.

## Porting rule

Port first. Reconstruct only where necessary.

Do not copy, adapt, transliterate, or use AT&T/USL-origin implementation as a basis. Provenance-sensitive files must be reviewed before reuse.

## Layout

- `sgisrc/` - imported SGI standalone source selected for the port
- `uefi/` - x86-64/UEFI hardware and firmware backends
- `docs/` - provenance, source mapping, and port notes

## First milestone

Run the original SGI standalone GUI path over a UEFI GOP backend in QEMU:

1. original `drawBackground()`
2. original calligraphy/logo renderer
3. original fonts and GUI widgets
4. UEFI GOP implementation of the `htp_fncs` graphics primitives
5. UEFI keyboard/input shim

The resulting firmware UI should identify itself simply as `ARCS`.
