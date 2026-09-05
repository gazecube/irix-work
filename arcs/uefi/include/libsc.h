#ifndef ARCS_UEFI_COMPAT_LIBSC_H
#define ARCS_UEFI_COMPAT_LIBSC_H

#include <stddef.h>

void *malloc(size_t Size);
void free(void *Ptr);
size_t strlen(const char *String);
char *strcpy(char *Dest, const char *Source);
char *getenv(const char *Name);
void panic(const char *Message);

#endif
