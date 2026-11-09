/* Runtime helpers for lifted code: just FP bit views. */
#ifndef LIFT_RT_H
#define LIFT_RT_H
#include <stdint.h>
#include <math.h>
static inline float FS(uint64_t v) { union { uint32_t u; float f; } x; x.u = (uint32_t)v; return x.f; }
static inline double FD(uint64_t v) { union { uint64_t u; double d; } x; x.u = v; return x.d; }
static inline uint64_t SF(uint64_t old, float v) { union { uint32_t u; float f; } x; x.f = v; return (old & 0xffffffff00000000ull) | x.u; }
static inline uint64_t DF(double v) { union { uint64_t u; double d; } x; x.d = v; return x.u; }
/* FP argument/return side channel: the caller's f12..f19 / f0 */
extern uint64_t lift_fpa[8];
extern uint64_t lift_fpr;
#ifdef LIFT_DETECT
extern void lift_det16(unsigned id, const void *a);
extern void lift_det8(unsigned id, const void *a);
#define LIFT_DET16(id, a) lift_det16((id), (const void *)(a))
#define LIFT_DET8(id, a) lift_det8((id), (const void *)(a))
#else
#define LIFT_DET16(id, a) ((void)0)
#define LIFT_DET8(id, a) ((void)0)
#endif
#endif
