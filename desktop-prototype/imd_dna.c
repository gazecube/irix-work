#include "imd_dna.h"

#include <stdlib.h>
#include <string.h>

static char *dup_range(const char *begin, const char *end)
{
    size_t n;
    char *out;

    if (!begin || !end || end < begin)
        return NULL;
    n = (size_t)(end - begin);
    out = (char *)malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, begin, n);
    out[n] = '\0';
    return out;
}

static int hexpair_to_char(char a, char b, char *out)
{
    if (a == '2' && b == '1') { *out = '!'; return 1; }
    if (a == '3' && b == 'a') { *out = ':'; return 1; }
    if (a == '3' && b == 'd') { *out = '='; return 1; }
    if (a == '2' && b == '5') { *out = '%'; return 1; }
    return 0;
}

static char *imd_unescape_range(const char *begin, const char *end)
{
    char *out;
    char *dst;
    const char *src;

    out = (char *)malloc((size_t)(end - begin) + 1);
    if (!out)
        return NULL;

    src = begin;
    dst = out;
    while (src < end) {
        if (*src == '%' && src + 2 < end) {
            char decoded;
            if (hexpair_to_char(src[1], src[2], &decoded)) {
                *dst++ = decoded;
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    return out;
}

static size_t escaped_len(const char *s)
{
    size_t n = 0;
    while (*s) {
        switch (*s++) {
        case '!': case ':': case '=': case '%': n += 3; break;
        default: n += 1; break;
        }
    }
    return n;
}

static char *append_escaped(char *dst, const char *s)
{
    while (*s) {
        switch (*s) {
        case '!': memcpy(dst, "%21", 3); dst += 3; break;
        case ':': memcpy(dst, "%3a", 3); dst += 3; break;
        case '=': memcpy(dst, "%3d", 3); dst += 3; break;
        case '%': memcpy(dst, "%25", 3); dst += 3; break;
        default: *dst++ = *s; break;
        }
        ++s;
    }
    return dst;
}

void imd_dna_record_init(ImdDnaRecord *record)
{
    if (record)
        memset(record, 0, sizeof(*record));
}

void imd_dna_record_free(ImdDnaRecord *record)
{
    size_t i;
    if (!record)
        return;
    free(record->field0);
    free(record->type);
    free(record->selector);
    for (i = 0; i < record->attr_count; ++i) {
        free(record->attrs[i].key);
        free(record->attrs[i].value);
    }
    free(record->attrs);
    imd_dna_record_init(record);
}

static int append_attr(ImdDnaRecord *record, char *key, char *value)
{
    ImdDnaAttr *attrs = (ImdDnaAttr *)realloc(
        record->attrs, (record->attr_count + 1) * sizeof(*attrs));
    if (!attrs)
        return 0;
    record->attrs = attrs;
    record->attrs[record->attr_count].key = key;
    record->attrs[record->attr_count].value = value;
    ++record->attr_count;
    return 1;
}

int imd_dna_parse(const char *text, ImdDnaRecord *out_record)
{
    const char *c1, *c2, *bang, *p;
    ImdDnaRecord tmp;

    if (!text || !out_record)
        return 0;

    imd_dna_record_init(&tmp);
    c1 = strchr(text, ':');
    if (!c1)
        goto fail;
    c2 = strchr(c1 + 1, ':');
    if (!c2)
        goto fail;
    bang = strchr(c2 + 1, '!');
    if (!bang)
        goto fail;

    tmp.field0 = imd_unescape_range(text, c1);
    tmp.type = imd_unescape_range(c1 + 1, c2);
    tmp.selector = imd_unescape_range(c2 + 1, bang);
    if (!tmp.field0 || !tmp.type || !tmp.selector)
        goto fail;

    p = bang + 1;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *colon;
        char *key;
        char *value;

        if (!eq)
            goto fail;
        colon = strchr(eq + 1, ':');
        if (!colon)
            goto fail;

        key = imd_unescape_range(p, eq);
        value = imd_unescape_range(eq + 1, colon);
        if (!key || !value) {
            free(key);
            free(value);
            goto fail;
        }
        if (!append_attr(&tmp, key, value)) {
            free(key);
            free(value);
            goto fail;
        }
        p = colon + 1;
    }

    imd_dna_record_free(out_record);
    *out_record = tmp;
    return 1;

fail:
    imd_dna_record_free(&tmp);
    return 0;
}

char *imd_dna_serialize(const ImdDnaRecord *record)
{
    size_t len, i;
    char *out, *p;
    const char *field0, *type, *selector;

    if (!record)
        return NULL;

    field0 = record->field0 ? record->field0 : "";
    type = record->type ? record->type : "";
    selector = record->selector ? record->selector : "";

    len = escaped_len(field0) + escaped_len(type) + escaped_len(selector) + 3;
    for (i = 0; i < record->attr_count; ++i) {
        const char *key = record->attrs[i].key ? record->attrs[i].key : "";
        const char *value = record->attrs[i].value ? record->attrs[i].value : "";
        len += escaped_len(key) + escaped_len(value) + 2;
    }

    out = (char *)malloc(len + 1);
    if (!out)
        return NULL;

    p = out;
    p = append_escaped(p, field0); *p++ = ':';
    p = append_escaped(p, type); *p++ = ':';
    p = append_escaped(p, selector); *p++ = '!';
    for (i = 0; i < record->attr_count; ++i) {
        p = append_escaped(p, record->attrs[i].key ? record->attrs[i].key : "");
        *p++ = '=';
        p = append_escaped(p, record->attrs[i].value ? record->attrs[i].value : "");
        *p++ = ':';
    }
    *p = '\0';
    return out;
}
