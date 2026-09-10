#include "igl_juniper.h"

#include "../tools/igl_juniper_tri_shaders.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <radeon_drm.h>

/*
 * Minimal native Juniper 3D bring-up path.
 *
 * Register addresses, packet ordering and fixed-function values are reduced
 * from the Evergreen code in Mesa commit
 * 50526e094f4c66957c7f74c190c35903bc82fb62. The shader payloads were emitted
 * offline by that same snapshot's Evergreen assembler for CHIP_JUNIPER.
 *
 * This is intentionally not part of libgl.a yet. It exists only to prove the
 * first native 3D rasterizer draw before any IRIS GL primitive API is wired to
 * it.
 */

#define IGL_TRI_IB_MAX_DW 512u
#define IGL_TRI_MAX_RELOCS 8u

#define IGL_PKT3(op, n) \
    ((3u << 30) | (((uint32_t)(op) & 0xffu) << 8) | \
     (((uint32_t)(n) & 0x3fffu) << 16))

#define IGL_PKT3_NOP 0x10u
#define IGL_PKT3_CONTEXT_CONTROL 0x28u
#define IGL_PKT3_DRAW_INDEX_AUTO 0x2du
#define IGL_PKT3_INDEX_TYPE 0x2au
#define IGL_PKT3_NUM_INSTANCES 0x2fu
#define IGL_PKT3_SURFACE_SYNC 0x43u
#define IGL_PKT3_SET_CONFIG_REG 0x68u
#define IGL_PKT3_SET_CONTEXT_REG 0x69u
#define IGL_PKT3_SET_RESOURCE 0x6du
#define IGL_PKT3_SET_CTL_CONST 0x6fu

#define IGL_CONFIG_REG_BASE 0x00008000u
#define IGL_CONTEXT_REG_BASE 0x00028000u

/* Evergreen config registers. */
#define IGL_EG_SQ_CONFIG 0x00008c00u
#define IGL_EG_SQ_GPR_RESOURCE_MGMT_1 0x00008c04u
#define IGL_EG_SQ_GPR_RESOURCE_MGMT_2 0x00008c08u
#define IGL_EG_SQ_GPR_RESOURCE_MGMT_3 0x00008c0cu
#define IGL_EG_SQ_THREAD_RESOURCE_MGMT_1 0x00008c18u
#define IGL_EG_SQ_THREAD_RESOURCE_MGMT_2 0x00008c1cu
#define IGL_EG_SQ_STACK_RESOURCE_MGMT_1 0x00008c20u
#define IGL_EG_SQ_STACK_RESOURCE_MGMT_2 0x00008c24u
#define IGL_EG_SQ_STACK_RESOURCE_MGMT_3 0x00008c28u
#define IGL_EG_SPI_CONFIG_CNTL 0x00009100u
#define IGL_EG_SPI_CONFIG_CNTL_1 0x0000913cu
#define IGL_EG_VGT_PRIMITIVE_TYPE 0x00008958u

/* Evergreen context registers used by the reduced triangle path. */
#define IGL_EG_PA_SC_SCREEN_SCISSOR_TL 0x00028030u
#define IGL_EG_PA_SC_SCREEN_SCISSOR_BR 0x00028034u
#define IGL_EG_PA_SC_WINDOW_OFFSET 0x00028200u
#define IGL_EG_PA_SC_WINDOW_SCISSOR_TL 0x00028204u
#define IGL_EG_PA_SC_WINDOW_SCISSOR_BR 0x00028208u
#define IGL_EG_CB_TARGET_MASK 0x00028238u
#define IGL_EG_CB_SHADER_MASK 0x0002823cu
#define IGL_EG_PA_SC_GENERIC_SCISSOR_TL 0x00028240u
#define IGL_EG_PA_SC_GENERIC_SCISSOR_BR 0x00028244u
#define IGL_EG_PA_SC_VPORT_SCISSOR_0_TL 0x00028250u
#define IGL_EG_PA_SC_VPORT_SCISSOR_0_BR 0x00028254u
#define IGL_EG_SPI_VS_OUT_ID_0 0x0002861cu
#define IGL_EG_SPI_VS_OUT_ID_1 0x00028620u
#define IGL_EG_SPI_VS_OUT_CONFIG 0x000286c4u
#define IGL_EG_SPI_PS_IN_CONTROL_0 0x000286ccu
#define IGL_EG_SPI_PS_IN_CONTROL_1 0x000286d0u
#define IGL_EG_SPI_BARYC_CNTL 0x000286e0u
#define IGL_EG_CB_BLEND0_CONTROL 0x00028780u
#define IGL_EG_DB_DEPTH_CONTROL 0x00028800u
#define IGL_EG_CB_COLOR_CONTROL 0x00028808u
#define IGL_EG_DB_SHADER_CONTROL 0x0002880cu
#define IGL_EG_PA_CL_CLIP_CNTL 0x00028810u
#define IGL_EG_PA_SU_SC_MODE_CNTL 0x00028814u
#define IGL_EG_PA_CL_VTE_CNTL 0x00028818u
#define IGL_EG_PA_CL_VS_OUT_CNTL 0x0002881cu
#define IGL_EG_PA_CL_NANINF_CNTL 0x00028820u
#define IGL_EG_SQ_PGM_START_PS 0x00028840u
#define IGL_EG_SQ_PGM_RESOURCES_PS 0x00028844u
#define IGL_EG_SQ_PGM_EXPORTS_PS 0x0002884cu
#define IGL_EG_SQ_PGM_START_VS 0x0002885cu
#define IGL_EG_SQ_PGM_RESOURCES_VS 0x00028860u
#define IGL_EG_PA_SU_POINT_SIZE 0x00028a00u
#define IGL_EG_PA_SU_POINT_MINMAX 0x00028a04u
#define IGL_EG_PA_SU_LINE_CNTL 0x00028a08u
#define IGL_EG_PA_SC_MODE_CNTL_0 0x00028a48u
#define IGL_EG_PA_SC_MODE_CNTL_1 0x00028a4cu
#define IGL_EG_VGT_PRIMITIVEID_EN 0x00028a84u
#define IGL_EG_VGT_INSTANCE_STEP_RATE_0 0x00028aa0u
#define IGL_EG_VGT_INSTANCE_STEP_RATE_1 0x00028aa4u
#define IGL_EG_VGT_REUSE_OFF 0x00028ab4u
#define IGL_EG_VGT_VTX_CNT_EN 0x00028ab8u
#define IGL_EG_VGT_SHADER_STAGES_EN 0x00028b54u
#define IGL_EG_VGT_STRMOUT_CONFIG 0x00028b94u
#define IGL_EG_VGT_STRMOUT_BUFFER_CONFIG 0x00028b98u
#define IGL_EG_PA_SC_LINE_CNTL 0x00028c00u
#define IGL_EG_PA_SU_VTX_CNTL 0x00028c08u
#define IGL_EG_PA_CL_GB_VERT_CLIP_ADJ 0x00028c0cu
#define IGL_EG_PA_CL_GB_VERT_DISC_ADJ 0x00028c10u
#define IGL_EG_PA_CL_GB_HORZ_CLIP_ADJ 0x00028c14u
#define IGL_EG_PA_CL_GB_HORZ_DISC_ADJ 0x00028c18u
#define IGL_EG_CB_COLOR0_BASE 0x00028c60u
#define IGL_EG_CB_COLOR0_PITCH 0x00028c64u
#define IGL_EG_CB_COLOR0_SLICE 0x00028c68u
#define IGL_EG_CB_COLOR0_VIEW 0x00028c6cu
#define IGL_EG_CB_COLOR0_INFO 0x00028c70u
#define IGL_EG_CB_COLOR0_ATTRIB 0x00028c74u
#define IGL_EG_CB_COLOR0_DIM 0x00028c78u
#define IGL_EG_VGT_MAX_VTX_INDX 0x00028400u
#define IGL_EG_VGT_MIN_VTX_INDX 0x00028404u
#define IGL_EG_VGT_INDX_OFFSET 0x00028408u

/* Radeon/Evergreen cache actions used by Mesa's r700SyncSurf(). */
#define IGL_EG_VC_ACTION_ENA (1u << 24)
#define IGL_EG_CB_ACTION_ENA (1u << 25)
#define IGL_EG_SH_ACTION_ENA (1u << 27)
#define IGL_EG_CB0_DEST_BASE_ENA (1u << 6)

#define IGL_EG_WINDOW_OFFSET_DISABLE (1u << 31)
#define IGL_EG_VTX_XY_FMT (1u << 8)
#define IGL_EG_CLIP_DISABLE (1u << 16)

#define IGL_EG_DI_PT_TRILIST 0x04u
#define IGL_EG_DI_SRC_SEL_AUTO_INDEX 0x02u

#define IGL_EG_FETCH_RESOURCE_VS_OFFSET 0x000000b0u
#define IGL_EG_FETCH_RESOURCE_STRIDE 8u

struct igl_tri_cs {
    uint32_t ib[IGL_TRI_IB_MAX_DW];
    size_t cdw;
    struct drm_radeon_cs_reloc reloc[IGL_TRI_MAX_RELOCS];
    size_t nreloc;
};

struct igl_tri_vertex {
    float x;
    float y;
    float z;
    float w;
};

static int igl_tri_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret == 0;
}

static int igl_tri_emit(struct igl_tri_cs *cs, uint32_t dw)
{
    if (!cs || cs->cdw >= IGL_TRI_IB_MAX_DW)
        return 0;
    cs->ib[cs->cdw++] = dw;
    return 1;
}

static int igl_tri_emit_reloc(struct igl_tri_cs *cs,
                              const struct igl_juniper_bo *bo,
                              uint32_t read_domains,
                              uint32_t write_domain)
{
    size_t i;

    if (!cs || !bo || !bo->handle)
        return 0;

    for (i = 0; i < cs->nreloc; i++) {
        if (cs->reloc[i].handle == bo->handle) {
            if (cs->reloc[i].read_domains != read_domains ||
                cs->reloc[i].write_domain != write_domain)
                return 0;
            return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_NOP, 0)) &&
                   igl_tri_emit(cs, (uint32_t)(i * 4u));
        }
    }

    if (cs->nreloc >= IGL_TRI_MAX_RELOCS)
        return 0;

    memset(&cs->reloc[cs->nreloc], 0, sizeof(cs->reloc[cs->nreloc]));
    cs->reloc[cs->nreloc].handle = bo->handle;
    cs->reloc[cs->nreloc].read_domains = read_domains;
    cs->reloc[cs->nreloc].write_domain = write_domain;
    i = cs->nreloc++;

    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_NOP, 0)) &&
           igl_tri_emit(cs, (uint32_t)(i * 4u));
}

static int igl_tri_set_config_reg(struct igl_tri_cs *cs,
                                  uint32_t reg,
                                  uint32_t value)
{
    if (reg < IGL_CONFIG_REG_BASE)
        return 0;
    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SET_CONFIG_REG, 1)) &&
           igl_tri_emit(cs, (reg - IGL_CONFIG_REG_BASE) >> 2) &&
           igl_tri_emit(cs, value);
}

static int igl_tri_set_context_reg(struct igl_tri_cs *cs,
                                   uint32_t reg,
                                   uint32_t value)
{
    if (reg < IGL_CONTEXT_REG_BASE)
        return 0;
    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SET_CONTEXT_REG, 1)) &&
           igl_tri_emit(cs, (reg - IGL_CONTEXT_REG_BASE) >> 2) &&
           igl_tri_emit(cs, value);
}

static int igl_tri_set_context_reg_reloc(struct igl_tri_cs *cs,
                                         uint32_t reg,
                                         uint32_t value,
                                         const struct igl_juniper_bo *bo,
                                         uint32_t read_domains,
                                         uint32_t write_domain)
{
    return igl_tri_set_context_reg(cs, reg, value) &&
           igl_tri_emit_reloc(cs, bo, read_domains, write_domain);
}

static int igl_tri_surface_sync(struct igl_tri_cs *cs,
                                const struct igl_juniper_bo *bo,
                                uint32_t action,
                                uint32_t read_domains,
                                uint32_t write_domain)
{
    uint64_t blocks;

    blocks = (bo->size + 255u) >> 8;
    if (blocks > 0xffffffffu)
        blocks = 0xffffffffu;

    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SURFACE_SYNC, 3)) &&
           igl_tri_emit(cs, action) &&
           igl_tri_emit(cs, (uint32_t)blocks) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, 10) &&
           igl_tri_emit_reloc(cs, bo, read_domains, write_domain);
}

static int igl_tri_set_vertex_resource(struct igl_tri_cs *cs,
                                       const struct igl_juniper_bo *vb)
{
    uint32_t word2;
    uint32_t word3;
    uint32_t word7;

    /*
     * Mesa 50526e Evergreen VTX resource for one float4 stream:
     * stride 16, FMT_32_32_32_32_FLOAT, SCALED, xyzw swizzle, valid buffer.
     */
    word2 = (16u << 8) | (35u << 20) | (2u << 26);
    word3 = (0u << 3) | (1u << 6) | (2u << 9) | (3u << 12);
    word7 = 3u << 30;

    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SET_RESOURCE, 8)) &&
           igl_tri_emit(cs, IGL_EG_FETCH_RESOURCE_VS_OFFSET *
                            IGL_EG_FETCH_RESOURCE_STRIDE) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, (uint32_t)vb->size) &&
           igl_tri_emit(cs, word2) &&
           igl_tri_emit(cs, word3) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, word7) &&
           igl_tri_emit_reloc(cs, vb, RADEON_GEM_DOMAIN_GTT, 0);
}

static int igl_tri_submit(struct igl_juniper_device *dev,
                          struct igl_tri_cs *builder)
{
    struct drm_radeon_cs_chunk chunks[3];
    struct drm_radeon_cs submit;
    uint64_t chunk_ptrs[3];
    uint32_t flags[3];

    flags[0] = 0;
    flags[1] = RADEON_CS_RING_GFX;
    flags[2] = 0;

    memset(chunks, 0, sizeof(chunks));
    chunks[0].chunk_id = RADEON_CHUNK_ID_IB;
    chunks[0].length_dw = (uint32_t)builder->cdw;
    chunks[0].chunk_data = (uint64_t)(uintptr_t)builder->ib;
    chunks[1].chunk_id = RADEON_CHUNK_ID_RELOCS;
    chunks[1].length_dw = (uint32_t)(builder->nreloc *
                           sizeof(struct drm_radeon_cs_reloc) /
                           sizeof(uint32_t));
    chunks[1].chunk_data = (uint64_t)(uintptr_t)builder->reloc;
    chunks[2].chunk_id = RADEON_CHUNK_ID_FLAGS;
    chunks[2].length_dw = 3;
    chunks[2].chunk_data = (uint64_t)(uintptr_t)flags;

    chunk_ptrs[0] = (uint64_t)(uintptr_t)&chunks[0];
    chunk_ptrs[1] = (uint64_t)(uintptr_t)&chunks[1];
    chunk_ptrs[2] = (uint64_t)(uintptr_t)&chunks[2];

    memset(&submit, 0, sizeof(submit));
    submit.num_chunks = 3;
    submit.chunks = (uint64_t)(uintptr_t)chunk_ptrs;
    return igl_tri_ioctl(dev->fd, DRM_IOCTL_RADEON_CS, &submit);
}

int igl_juniper_draw_test_triangle(struct igl_juniper_device *dev,
                                   struct igl_juniper_bo *color,
                                   int width,
                                   int height,
                                   int stride)
{
    struct igl_juniper_bo vs;
    struct igl_juniper_bo ps;
    struct igl_juniper_bo vb;
    struct igl_tri_vertex vertices[3];
    struct igl_tri_cs cs;
    struct drm_radeon_gem_set_domain domain;
    uint32_t br;
    uint32_t pitch_pixels;
    uint32_t pitch_tile_max;
    uint32_t slice_tile_max;
    int ok;

    if (!dev || !color || dev->fd < 0 || !color->handle ||
        width <= 0 || height <= 0 || stride <= 0 || (stride & 255) != 0 ||
        (stride & 3) != 0 || color->size < (uint64_t)stride * (uint64_t)height)
        return 0;

    pitch_pixels = (uint32_t)stride / 4u;
    if ((pitch_pixels & 7u) != 0 ||
        ((uint64_t)pitch_pixels * (uint64_t)height & 63u) != 0)
        return 0;
    pitch_tile_max = pitch_pixels / 8u - 1u;
    slice_tile_max = (uint32_t)(((uint64_t)pitch_pixels *
                                 (uint64_t)height) / 64u - 1u);

    memset(&vs, 0, sizeof(vs));
    memset(&ps, 0, sizeof(ps));
    memset(&vb, 0, sizeof(vb));
    memset(&cs, 0, sizeof(cs));

    if (!igl_juniper_bo_create(dev, &vs, 4096) ||
        !igl_juniper_bo_create(dev, &ps, 4096) ||
        !igl_juniper_bo_create(dev, &vb, 4096))
        goto fail;

    memcpy(vs.map, igl_juniper_tri_vs, sizeof(igl_juniper_tri_vs));
    memcpy(ps.map, igl_juniper_tri_ps, sizeof(igl_juniper_tri_ps));

    /* Screen-space triangle; PA_CL_VTE_CNTL uses VTX_XY_FMT. */
    vertices[0].x = (float)width * 0.25f;
    vertices[0].y = (float)height * 0.75f;
    vertices[0].z = 0.0f;
    vertices[0].w = 1.0f;
    vertices[1].x = (float)width * 0.50f;
    vertices[1].y = (float)height * 0.25f;
    vertices[1].z = 0.0f;
    vertices[1].w = 1.0f;
    vertices[2].x = (float)width * 0.75f;
    vertices[2].y = (float)height * 0.75f;
    vertices[2].z = 0.0f;
    vertices[2].w = 1.0f;
    memcpy(vb.map, vertices, sizeof(vertices));

    /* Start a fresh Evergreen 3D context, as the 2010 classic driver does. */
    ok = igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_CONTEXT_CONTROL, 1)) &&
         igl_tri_emit(&cs, 0x80000000u) &&
         igl_tri_emit(&cs, 0x80000000u);

    /* Exact Juniper resource partition values from Mesa 50526e. */
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SPI_CONFIG_CNTL, 0x00000000u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SPI_CONFIG_CNTL_1, 0x00000004u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_CONFIG, 0xe4000003u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_GPR_RESOURCE_MGMT_1, 0x402e005du);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_GPR_RESOURCE_MGMT_2, 0x001f001fu);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_GPR_RESOURCE_MGMT_3, 0x00170017u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_THREAD_RESOURCE_MGMT_1, 0x14141480u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_THREAD_RESOURCE_MGMT_2, 0x00001414u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_STACK_RESOURCE_MGMT_1, 0x00550055u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_STACK_RESOURCE_MGMT_2, 0x00550055u);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_SQ_STACK_RESOURCE_MGMT_3, 0x00550055u);

    /* Make CPU-uploaded shader and vertex BOs visible to SQ/VC. */
    ok = ok && igl_tri_surface_sync(&cs, &vs, IGL_EG_SH_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_surface_sync(&cs, &ps, IGL_EG_SH_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_surface_sync(&cs, &vb, IGL_EG_VC_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);

    /* Vertex and pixel shader program state. */
    ok = ok && igl_tri_set_context_reg_reloc(&cs, IGL_EG_SQ_PGM_START_VS,
                                              0, &vs,
                                              RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SQ_PGM_RESOURCES_VS,
                                       0x00000004u);
    ok = ok && igl_tri_set_context_reg_reloc(&cs, IGL_EG_SQ_PGM_START_PS,
                                              0, &ps,
                                              RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SQ_PGM_RESOURCES_PS,
                                       0x00800001u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SQ_PGM_EXPORTS_PS,
                                       0x00000002u);

    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_VS_OUT_ID_0,
                                       0x03020100u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_VS_OUT_ID_1,
                                       0x07060504u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_VS_OUT_CONFIG, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_PS_IN_CONTROL_0,
                                       0x10000000u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_PS_IN_CONTROL_1, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_SPI_BARYC_CNTL, 0x00100010u);

    /* Base vertex and instance are both zero. */
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_SET_CTL_CONST, 1)) &&
         igl_tri_emit(&cs, 0) && igl_tri_emit(&cs, 0);
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_SET_CTL_CONST, 1)) &&
         igl_tri_emit(&cs, 1) && igl_tri_emit(&cs, 0);
    ok = ok && igl_tri_set_vertex_resource(&cs, &vb);

    /* Color buffer: linear 8:8:8:8, alternate component swap, UNORM. */
    ok = ok && igl_tri_surface_sync(&cs, color,
                                    IGL_EG_CB_ACTION_ENA |
                                    IGL_EG_CB0_DEST_BASE_ENA,
                                    0, RADEON_GEM_DOMAIN_GTT);
    ok = ok && igl_tri_set_context_reg_reloc(&cs, IGL_EG_CB_COLOR0_BASE,
                                              0, color, 0,
                                              RADEON_GEM_DOMAIN_GTT);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR0_PITCH,
                                       pitch_tile_max);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR0_SLICE,
                                       slice_tile_max);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR0_VIEW, 0);
    ok = ok && igl_tri_set_context_reg_reloc(&cs, IGL_EG_CB_COLOR0_INFO,
                                              0x01088068u, color, 0,
                                              RADEON_GEM_DOMAIN_GTT);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR0_ATTRIB,
                                       0x00000010u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR0_DIM, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_TARGET_MASK, 0x0000000fu);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_SHADER_MASK, 0x0000000fu);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_BLEND0_CONTROL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR_CONTROL,
                                       0x00cc0010u);

    /* No depth, no clipping, screen-space XY. */
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_DB_DEPTH_CONTROL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_DB_SHADER_CONTROL,
                                       0x00000210u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_CLIP_CNTL,
                                       IGL_EG_CLIP_DISABLE);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SU_SC_MODE_CNTL,
                                       0x00000240u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_VTE_CNTL,
                                       IGL_EG_VTX_XY_FMT);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_VS_OUT_CNTL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_NANINF_CNTL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SU_POINT_SIZE,
                                       0x00080008u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SU_POINT_MINMAX,
                                       0x80000000u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SU_LINE_CNTL,
                                       0x00000008u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_MODE_CNTL_0, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_MODE_CNTL_1, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_LINE_CNTL,
                                       0x00000400u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SU_VTX_CNTL,
                                       0x00000005u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_GB_VERT_CLIP_ADJ,
                                       0x3f800000u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_GB_VERT_DISC_ADJ,
                                       0x3f800000u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_GB_HORZ_CLIP_ADJ,
                                       0x3f800000u);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_CL_GB_HORZ_DISC_ADJ,
                                       0x3f800000u);

    br = ((uint32_t)height << 16) | (uint32_t)width;
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_SCREEN_SCISSOR_TL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_SCREEN_SCISSOR_BR, br);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_WINDOW_OFFSET, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_WINDOW_SCISSOR_TL,
                                       IGL_EG_WINDOW_OFFSET_DISABLE);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_WINDOW_SCISSOR_BR, br);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_GENERIC_SCISSOR_TL,
                                       IGL_EG_WINDOW_OFFSET_DISABLE);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_GENERIC_SCISSOR_BR, br);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_VPORT_SCISSOR_0_TL,
                                       IGL_EG_WINDOW_OFFSET_DISABLE);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_PA_SC_VPORT_SCISSOR_0_BR, br);

    /* VGT fixed state and auto-indexed triangle-list draw. */
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_MAX_VTX_INDX,
                                       0x00ffffffu);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_MIN_VTX_INDX, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_INDX_OFFSET, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_PRIMITIVEID_EN, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_INSTANCE_STEP_RATE_0, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_INSTANCE_STEP_RATE_1, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_REUSE_OFF, 1);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_VTX_CNT_EN, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_SHADER_STAGES_EN, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_STRMOUT_CONFIG, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_VGT_STRMOUT_BUFFER_CONFIG, 0);
    ok = ok && igl_tri_set_config_reg(&cs, IGL_EG_VGT_PRIMITIVE_TYPE,
                                      IGL_EG_DI_PT_TRILIST);
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_INDEX_TYPE, 0)) &&
         igl_tri_emit(&cs, 0);
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_NUM_INSTANCES, 0)) &&
         igl_tri_emit(&cs, 1);
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_DRAW_INDEX_AUTO, 1)) &&
         igl_tri_emit(&cs, 3) &&
         igl_tri_emit(&cs, IGL_EG_DI_SRC_SEL_AUTO_INDEX);

    /* Flush color writes before CPU verification or PRIME presentation. */
    ok = ok && igl_tri_surface_sync(&cs, color,
                                    IGL_EG_CB_ACTION_ENA |
                                    IGL_EG_CB0_DEST_BASE_ENA,
                                    0, RADEON_GEM_DOMAIN_GTT);

    if (!ok || !igl_tri_submit(dev, &cs) || !igl_juniper_bo_wait(dev, color))
        goto fail;

    memset(&domain, 0, sizeof(domain));
    domain.handle = color->handle;
    domain.read_domains = RADEON_GEM_DOMAIN_CPU;
    domain.write_domain = RADEON_GEM_DOMAIN_CPU;
    if (!igl_tri_ioctl(dev->fd, DRM_IOCTL_RADEON_GEM_SET_DOMAIN, &domain))
        goto fail;

    igl_juniper_bo_destroy(dev, &vb);
    igl_juniper_bo_destroy(dev, &ps);
    igl_juniper_bo_destroy(dev, &vs);
    return 1;

fail:
    igl_juniper_bo_destroy(dev, &vb);
    igl_juniper_bo_destroy(dev, &ps);
    igl_juniper_bo_destroy(dev, &vs);
    return 0;
}
