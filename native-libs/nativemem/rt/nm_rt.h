#ifndef NM_RT_H
#define NM_RT_H
#include <stdint.h>
#include <stddef.h>
typedef struct NmDataImport { const char *name; uint32_t addr; } NmDataImport;
typedef struct NmImportSlot { uint32_t slot; const char *name; int32_t addend; } NmImportSlot;
typedef struct NmImage {
    uint32_t main_base, small_base, gp;
    const uint8_t *main, *small;
    size_t main_size, small_size;
    const NmDataImport *dimps;
    size_t ndimps;
    const NmImportSlot *islots;
    size_t nislots;
} NmImage;
void nm_init(const NmImage *img);
#endif
