#ifndef IMD_DNA_H
#define IMD_DNA_H

#include <stddef.h>

typedef struct ImdDnaAttr {
    char *key;
    char *value;
} ImdDnaAttr;

typedef struct ImdDnaRecord {
    char *field0;
    char *type;
    char *selector;
    ImdDnaAttr *attrs;
    size_t attr_count;
} ImdDnaRecord;

void imd_dna_record_init(ImdDnaRecord *record);
void imd_dna_record_free(ImdDnaRecord *record);

int imd_dna_parse(const char *text, ImdDnaRecord *out_record);
char *imd_dna_serialize(const ImdDnaRecord *record);

#endif
