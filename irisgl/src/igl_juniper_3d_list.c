/*
 * Native Juniper flat triangle-list path.
 *
 * This keeps the proven mature Evergreen PM4/state sequence from
 * igl_juniper_3d_main.c and changes only the vertex-buffer size/content and
 * DRAW_INDEX_AUTO vertex count.  The input stream is non-indexed float4
 * screen-space vertices; vertex_count must be a multiple of three.
 */
#include "igl_juniper_3d_main.c"

static uint32_t igl_list_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int igl_juniper_draw_flat_triangles(struct igl_juniper_device *dev,
                                    struct igl_juniper_bo *color,
                                    int width,
                                    int height,
                                    int stride,
                                    const float (*input_vertices)[4],
                                    uint32_t vertex_count,
                                    uint32_t rgb)
{
    struct igl_juniper_bo vs;
    struct igl_juniper_bo ps;
    struct igl_juniper_bo vb;
    struct igl_tri_cs cs;
    struct drm_radeon_gem_set_domain domain;
    struct igl_tri_vertex *vertices;
    uint32_t *ps_dw;
    uint32_t br;
    uint32_t pitch_pixels;
    uint32_t pitch_tile_max;
    uint32_t slice_tile_max;
    uint64_t vertex_bytes;
    uint64_t vb_bytes;
    float red;
    float green;
    float blue;
    uint32_t i;
    int ok;

    if (!dev || !color || !input_vertices || dev->fd < 0 || !color->handle ||
        width <= 0 || height <= 0 || stride <= 0 || (stride & 255) != 0 ||
        (stride & 3) != 0 || !vertex_count || (vertex_count % 3u) != 0 ||
        color->size < (uint64_t)stride * (uint64_t)height)
        return 0;

    vertex_bytes = (uint64_t)vertex_count * sizeof(struct igl_tri_vertex);
    if (vertex_bytes > UINT32_MAX)
        return 0;

    pitch_pixels = (uint32_t)stride / 4u;
    if ((pitch_pixels & 7u) != 0 ||
        ((uint64_t)pitch_pixels * (uint64_t)height & 63u) != 0)
        return 0;
    pitch_tile_max = pitch_pixels / 8u - 1u;
    slice_tile_max = (uint32_t)(((uint64_t)pitch_pixels *
                                 (uint64_t)height) / 64u - 1u);

    vb_bytes = (vertex_bytes + 4095u) & ~4095ull;
    if (vb_bytes < 4096u)
        vb_bytes = 4096u;

    memset(&vs, 0, sizeof(vs));
    memset(&ps, 0, sizeof(ps));
    memset(&vb, 0, sizeof(vb));
    memset(&cs, 0, sizeof(cs));
    vertices = NULL;

    vertices = malloc((size_t)vertex_bytes);
    if (!vertices)
        goto fail;

    if (!igl_juniper_bo_create(dev, &vs, 4096) ||
        !igl_juniper_bo_create(dev, &ps, 4096) ||
        !igl_juniper_bo_create(dev, &vb, vb_bytes))
        goto fail;

    memcpy(vs.map, igl_juniper_tri_vs, sizeof(igl_juniper_tri_vs));
    memcpy(ps.map, igl_juniper_tri_ps, sizeof(igl_juniper_tri_ps));

    red = (float)((rgb >> 16) & 0xffu) / 255.0f;
    green = (float)((rgb >> 8) & 0xffu) / 255.0f;
    blue = (float)(rgb & 0xffu) / 255.0f;
    ps_dw = (uint32_t *)ps.map;
    ps_dw[IGL_JUNIPER_TRI_PS_DW - 4u] = igl_list_float_bits(red);
    ps_dw[IGL_JUNIPER_TRI_PS_DW - 3u] = igl_list_float_bits(green);
    ps_dw[IGL_JUNIPER_TRI_PS_DW - 2u] = igl_list_float_bits(blue);
    ps_dw[IGL_JUNIPER_TRI_PS_DW - 1u] = igl_list_float_bits(1.0f);

    for (i = 0; i < vertex_count; ++i) {
        vertices[i].x = input_vertices[i][0];
        vertices[i].y = input_vertices[i][1];
        vertices[i].z = input_vertices[i][2];
        vertices[i].w = input_vertices[i][3];
    }
    memcpy(vb.map, vertices, (size_t)vertex_bytes);

    ok = igl_tri_emit(&cs, IGL_PKT3(IGL_PKT3_CONTEXT_CONTROL, 1)) &&
         igl_tri_emit(&cs, 0x80000000u) &&
         igl_tri_emit(&cs, 0x80000000u);

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
                                                (uint32_t)vertex_bytes);

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
         igl_tri_emit(&cs, vertex_count) &&
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

    free(vertices);
    igl_juniper_bo_destroy(dev, &vb);
    igl_juniper_bo_destroy(dev, &ps);
    igl_juniper_bo_destroy(dev, &vs);
    return 1;

fail:
    free(vertices);
    igl_juniper_bo_destroy(dev, &vb);
    igl_juniper_bo_destroy(dev, &ps);
    igl_juniper_bo_destroy(dev, &vs);
    return 0;
}
