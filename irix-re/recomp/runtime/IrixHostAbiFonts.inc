/*
 * Motif 1.x applications occasionally depend on the private list/context
 * prefix despite XmFontList being documented as opaque. Netscape's bundled
 * Microline does exactly that: it reads the first font-list context pointer
 * and the first node's XFontStruct pointer before calling Motif. Keep those
 * legacy host-visible prefixes, then store the translated object alongside.
 */
typedef struct HostFontList {
    XFontStruct *font;             /* Motif 1.x XmFontListRec prefix */
    XmStringCharSet charset;
    struct HostFontList *next_entry;
    uint32_t guest;
    int owned;
    struct HostFontList *next;
} HostFontList;
typedef struct HostFontContext {
    XmFontList nextFontList;       /* Motif 1.x XmFontListContextRec prefix */
    Boolean unused;
    uint32_t guest;
    struct HostFontContext *next;
} HostFontContext;
static HostFontList *host_font_lists;
static HostFontContext *host_font_contexts;
static XmFontList host_font_wrap(uint32_t guest, int owned);
typedef struct HostFontListEntry {
    uint32_t guest;
    XmFontType type;
    XtPointer font;
    char *tag;
    struct HostFontListEntry *next;
} HostFontListEntry;
static HostFontListEntry *host_font_list_entries;

XmString XmStringCreateLocalized(char *text)
{
    return host_xm_string_create("XmStringCreateLocalized", text, NULL);
}

XmString XmStringCreateSimple(char *text)
{
    return host_xm_string_create("XmStringCreateSimple", text, NULL);
}

XmFontList XmFontListCreate(XFontStruct *font, XmStringCharSet charset)
{
    IrixHostAbiContext *context;
    IrixGuestAddr mark, guest_charset;
    uint32_t args[2] = {0, 0}, result = 0;
    XmFontList list = NULL;
    if (!font || !charset || !context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    args[0] = irix_x11_primary_font_to_guest(&context->runtime, font);
    if (args[0] && !irix_host_abi_context_copy_string(context, charset, &guest_charset)) {
        args[1] = guest_charset;
        if (!irix_host_abi_context_call(context, "XmFontListCreate", args, 2, &result)) {
            list = host_font_wrap(result, 1);
            if (list) {
                HostFontList *item = (HostFontList *)list;
                item->font = font;
                item->charset = charset;
            }
        }
    }
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
        fprintf(stderr, "host XmFontListCreate font=%p guest-font=%08x list=%08x host-list=%p charset=%s\n",
                (void *)font, args[0], result, (void *)list, charset);
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return list;
}

static uint32_t host_font_address(XmFontList font)
{
    HostFontList *item;
    for (item = host_font_lists; item; item = item->next)
        if ((XmFontList)item == font) return item->guest;
    return 0;
}

static XmFontList host_font_wrap(uint32_t guest, int owned)
{
    HostFontList *item;
    if (!guest) return NULL;
    if (!owned)
        for (item = host_font_lists; item; item = item->next)
            if (!item->owned && item->guest == guest) return (XmFontList)item;
    item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->guest = guest; item->owned = owned;
    item->next = host_font_lists; host_font_lists = item;
    return (XmFontList)item;
}

#ifdef _XmGetDefaultFontList
#undef _XmGetDefaultFontList
#endif
#ifdef IRIX_NATIVE32_DSO_BUILD
#define _XmGetDefaultFontList irix_hostabi_impl__XmGetDefaultFontList
#endif
XmFontList _XmGetDefaultFontList(Widget widget, unsigned char type)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[2], result = 0;
    XmFontList font = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address; args[1] = type;
        if (!irix_host_abi_context_call(context, "_XmGetDefaultFontList",
                                        args, 2, &result))
            font = host_font_wrap(result, 0);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return font;
}

XmRenderTable XmeGetDefaultRenderTable(Widget widget, unsigned char type)
{
    return (XmRenderTable)_XmGetDefaultFontList(widget, type);
}

XmFontList XmFontListCopy(XmFontList original)
{
    IrixHostAbiContext *context;
    uint32_t arg, result = 0;
    XmFontList font = NULL;
    if (!original || !context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    arg = host_font_address(original);
    if (arg && !irix_host_abi_context_call(context, "XmFontListCopy", &arg, 1, &result)) {
        font = host_font_wrap(result, 1);
        if (font) {
            HostFontList *src = (HostFontList *)original;
            HostFontList *dst = (HostFontList *)font;
            dst->font = src->font;
            dst->charset = src->charset;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return font;
}

XmFontListEntry XmFontListEntryCreate(char *tag, XmFontType type,
                                      XtPointer font)
{
    IrixHostAbiContext *context;
    IrixGuestAddr mark, guest_tag;
    uint32_t args[3], result = 0;
    HostFontListEntry *entry = NULL;
    if (!tag || !font || !context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (irix_host_abi_context_copy_string(context, tag, &guest_tag)) goto done;
    args[0] = guest_tag;
    args[1] = (uint32_t)type;
    args[2] = type == XmFONT_IS_FONT
        ? irix_x11_primary_font_to_guest(&context->runtime,
                                         (XFontStruct *)font)
        : 0u;
    if (!args[2] || irix_host_abi_context_call(context,
            "XmFontListEntryCreate", args, 3, &result) || !result)
        goto done;
    entry = calloc(1, sizeof(*entry));
    if (!entry) goto done;
    entry->guest = result; entry->type = type; entry->font = font;
    entry->tag = strdup(tag);
    entry->next = host_font_list_entries; host_font_list_entries = entry;
done:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XmFontListEntry)entry;
}

void XmFontListEntryFree(XmFontListEntry *entry_pointer)
{
    IrixHostAbiContext *context;
    HostFontListEntry **link, *entry;
    IrixGuestAddr mark, guest_slot;
    uint32_t value;
    if (!entry_pointer || !*entry_pointer || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (link = &host_font_list_entries; *link; link = &(*link)->next)
        if ((XmFontListEntry)*link == *entry_pointer) break;
    entry = *link;
    if (!entry) goto done;
    mark = context_arena_mark(context);
    value = entry->guest;
    if (!irix_host_abi_context_copy_bytes(context, &value, sizeof(value),
                                          &guest_slot)) {
        (void)irix_host_abi_context_write32be(context, guest_slot, value);
        (void)irix_host_abi_context_call(context, "XmFontListEntryFree",
                                         &guest_slot, 1, NULL);
    }
    context_arena_rewind(context, mark);
    *link = entry->next; free(entry->tag); free(entry);
    *entry_pointer = NULL;
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XmFontList XmFontListAppendEntry(XmFontList list, XmFontListEntry entry_value)
{
    IrixHostAbiContext *context;
    HostFontListEntry *entry;
    uint32_t args[2], result = 0;
    XmFontList result_list = NULL;
    if (!entry_value || !context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (entry = host_font_list_entries; entry; entry = entry->next)
        if ((XmFontListEntry)entry == entry_value) break;
    args[0] = list ? host_font_address(list) : 0;
    args[1] = entry ? entry->guest : 0;
    if (entry && (!list || args[0]) && !irix_host_abi_context_call(context,
            "XmFontListAppendEntry", args, 2, &result)) {
        result_list = host_font_wrap(result, 1);
        if (result_list && list) {
            ((HostFontList *)result_list)->font = ((HostFontList *)list)->font;
            ((HostFontList *)result_list)->charset = ((HostFontList *)list)->charset;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result_list;
}

void XmFontListFree(XmFontList font)
{
    IrixHostAbiContext *context;
    HostFontList **link, *item;
    if (!font || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (link = &host_font_lists; *link; link = &(*link)->next)
        if ((XmFontList)*link == font) break;
    item = *link;
    if (item && item->owned) {
        *link = item->next;
        irix_host_abi_context_call(context, "XmFontListFree", &item->guest, 1, NULL);
        free(item);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmStringExtent(XmFontList font, XmString string, Dimension *width, Dimension *height)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item;
    IrixGuestAddr mark, output;
    uint32_t args[4] = { 0 };
    const unsigned char *bytes;
    unsigned char zero[4] = {0};
    if (width) *width = 0;
    if (height) *height = 0;
    if (!font || !string || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    args[0] = host_font_address(font);
    for (item = g_host_xmstrings; item; item = item->next)
        if ((XmString)item == string) { args[1] = item->guest_address; break; }
    mark = context_arena_mark(context);
    if (args[0] && args[1] &&
        !irix_host_abi_context_copy_bytes(context, zero, 4, &output)) {
        args[2] = output; args[3] = output + 2;
        if (!irix_host_abi_context_call(context, "XmStringExtent", args, 4, NULL)) {
            bytes = irix_guest_ptr(&context->runtime, output, 4);
            if (bytes) {
                if (width) *width = (Dimension)((bytes[0] << 8) | bytes[1]);
                if (height) *height = (Dimension)((bytes[2] << 8) | bytes[3]);
            }
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Dimension XmStringWidth(XmFontList font, XmString string)
{
    Dimension width, height;
    XmStringExtent(font, string, &width, &height);
    return width;
}

Dimension XmStringHeight(XmFontList font, XmString string)
{
    Dimension width, height;
    XmStringExtent(font, string, &width, &height);
    return height;
}

Boolean XmFontListInitFontContext(XmFontContext *out, XmFontList font)
{
    IrixHostAbiContext *context;
    HostFontContext *item;
    IrixGuestAddr mark, output;
    uint32_t args[2], guest_context;
    unsigned char zero[4] = {0};
    const unsigned char *bytes;
    if (out) *out = NULL;
    if (!out || !font || !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    args[1] = host_font_address(font);
    if (!args[1] || irix_host_abi_context_copy_bytes(context, zero, 4, &output)) goto fail;
    /* XmFontListInitFontContext(context_out, font_list). */
    args[0] = output;
    if (irix_host_abi_context_call(context, "XmFontListInitFontContext", args, 2, NULL)) goto fail;
    bytes = irix_guest_ptr(&context->runtime, output, 4);
    guest_context = bytes ? ((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3] : 0;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
        fprintf(stderr, "host XmFontListInitFontContext host-list=%p guest-list=%08x out=%08x guest-context=%08x bytes=%02x%02x%02x%02x\n",
                (void *)font, args[0], output, guest_context,
                bytes ? bytes[0] : 0, bytes ? bytes[1] : 0,
                bytes ? bytes[2] : 0, bytes ? bytes[3] : 0);
    if (!guest_context) goto fail;
    item = calloc(1, sizeof(*item));
    if (!item) goto fail_context;
    item->guest = guest_context;
    item->nextFontList = font;
    item->next = host_font_contexts;
    host_font_contexts = item;
    *out = (XmFontContext)item;
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return True;
fail_context:
    irix_host_abi_context_call(context, "XmFontListFreeFontContext", &guest_context, 1, NULL);
fail:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return False;
}

Boolean XmFontListGetNextFont(XmFontContext opaque, XmStringCharSet *charset,
                              XFontStruct **font)
{
    IrixHostAbiContext *context;
    HostFontContext *item = (HostFontContext *)opaque;
    IrixGuestAddr mark, outputs;
    uint32_t args[3], result = 0, font_guest, charset_guest;
    const unsigned char *bytes;
    if (charset) *charset = NULL;
    if (font) *font = NULL;
    if (!item || !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (irix_host_abi_context_copy_bytes(context, (unsigned char[8]){0}, 8, &outputs)) goto fail;
    args[0] = item->guest; args[1] = outputs; args[2] = outputs + 4;
    if (irix_host_abi_context_call(context, "XmFontListGetNextFont", args, 3, &result) || !result) goto fail;
    bytes = irix_guest_ptr(&context->runtime, outputs, 8);
    if (!bytes) goto fail;
    /* XmFontListGetNextFont(context, charset_out, font_out). */
    charset_guest = ((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
    font_guest = ((uint32_t)bytes[4]<<24)|((uint32_t)bytes[5]<<16)|((uint32_t)bytes[6]<<8)|bytes[7];
    if (font) *font = irix_x11_primary_font_from_guest(font_guest);
    if (charset && charset_guest) *charset = context_guest_string_dup(context, charset_guest);
    /* Microline's Motif 1.x workaround inspects this field before each
     * iteration. This bridge currently exposes one host proxy node per
     * XmFontListCreate() result. */
    item->nextFontList = NULL;
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (font && *font) ? True : False;
fail:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return False;
}

void XmFontListFreeFontContext(XmFontContext opaque)
{
    IrixHostAbiContext *context;
    HostFontContext **link, *item;
    if (!opaque || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (link = &host_font_contexts; *link; link = &(*link)->next)
        if ((XmFontContext)*link == opaque) break;
    item = *link;
    if (item) {
        *link = item->next;
        irix_host_abi_context_call(context, "XmFontListFreeFontContext", &item->guest, 1, NULL);
        free(item);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}
