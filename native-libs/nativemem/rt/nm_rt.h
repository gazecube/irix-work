#ifndef NM_RT_H
#define NM_RT_H
#include <stdint.h>
#include <stddef.h>
typedef struct NmImage {
    uint32_t main_base, small_base, gp;
    const uint8_t *main, *small;
    size_t main_size, small_size;
} NmImage;
void nm_init(const NmImage *img);
#endif
