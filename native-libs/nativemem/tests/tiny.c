#include <stddef.h>
extern size_t strlen(const char *);
extern void *malloc(size_t);
struct Ops { int (*add)(int, int); const char *name; short s; float f; unsigned char b[3]; };
static int add(int a, int b) { return a + b; }
struct Ops ops = { add, "ops", -2, 1.5f, { 1, 2, 3 } };
int counter = 5;
double gd = 2.25;
int use_ops(void) { return ops.add(ops.s, 10) + (int)ops.f + ops.b[2]; }
int call_native(int (*cb)(int), int v) { return cb(v) * 2 + counter; }
int name_len(void) { return (int)strlen(ops.name); }
int bump(void) { return ++counter; }
int gd_int(void) { return (int)(gd * 4.0); }
char *dupname(void) { char *p = malloc(8); p[0] = 'o'; p[1] = 'k'; p[2] = 0; return p; }
