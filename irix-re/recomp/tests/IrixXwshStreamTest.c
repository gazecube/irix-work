#define XWSH_STREAM_TEST
#include "../runtime/IrixXwshCompat.c"
#undef NDEBUG
#include <assert.h>
typedef struct { unsigned char data[4096]; size_t size; } Result;
static void collect(void *arg, const unsigned char *p, size_t n)
{
    Result *r = arg;
    assert(r->size + n < sizeof(r->data));
    memcpy(r->data + r->size, p, n); r->size += n;
}
static void check(const char *input, const char *expected)
{
    size_t length = strlen(input), split;
    for (split = 0; split <= length; ++split) {
        XwshStream s = {0}; Result r = {{0},0};
        xwsh_stream_feed(&s, (const unsigned char *)input, split, collect, &r);
        xwsh_stream_feed(&s, (const unsigned char *)input + split, length - split, collect, &r);
        assert(r.size == strlen(expected));
        assert(!memcmp(r.data, expected, r.size));
    }
    {
        XwshStream s = {0}; Result r = {{0},0}; size_t i;
        for (i = 0; i < length; ++i)
            xwsh_stream_feed(&s, (const unsigned char *)input+i, 1, collect, &r);
        assert(r.size == strlen(expected) && !memcmp(r.data, expected, r.size));
    }
}
int main(void)
{
    char long_osc[3000];
    check("plain\r\n\033[31mred\033[0m\033P1.ytitle\033\\", "plain\r\n\033[31mred\033[0m\033P1.ytitle\033\\");
    check("a\033]3008;start=metadata\007b\033]3008;end=id\033\\c", "abc");
    check("a\033]8;;https://example.org\033\\link\033]8;;\007b", "alinkb");
    check("\033]2;title\033\\", "\033]2;title\007");
    check("\033[C\033[0D\033[4F\033[E", "\033[1C\033[1D\033[4A\r\033[1B\r");
    check("\033[0G\033[5G", "\r\r\033[4C");
    check("a\033]3008;x\030b", "ab");
    strcpy(long_osc, "a\033]3008;");
    memset(long_osc+8, 'x', 2500);
    strcpy(long_osc+2508, "\007b");
    check(long_osc, "ab");
    puts("PASS: OSC termination, long payloads, split reads, CSI cursor operations, and legacy sequences");
    return 0;
}
