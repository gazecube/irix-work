/* Included by IrixHostAbiContext.c: host-C ABI for the drop-site API. */
extern int irix_x11_image_read_n32_event(IrixGuestRuntime *, IrixGuestAddr,
                                        Display *, XEvent *);

struct IrixHostDropCallback {
    IrixGuestAddr address;
    IrixHostOpaqueHandle *widget;
    XtCallbackProc proc;
    unsigned is_drop;
    struct IrixHostDropCallback *next;
};

static void host_drop_callbacks_free(IrixHostAbiContext *context)
{
    IrixHostDropCallback *item = context->drop_callbacks;
    while (item) {
        IrixHostDropCallback *next = item->next;
        free(item);
        item = next;
    }
}

static void host_drop_dispatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    /* runtime is the first member of the isolated context. */
    IrixHostAbiContext *context = (IrixHostAbiContext *)rt;
    IrixHostDropCallback *item;
    IrixGuestAddr data = (uint32_t)cpu->gpr[6];
    IrixGuestAddr event_addr, drag_addr;
    IrixHostOpaqueHandle *drag_handle;
    XmDropProcCallbackStruct drop;
    XmDragProcCallbackStruct drag;
    XEvent event;
    unsigned char *bytes;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = context->drop_callbacks; item; item = item->next)
        if (item->address == cpu->pc ||
            item->address == (uint32_t)cpu->gpr[25])
            break;
    if (!item || !item->proc || !item->widget->alive ||
        !(bytes = irix_guest_ptr(rt, data, 24u)))
        goto done;
    event_addr = irix_guest_read32be(rt, data + 4u);
    drag_addr = irix_guest_read32be(rt, data + 12u);
    drag_handle = drag_addr ? host_handle_create(IRIX_HOST_HANDLE_WIDGET,
        drag_addr, 0, item->widget->app_guest_address) : NULL;
    if (drag_handle)
        host_widget_set_display(drag_handle, item->widget->host_display,
                                 item->widget->host_screen);
    if (event_addr && !irix_x11_image_read_n32_event(rt, event_addr,
            item->widget->host_display, &event))
        goto done;
    memset(&drop, 0, sizeof(drop));
    drop.reason = (int32_t)irix_guest_read32be(rt, data);
    drop.event = event_addr ? &event : NULL;
    drop.timeStamp = irix_guest_read32be(rt, data + 8u);
    drop.dragContext = host_widget_value(drag_handle);
    drop.x = (int16_t)irix_guest_read16be(rt, data + 16u);
    drop.y = (int16_t)irix_guest_read16be(rt, data + 18u);
    drop.dropSiteStatus = bytes[20];
    drop.operation = bytes[21];
    drop.operations = bytes[22];
    drop.dropAction = bytes[23];
    if (item->is_drop) {
        item->proc(host_widget_value(item->widget), NULL, &drop);
    } else {
        /* Assign fields explicitly: host padding/pointer width is not N32. */
        memset(&drag, 0, sizeof(drag));
        drag.reason = drop.reason; drag.event = drop.event;
        drag.timeStamp = drop.timeStamp; drag.dragContext = drop.dragContext;
        drag.x = drop.x; drag.y = drop.y;
        drag.dropSiteStatus = drop.dropSiteStatus;
        drag.operation = drop.operation; drag.operations = drop.operations;
        drag.animate = bytes[23];
        item->proc(host_widget_value(item->widget), NULL, &drag);
        drop.x = drag.x; drop.y = drag.y;
        drop.dropSiteStatus = drag.dropSiteStatus;
        drop.operation = drag.operation; drop.operations = drag.operations;
        drop.dropAction = drag.animate;
    }
    irix_guest_write16be(rt, data + 16u, (uint16_t)drop.x);
    irix_guest_write16be(rt, data + 18u, (uint16_t)drop.y);
    bytes[20] = drop.dropSiteStatus; bytes[21] = drop.operation;
    bytes[22] = drop.operations; bytes[23] = drop.dropAction;
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static IrixGuestAddr host_drop_callback(IrixHostAbiContext *context,
    IrixHostOpaqueHandle *widget, XtCallbackProc proc, unsigned is_drop)
{
    IrixHostDropCallback *item;
    if (!proc)
        return 0;
    for (item = context->drop_callbacks; item; item = item->next)
        if (item->widget == widget && item->proc == proc &&
            item->is_drop == is_drop)
            return item->address;
    if (context->drop_callback_count >= 4096u)
        return 0;
    item = calloc(1, sizeof(*item));
    if (!item)
        return 0;
    item->address = 0x7e800000u + context->drop_callback_count++ * 4u;
    item->widget = widget; item->proc = proc; item->is_drop = is_drop;
    if (irix_guest_register_function(&context->runtime, item->address,
            host_drop_dispatch, "host_drop_callback") != 0) {
        free(item);
        return 0;
    }
    item->next = context->drop_callbacks;
    context->drop_callbacks = item;
    return item->address;
}

static void host_drop_site_args(const char *api, Widget widget,
                                ArgList args, Cardinal count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_args = 0, targets = 0;
    IrixN32ArgRec records[IRIX_HOST_ABI_MAX_ARGS];
    uint32_t call_args[3], target_count = 0;
    Cardinal i;
    int status = -1, replacing_targets = 0;
    if ((count && !args) || count > IRIX_HOST_ABI_MAX_ARGS ||
        !context_default(&context))
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (!handle)
        goto unlock;
    target_count = handle->drop_target_count;
    mark = context_arena_mark(context);
    for (i = 0; i < count; ++i)
        if (host_resource_matches(args[i].name, "numImportTargets"))
            target_count = (uint32_t)args[i].value;
    if (target_count > 65535u)
        goto done;
    for (i = 0; i < count; ++i) {
        const char *name = NULL;
        uint32_t value = (uint32_t)args[i].value;
        if (host_resource_matches(args[i].name, "dragProc") || host_resource_matches(args[i].name, "dropProc")) {
            unsigned is_drop = host_resource_matches(args[i].name, "dropProc");
            name = is_drop ? "dropProc" : "dragProc";
            value = host_drop_callback(context, handle,
                (XtCallbackProc)(uintptr_t)args[i].value, is_drop);
            if (args[i].value && !value)
                goto done;
        } else if (host_resource_matches(args[i].name, "importTargets")) {
            Atom *host_targets = (Atom *)(uintptr_t)args[i].value;
            uint32_t bytes = target_count * 4u, j;
            if (replacing_targets || (target_count && !host_targets))
                goto done;
            replacing_targets = 1;
            name = "importTargets";
            if (bytes && irix_host_abi_context_call(context, "XtMalloc",
                    &bytes, 1u, &targets) != 0)
                goto done;
            if (bytes && !targets)
                goto done;
            for (j = 0; j < target_count; ++j)
                irix_guest_write32be(&context->runtime, targets + j * 4u,
                                     (uint32_t)host_targets[j]);
            value = targets;
        } else {
#define DROP_SCALAR(token, literal) if (host_resource_matches(args[i].name, literal)) name = literal
            DROP_SCALAR(XmNnumImportTargets, "numImportTargets");
            else DROP_SCALAR(XmNdropSiteActivity, "dropSiteActivity");
            else DROP_SCALAR(XmNdropSiteType, "dropSiteType");
            else DROP_SCALAR(XmNdropSiteOperations, "dropSiteOperations");
            else DROP_SCALAR(XmNanimationStyle, "animationStyle");
#undef DROP_SCALAR
        }
        if (!name) {
            fprintf(stderr, "libirixhostabi: %s rejected unsupported drop-site resource %p\n",
                    api, (void *)args[i].name);
            goto done;
        }
        if (irix_host_abi_context_copy_string(context, name,
                                              &records[i].name) != 0)
            goto done;
        records[i].value = value;
    }
    if (count && irix_host_abi_context_copy_bytes(context, records,
            count * sizeof(*records), &guest_args) != 0)
        goto done;
    for (i = 0; i < count; ++i) {
        irix_guest_write32be(&context->runtime, guest_args + i * 8u,
                             records[i].name);
        irix_guest_write32be(&context->runtime, guest_args + i * 8u + 4u,
                             records[i].value);
    }
    call_args[0] = handle->guest_address; call_args[1] = guest_args;
    call_args[2] = count;
    status = irix_host_abi_context_call(context, api, call_args, 3u, NULL);
    if (status == 0 && replacing_targets) {
        uint32_t old = handle->drop_targets;
        handle->drop_targets = targets; targets = 0;
        handle->drop_target_count = target_count;
        if (old)
            (void)irix_host_abi_context_call(context, "XtFree", &old, 1u, NULL);
    }
done:
    if (targets)
        (void)irix_host_abi_context_call(context, "XtFree", &targets, 1u, NULL);
    context_arena_rewind(context, mark);
    if (status != 0)
        fprintf(stderr, "libirixhostabi: %s did not complete for %p\n", api, (void *)widget);
unlock:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmDropSiteRegister(Widget widget, ArgList args, Cardinal count)
{
    host_drop_site_args("XmDropSiteRegister", widget, args, count);
}

void XmDropSiteUpdate(Widget widget, ArgList args, Cardinal count)
{
    host_drop_site_args("XmDropSiteUpdate", widget, args, count);
}

void XmDropSiteUnregister(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixHostDropCallback *item;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && irix_host_abi_context_call(context, "XmDropSiteUnregister",
            &handle->guest_address, 1u, NULL) == 0) {
        for (item = context->drop_callbacks; item; item = item->next)
            if (item->widget == handle) item->proc = NULL;
        if (handle->drop_targets)
            (void)irix_host_abi_context_call(context, "XtFree",
                &handle->drop_targets, 1u, NULL);
        handle->drop_targets = 0;
        handle->drop_target_count = 0;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

#ifdef IRIX_HOST_ABI_TESTING
/* Test fixture enters the registered guest callback address, rather than
 * calling the native conversion routine directly. No production API. */
int irix_host_abi_test_drop_callback(Widget widget, int is_drop,
                                    const unsigned char input[24],
                                    const unsigned char event_bytes[96],
                                    unsigned char output[24])
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixHostDropCallback *item;
    IrixGuestAddr mark, data, event_addr, name_addr, out_addr, arg_addr;
    uint32_t args[3];
    unsigned char *sentinel;
    unsigned i;
    int status = -1;
    if (!context_default(&context)) return -1;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    for (item = context->drop_callbacks; item; item = item->next)
        if (item->widget == handle && item->is_drop == (unsigned)is_drop &&
            item->proc) break;
    if (!item) goto unlock;
    mark = context_arena_mark(context);
    /* Confirm the real translated drop manager retained this procedure. */
    if (irix_host_abi_context_copy_string(context,
            is_drop ? "dropProc" : "dragProc", &name_addr) != 0 ||
        irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4u,
                                          &out_addr) != 0 ||
        irix_host_abi_context_copy_bytes(context, "\0\0\0\0\0\0\0\0", 8u,
                                          &arg_addr) != 0)
        goto done;
    irix_guest_write32be(&context->runtime, arg_addr, name_addr);
    irix_guest_write32be(&context->runtime, arg_addr + 4u, out_addr);
    args[0] = handle->guest_address; args[1] = arg_addr; args[2] = 1;
    if (irix_host_abi_context_call(context, "XmDropSiteRetrieve", args, 3u,
                                   NULL) != 0 ||
        irix_guest_read32be(&context->runtime, out_addr) != item->address)
        goto done;
    if (irix_host_abi_context_copy_bytes(context, input, 24u, &data) != 0)
        goto done;
    if (event_bytes) {
        if (irix_host_abi_context_copy_bytes(context, event_bytes, 96u,
                                              &event_addr) != 0)
            goto done;
        irix_guest_write32be(&context->runtime, data + 4u, event_addr);
    }
    sentinel = irix_guest_ptr(&context->runtime, 0x7fdffec0u, 64u);
    if (!sentinel) goto done;
    memset(sentinel, 0xa5, 64u);
    args[0] = handle->guest_address; args[1] = 0; args[2] = data;
    status = host_call_guest_address(context, item->address, args, 3u, NULL);
    memcpy(output, irix_guest_ptr(&context->runtime, data, 24u), 24u);
    for (i = 0; i < 64u; ++i)
        if (sentinel[i] != 0xa5) status = -1;
done:
    context_arena_rewind(context, mark);
unlock:
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status;
}
#endif
