# Native Juniper triangle baseline

Known-good native 3D proof point for the Radeon HD 6770 / Juniper XT path.

Baseline commit:

    27effdacf0f5605f0b25fd77d751d4503c5c80b8

Observed result on real hardware:

    raw Radeon PM4 submission accepted
    Juniper VS executed
    Juniper PS executed
    triangle rasterized into the native color BO
    CPU readback verified the rendered pixels
    the same BO was exported through PRIME and presented through DRI3

The first proven triangle uses the fixed flat color #0f5c7f.

This commit is the rollback/reference point for future 3D work. Keep the original 2010-derived bring-up path in src/igl_juniper_3d.c as archaeology. The working path is src/igl_juniper_3d_main.c, which is cross-checked against current Mesa r600 Evergreen state emission and the Linux radeon Evergreen command-stream parser.

Important Radeon CS requirement proven during bring-up:

    flags[0] = RADEON_CS_KEEP_TILING_FLAGS

The mature legacy Radeon color-target relocation sequence is:

    CB_COLOR0_BASE
    CB_COLOR0_ATTRIB
    CB_COLOR0_CMASK
    CB_COLOR0_FMASK

Do not alter the known-good packet sequence speculatively. Generalize one externally visible input at a time and preserve this baseline as the comparison point.
