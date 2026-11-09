/* Runtime for the lane-mismatch detector (LD_PRELOAD or link into the debug build).
 *
 * Hazard signature: a narrow load that reads the HIGH half/bytes of a little-endian word where MIPS meant the LOW
 * part of a wider field.  For an int that fits in 16 bits the high half is always 0 (or 0xffff for negatives) while
 * the low half carries the value.  A genuine halfword field next to another halfword shows 'real' values (neither 0
 * nor 0xffff) in the high half at least sometimes.  So per site we count:
 *   n      loads
 *   sig    high part in {0,ffff} and the low part non-zero
 *   real   high part is something else (=> genuine narrow field)
 * Report: sites with sig > 0 and real == 0.   Output: $LIFT_DET_OUT (default lift_det.out) at exit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define MAXS 65536
static struct { unsigned n, sig, real; } tab[MAXS];
void lift_det16(unsigned id, const void *a)
{
    uintptr_t p = (uintptr_t)a;
    if (id >= MAXS) return;
    tab[id].n++;
    if ((p & 3) != 2) return;
    unsigned hi = *(const uint16_t *)p, lo = *(const uint16_t *)(p - 2);
    if (hi != 0 && hi != 0xffff) tab[id].real++;
    else if (lo != 0 && (hi == 0 || (lo & 0x8000))) tab[id].sig++;
}
void lift_det8(unsigned id, const void *a)
{
    uintptr_t p = (uintptr_t)a;
    if (id >= MAXS) return;
    tab[id].n++;
    if ((p & 3) != 3) return;
    unsigned hi = *(const uint8_t *)p, mid1 = *(const uint8_t *)(p - 1), mid2 = *(const uint8_t *)(p - 2), lo = *(const uint8_t *)(p - 3);
    (void)mid1; (void)mid2;
    if (hi != 0 && hi != 0xff) tab[id].real++;
    else if (lo != 0 && (hi == 0 || (lo & 0x80))) tab[id].sig++;
}
#include <signal.h>
static void dump(void);
static void on_usr2(int sig) { (void)sig; dump(); }
static void __attribute__((constructor)) init_sig(void) { signal(SIGUSR2, on_usr2); }
static void __attribute__((destructor)) dump_at_exit(void) { dump(); }
static void dump(void)
{
    const char *o = getenv("LIFT_DET_OUT");
    FILE *f = fopen(o ? o : "lift_det.out", "w");
    unsigned i;
    if (!f) return;
    for (i = 0; i < MAXS; i++)
        if (tab[i].n) fprintf(f, "%u %u %u %u\n", i, tab[i].n, tab[i].sig, tab[i].real);
    fclose(f);
}
