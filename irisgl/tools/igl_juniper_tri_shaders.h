#ifndef IRISGL_IGL_JUNIPER_TRI_SHADERS_H
#define IRISGL_IGL_JUNIPER_TRI_SHADERS_H

#include <stdint.h>

/*
 * Standalone Juniper triangle bring-up shaders.
 *
 * These dwords were generated offline from the original Evergreen assembler
 * in Mesa commit 50526e094f4c66957c7f74c190c35903bc82fb62 using
 * CHIP_JUNIPER. They are embedded here so the probe has no Mesa runtime
 * dependency.
 *
 * VS: fetch one float4 position from vertex buffer 0, export POS0, and emit
 *     the parameter export expected by the 2010 Evergreen path.
 * PS: move four literal floats into GPR0 and export MRT0.
 *     RGB = #0f5c7f, alpha = 1.0.
 */
static const uint32_t igl_juniper_tri_vs[] = {
    0x00000004u, 0x80800000u, 0xc000203cu, 0x95000688u,
    0xc0004000u, 0x15200688u, 0x00000000u, 0x00000000u,
    0x40000000u, 0x002d1000u, 0x00080000u, 0x00000000u
};

static const uint32_t igl_juniper_tri_ps[] = {
    0x00000002u, 0xa0140000u, 0xc0000000u, 0x95200688u,
    0x000000fdu, 0x00000c90u, 0x000004fdu, 0x20000c90u,
    0x000008fdu, 0x40000c90u, 0x80000cfdu, 0x60000c90u,
    0x3d70f0f1u, 0x3eb8b8b9u, 0x3efefeffu, 0x3f800000u
};

#define IGL_JUNIPER_TRI_VS_DW \
    (sizeof(igl_juniper_tri_vs) / sizeof(igl_juniper_tri_vs[0]))
#define IGL_JUNIPER_TRI_PS_DW \
    (sizeof(igl_juniper_tri_ps) / sizeof(igl_juniper_tri_ps[0]))

#endif /* IRISGL_IGL_JUNIPER_TRI_SHADERS_H */
