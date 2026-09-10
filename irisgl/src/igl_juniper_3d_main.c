/*
 * Mature Evergreen triangle bring-up overlay.
 *
 * The original igl_juniper_3d.c was reduced from Mesa's first Evergreen
 * bring-up commit (50526e094f4c66957c7f74c190c35903bc82fb62). Keep that
 * file intact as archaeology, but replace its public triangle entry point in
 * this translation unit with state emission cross-checked against current
 * Mesa r600/evergreen_state.c and the Linux radeon Evergreen CS parser.
 */
#define igl_juniper_draw_test_triangle igl_juniper_draw_test_triangle_initial
#include "igl_juniper_3d.c"
#undef igl_juniper_draw_test_triangle

#define IGL_EG_CB_COLOR0_CMASK          0x00028c7cu
#define IGL_EG_CB_COLOR0_CMASK_SLICE    0x00028c80u
#define IGL_EG_CB_COLOR0_FMASK          0x00028c84u
#define IGL_EG_CB_COLOR0_FMASK_SLICE    0x00028c88u
#define IGL_EG_CB_COLOR0_CLEAR_WORD0    0x00028c8cu
#define IGL_EG_CB_COLOR0_CLEAR_WORD1    0x00028c90u

static int igl_tri_set_vertex_resource_main(struct igl_tri_cs *cs,
                                            const struct igl_juniper_bo *vb,
                                            uint32_t data_bytes)
{
    uint32_t word2;
    uint32_t word3;
    uint32_t word7;

    if (!vb || !data_bytes || data_bytes > vb->size)
        return 0;

    /* Current r600 still describes only the valid vertex range, not BO size. */
    word2 = (16u << 8) | (35u << 20) | (2u << 26);
    word3 = (0u << 3) | (1u << 6) | (2u << 9) | (3u << 12);
    word7 = 3u << 30;

    return igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SET_RESOURCE, 8)) &&
           igl_tri_emit(cs, IGL_EG_FETCH_RESOURCE_VS_OFFSET *
                            IGL_EG_FETCH_RESOURCE_STRIDE) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, data_bytes) &&
           igl_tri_emit(cs, word2) &&
           igl_tri_emit(cs, word3) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, 0) &&
           igl_tri_emit(cs, word7) &&
           igl_tri_emit_reloc(cs, vb, RADEON_GEM_DOMAIN_GTT, 0);
}

static int igl_tri_set_color_target_main(struct igl_tri_cs *cs,
                                         const struct igl_juniper_bo *color,
                                         uint32_t pitch_tile_max,
                                         uint32_t slice_tile_max)
{
    /*
     * Current Mesa emits CB_COLOR0_BASE..CLEAR_WORD1 as one 13-register
     * packet, then four legacy-radeon relocation NOPs for BASE, ATTRIB,
     * CMASK and FMASK. This ordering is significant to evergreen_cs.c.
     *
     * We have no separate CMASK/FMASK allocation in this bring-up path, so
     * their base registers alias the color BO, matching Mesa's no-metadata
     * fallback.
     */
    if (!igl_tri_emit(cs, IGL_PKT3(IGL_PKT3_SET_CONTEXT_REG, 13)) ||
        !igl_tri_emit(cs, (IGL_EG_CB_COLOR0_BASE - IGL_CONTEXT_REG_BASE) >> 2) ||
        !igl_tri_emit(cs, 0) ||                    /* BASE */
        !igl_tri_emit(cs, pitch_tile_max) ||       /* PITCH */
        !igl_tri_emit(cs, slice_tile_max) ||       /* SLICE */
        !igl_tri_emit(cs, 0) ||                    /* VIEW */
        !igl_tri_emit(cs, 0x01088068u) ||          /* INFO */
        !igl_tri_emit(cs, 0x00000010u) ||          /* ATTRIB */
        !igl_tri_emit(cs, 0) ||                    /* DIM */
        !igl_tri_emit(cs, 0) ||                    /* CMASK */
        !igl_tri_emit(cs, 0) ||                    /* CMASK_SLICE */
        !igl_tri_emit(cs, 0) ||                    /* FMASK */
        !igl_tri_emit(cs, 0) ||                    /* FMASK_SLICE */
        !igl_tri_emit(cs, 0) ||                    /* CLEAR_WORD0 */
        !igl_tri_emit(cs, 0))                      /* CLEAR_WORD1 */
        return 0;

    return igl_tri_emit_reloc(cs, color, 0, RADEON_GEM_DOMAIN_GTT) &&
           igl_tri_emit_reloc(cs, color, 0, RADEON_GEM_DOMAIN_GTT) &&
           igl_tri_emit_reloc(cs, color, 0, RADEON_GEM_DOMAIN_GTT) &&
           igl_tri_emit_reloc(cs, color, 0, RADEON_GEM_DOMAIN_GTT);
}

/*
 * Current Mesa's Radeon DRM winsys submits legacy r600/evergreen CS with
 * RADEON_CS_KEEP_TILING_FLAGS. Without it evergreen_cs.c may consume an
 * additional relocation while processing CB_COLOR0_INFO, shifting the
 * BASE/ATTRIB/CMASK/FMASK relocation sequence.
 */
static int igl_tri_submit_main(struct igl_juniper_device *dev,
                               struct igl_tri_cs *builder)
{
    struct drm_radeon_cs_chunk chunks[3];
    struct drm_radeon_cs submit;
    uint64_t chunk_ptrs[3];
    uint32_t flags[3];

    flags[0] = RADEON_CS_KEEP_TILING_FLAGS;
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

    ok = igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_CONTEXT_CONTROL, 1)) &&
         igl_tri_emit(&cs, 0x80000000u) &&
         igl_tri_emit(&cs, 0x80000000u);

    /* Juniper resource partition from the original Evergreen bring-up. */
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

    ok = ok && igl_tri_surface_sync(&cs, &vs, IGL_EG_SH_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_surface_sync(&cs, &ps, IGL_EG_SH_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);
    ok = ok && igl_tri_surface_sync(&cs, &vb, IGL_EG_VC_ACTION_ENA,
                                    RADEON_GEM_DOMAIN_GTT, 0);

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

    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_SET_CTL_CONST, 1)) &&
         igl_tri_emit(&cs, 0) && igl_tri_emit(&cs, 0);
    ok = ok && igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_SET_CTL_CONST, 1)) &&
         igl_tri_emit(&cs, 1) && igl_tri_emit(&cs, 0);
    ok = ok && igl_tri_set_vertex_resource_main(&cs, &vb,
                                                (uint32_t)sizeof(vertices));

    ok = ok && igl_tri_surface_sync(&cs, color,
                                    IGL_EG_CB_ACTION_ENA |
                                    IGL_EG_CB0_DEST_BASE_ENA,
                                    0, RADEON_GEM_DOMAIN_GTT);
    ok = ok && igl_tri_set_color_target_main(&cs, color,
                                             pitch_tile_max,
                                             slice_tile_max);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_TARGET_MASK, 0x0000000fu);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_SHADER_MASK, 0x0000000fu);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_BLEND0_CONTROL, 0);
    ok = ok && igl_tri_set_context_reg(&cs, IGL_EG_CB_COLOR_CONTROL,
                                       0x00cc0010u);

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

    ok = ok && igl_tri_surface_sync(&cs, color,
                                    IGL_EG_CB_ACTION_ENA |
                                    IGL_EG_CB0_DEST_BASE_ENA,
                                    0, RADEON_GEM_DOMAIN_GTT);

    if (!ok || !igl_tri_submit_main(dev, &cs) || !igl_juniper_bo_wait(dev, color))
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
