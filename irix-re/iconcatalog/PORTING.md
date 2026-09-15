# iconcatalog port

Original binary: /usr/sbin/iconcatalog (IRIX 6.5, MIPS N32, stripped)

This directory is the source-recovery target for the SGI Icon Catalog application.

Binary-backed entrypoint facts:
- `main` exported at 0x10013088.
- `CatalogApp` constructor exported at 0x1000c02c.
- `main` performs locale/resource setup, constructs the argument list, allocates 0xb0 bytes for `CatalogApp`, constructs it, then enters the ViewKit application run loop.
- Dynamic class surface includes `CatalogApp`, `CatalogWindow`, `CatalogMenuHandler`, `CatalogMenuCmd`, `CatalogPageData`, `CatalogPageDialog`, `CatalogRefFilter`, `CatalogRefTarget`, and `CatalogOldRefTarget`.
- Original state/resources include `IconPositions`, `IconMappings`, `IconScale`, `IconPanel`, page naming, quick help text, and `/usr/lib/desktop/iconbooktocatalog` integration.

Direct dependencies include libimdUtil, libimdXutil, libimdApp, libimdModel, libimdIconPanel, libimdFtrDB, libimdFtrIcon, libvk, libSgm, libXm, libXext, libXt and libX11.

Port rule: reuse the reconstructed imd/model/icon-panel stack, but recover IconCatalog's own page/window/menu/DnD classes from this binary rather than recreating UI behavior from screenshots.

Next binary pass: decompile `CatalogApp`, `CatalogWindow`, page-data management, menu handler/commands and RefTarget/Filter classes as source units.
