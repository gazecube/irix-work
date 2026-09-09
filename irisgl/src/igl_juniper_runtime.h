#ifndef IRISGL_IGL_JUNIPER_RUNTIME_H
#define IRISGL_IGL_JUNIPER_RUNTIME_H

#include <stdint.h>

struct igl_runtime_state;
struct igl_window_state;

/*
 * Reconstructed host integration boundary. This is not an original SGI API.
 * It mirrors the source-proven separation between IRIS GL state and the
 * machine-specific graphics implementation while keeping Radeon details out
 * of the public API and core state transitions.
 */
void igl_juniper_runtime_init(struct igl_runtime_state *runtime);
int igl_juniper_runtime_ensure_surface(struct igl_runtime_state *runtime,
                                       struct igl_window_state *window);
int igl_juniper_runtime_clear(struct igl_runtime_state *runtime,
                              struct igl_window_state *window,
                              uint32_t rgba8);

#endif /* IRISGL_IGL_JUNIPER_RUNTIME_H */
