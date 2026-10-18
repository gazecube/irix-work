#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct Ops { int (*add)(int, int); const char *name; short s; float f; unsigned char b[3]; };
extern struct Ops ops; extern int counter; extern double gd;
extern int use_ops(void), name_len(void), bump(void), gd_int(void), call_native(int (*)(int), int);
extern char *dupname(void);
static int cb(int v) { return v + 1; }
int main(void) {
  int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
  CHECK(ops.s == -2); CHECK(ops.f == 1.5f); CHECK(ops.b[2] == 3); CHECK(strcmp(ops.name, "ops") == 0);
  CHECK(counter == 5); CHECK(gd == 2.25);
  CHECK(use_ops() == 8 + 1 + 3);
  CHECK(name_len() == 3);
  CHECK(bump() == 6); CHECK(counter == 6);
  CHECK(gd_int() == 9);
  CHECK(call_native(cb, 10) == (11 * 2 + 6));
  CHECK(ops.add(20, 22) == 42);          /* native call through a guest function pointer in data */
  char *p = dupname(); CHECK(p && strcmp(p, "ok") == 0);
  printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
  return fails != 0;
}
