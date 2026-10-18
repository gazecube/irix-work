/* Native subclasses retain their own methods and native resource offsets.
 * Their Motif ancestor lives in translated memory; never call its guest
 * entry points with native Widget records. */
typedef struct HostNativeClass {
    WidgetClass widget_class;
    struct HostNativeClass *next;
} HostNativeClass;
static HostNativeClass *host_native_classes;
static int host_class_is_motif(WidgetClass wc);
static void *host_system_xt_handle;

static WidgetClass host_system_composite_class(void)
{
    WidgetClass *slot;
    if (!host_system_xt_handle)
        host_system_xt_handle = dlopen("/usr/lib32/libXt.so.6", RTLD_NOW | RTLD_LOCAL);
    slot = host_system_xt_handle
        ? (WidgetClass *)dlsym(host_system_xt_handle, "compositeWidgetClass")
        : NULL;
    return slot ? *slot : NULL;
}

static int host_native_class_has_manager_ancestor(WidgetClass wc)
{
    WidgetClass current;
    WidgetClass manager = host_motif_class_from_facade("xmManagerWidgetClass");
    if (!wc || host_class_is_motif(wc)) return 0;
    for (current = wc ? wc->core_class.superclass : NULL; current;
         current = current->core_class.superclass) {
        if (current == manager) return 1;
        if (host_class_is_motif(current)) return 0;
    }
    return 0;
}

static int host_class_is_motif(WidgetClass wc)
{
    size_t i;
    for (i = 0; i < sizeof(g_host_motif_classes)/sizeof(*g_host_motif_classes); ++i)
        if (wc == host_motif_class_from_facade(g_host_motif_classes[i].symbol))
            return 1;
    return 0;
}

static int host_class_has_interposed_xt_init(WidgetClass wc)
{
    static const char *const names[] = {
        "Object", "Rect", "Core", "Composite", "Shell", "WMShell",
        "VendorShell", "TopLevelShell", "ApplicationShell"
    };
    size_t i;
    if (!wc || !wc->core_class.class_name) return 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(wc->core_class.class_name, names[i])) return 1;
    return 0;
}

typedef struct HostNativeConverter {
    char *from, *to;
    XtTypeConverter converter;
    XtConvertArgList args;
    Cardinal count;
    XtCacheType cache;
    XtDestructor destructor;
    struct HostNativeConverter *next;
} HostNativeConverter;
static HostNativeConverter *host_native_converters;

XmRepTypeId XmRepTypeRegister(String name, String *names,
                              unsigned char *values, unsigned char count)
{
    IrixHostAbiContext *context;
    IrixGuestAddr mark, guest_name, guest_names, guest_values = 0;
    uint32_t args[4], result = XmREP_TYPE_INVALID;
    unsigned i;
    if (!name || (count && !names) || !context_default(&context)) return result;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (irix_host_abi_context_copy_string(context, name, &guest_name) ||
        irix_host_abi_context_copy_bytes(context, names, count * 4u, &guest_names)) goto done;
    for (i = 0; i < count; ++i) {
        IrixGuestAddr at;
        if (irix_host_abi_context_copy_string(context, names[i], &at)) goto done;
        irix_guest_write32be(&context->runtime, guest_names + i * 4, at);
    }
    if (values && irix_host_abi_context_copy_bytes(context, values, count, &guest_values)) goto done;
    args[0] = guest_name; args[1] = guest_names; args[2] = guest_values; args[3] = count;
    if (irix_host_abi_context_call(context, "XmRepTypeRegister", args, 4, &result))
        result = XmREP_TYPE_INVALID;
done:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

XmRepTypeId XmRepTypeGetId(String name)
{
    IrixHostAbiContext *context;
    IrixGuestAddr mark, arg;
    uint32_t result = XmREP_TYPE_INVALID;
    if (!name || !context_default(&context)) return result;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (!irix_host_abi_context_copy_string(context, name, &arg) &&
        irix_host_abi_context_call(context, "XmRepTypeGetId", &arg, 1, &result))
        result = XmREP_TYPE_INVALID;
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

Boolean XmRepTypeValidValue(XmRepTypeId type, unsigned char value, Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[3] = {type, value, 0}, result = 0;
    if (!context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) args[2] = handle->guest_address;
    if (irix_host_abi_context_call(context, "XmRepTypeValidValue", args, 3, &result)) result = 0;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result ? True : False;
}

void XtSetTypeConverter(_Xconst _XtString from, _Xconst _XtString to,
    XtTypeConverter converter, XtConvertArgList args, Cardinal count,
    XtCacheType cache, XtDestructor destructor)
{
    HostNativeConverter *item;
    if (!from || !to || !converter || (count && !args)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = host_native_converters; item; item = item->next)
        if (!strcmp(item->from, from) && !strcmp(item->to, to)) break;
    if (!item) {
        item = calloc(1, sizeof(*item));
        if (!item) goto done;
        item->from = strdup(from); item->to = strdup(to);
        item->next = host_native_converters; host_native_converters = item;
    }
    free(item->args);
    item->args = count ? malloc(count * sizeof(*args)) : NULL;
    if (count && !item->args) { item->converter = NULL; goto done; }
    if (count) memcpy(item->args, args, count * sizeof(*args));
    item->converter = converter; item->count = count;
    item->cache = cache; item->destructor = destructor;
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static void host_native_class_initialize(WidgetClass wc)
{
    HostNativeClass *item;
    WidgetClass chain[32], current;
    unsigned count = 0;
    if (!wc || host_class_is_motif(wc)) return;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
        fprintf(stderr, "host ABI class initialize %s\n", wc->core_class.class_name);
    for (item = host_native_classes; item; item = item->next)
        if (item->widget_class == wc) return;
    item = calloc(1, sizeof(*item));
    if (!item) return;
    item->widget_class = wc;
    item->next = host_native_classes; host_native_classes = item;
    /* Mark before descending: some partially synthesized Xfe class records
     * contain superclass cycles while the native class graph is being built. */
    host_native_class_initialize(wc->core_class.superclass);
    /* Native Xt class initializers call XtCompileResourceList, whose symbol
     * is interposed by translated IRIX Xt in this process. That code reads
     * N32 Resource records and cannot process the host Xt class records. */
    if (wc->core_class.class_initialize &&
        !host_class_has_interposed_xt_init(wc) &&
        (!wc->core_class.class_name ||
         strcmp(wc->core_class.class_name, "XfeFrameShell") != 0))
        wc->core_class.class_initialize();
    for (current = wc; current && !host_class_is_motif(current) && count < 32;
         current = current->core_class.superclass)
        chain[count++] = current;
    while (count) {
        current = chain[--count];
        if (current->core_class.class_part_initialize &&
            !host_class_has_interposed_xt_init(current))
            current->core_class.class_part_initialize(wc);
    }
#define NATIVE_INHERIT(field, sentinel) \
    if (wc->core_class.field == sentinel && wc->core_class.superclass) \
        wc->core_class.field = wc->core_class.superclass->core_class.field
    NATIVE_INHERIT(realize, XtInheritRealize);
    NATIVE_INHERIT(resize, XtInheritResize);
    NATIVE_INHERIT(expose, XtInheritExpose);
    NATIVE_INHERIT(set_values_almost, XtInheritSetValuesAlmost);
    NATIVE_INHERIT(accept_focus, XtInheritAcceptFocus);
    NATIVE_INHERIT(query_geometry, XtInheritQueryGeometry);
    NATIVE_INHERIT(display_accelerator, XtInheritDisplayAccelerator);
#undef NATIVE_INHERIT
    /* XFE's Motif class part is built with _XtInherit from the translated
     * libXt. Since the Motif ancestor is only a host facade, native Xt's
     * class-part initializer never resolves that sentinel. Calling it later
     * jumps into MIPS code with a host Widget. These callbacks are optional;
     * leave them unset until a host Motif superclass implementation exists. */
    if (wc->core_class.superclass &&
        host_class_is_motif(wc->core_class.superclass)) {
        XmPrimitiveWidgetClass primitive = (XmPrimitiveWidgetClass)wc;
        if (primitive->primitive_class.border_highlight ==
            XmInheritBorderHighlight)
            primitive->primitive_class.border_highlight = NULL;
        if (primitive->primitive_class.border_unhighlight ==
            XmInheritBorderUnhighlight)
            primitive->primitive_class.border_unhighlight = NULL;
    }
    if (host_native_class_has_manager_ancestor(wc)) {
        CompositeWidgetClass composite = (CompositeWidgetClass)wc;
        WidgetClass superclass = wc->core_class.superclass;
        CompositeWidgetClass parent_composite = NULL;
        if (superclass && !host_class_is_motif(superclass))
            parent_composite = (CompositeWidgetClass)superclass;
        if (!parent_composite) {
            WidgetClass system_composite = host_system_composite_class();
            if (system_composite)
                parent_composite = (CompositeWidgetClass)system_composite;
        }
        if (parent_composite) {
            if (composite->composite_class.geometry_manager == XtInheritGeometryManager)
                composite->composite_class.geometry_manager =
                    parent_composite->composite_class.geometry_manager;
            if (composite->composite_class.change_managed == XtInheritChangeManaged)
                composite->composite_class.change_managed =
                    parent_composite->composite_class.change_managed;
            if (composite->composite_class.insert_child == XtInheritInsertChild)
                composite->composite_class.insert_child =
                    parent_composite->composite_class.insert_child;
            if (composite->composite_class.delete_child == XtInheritDeleteChild)
                composite->composite_class.delete_child =
                    parent_composite->composite_class.delete_child;
        }
    }
}

static void host_native_composite_insert_child(IrixHostOpaqueHandle *parent,
                                                IrixHostOpaqueHandle *child)
{
    WidgetClass wc;
    CompositeWidgetClass composite;
    static unsigned trace_count;
    if (!parent || !child || !parent->widget_object) return;
    wc = XtClass(parent->widget_object);
    if (!host_native_class_has_manager_ancestor(wc)) return;
    composite = (CompositeWidgetClass)wc;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && trace_count++ < 40u)
        fprintf(stderr, "libirixhostabi: native insert parent=%s class=%p method=%p child=%s\n",
                wc->core_class.class_name ? wc->core_class.class_name : "?",
                (void *)wc, (void *)composite->composite_class.insert_child,
                child->widget_object->core.name ? child->widget_object->core.name : "?");
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && wc->core_class.class_name &&
        !strcmp(wc->core_class.class_name, "XfeChrome")) {
        uintptr_t *tail = (uintptr_t *)((char *)parent->widget_object +
            wc->core_class.widget_size - 9u * sizeof(uintptr_t));
        fprintf(stderr, "host ABI Chrome slots before %s: %p %p %p %p %p / %p %p %p | csize=%u constraints=%p\n",
            child->widget_object->core.name ? child->widget_object->core.name : "?",
            (void *)tail[0], (void *)tail[1], (void *)tail[2],
            (void *)tail[3], (void *)tail[4], (void *)tail[5],
            (void *)tail[6], (void *)tail[7],
            ((ConstraintWidgetClass)wc)->constraint_class.constraint_size,
            child->widget_object->core.constraints);
    }
    /* Xfe's host methods call through translated Motif class identities for
     * fast subclass predicates.  That rejects valid native children here
     * (for example XfeChrome sees its dashboard as a second menu bar). Keep
     * the Xt composite child list native, then populate the Xfe component
     * slots from the native class chain directly. */
    if (wc->core_class.class_name &&
        (!strcmp(wc->core_class.class_name, "XfeChrome") ||
         !strcmp(wc->core_class.class_name, "XfeDashBoard"))) {
        WidgetClass base = host_system_composite_class();
        CompositeWidgetClass base_composite = (CompositeWidgetClass)base;
        WidgetClass child_class = XtClass(child->widget_object), ancestor;
        if (base_composite && base_composite->composite_class.insert_child &&
            base_composite->composite_class.insert_child != XtInheritInsertChild)
            base_composite->composite_class.insert_child(child->widget_object);
        if (!strcmp(wc->core_class.class_name, "XfeChrome")) {
            Widget *part = (Widget *)((char *)parent->widget_object +
                                      wc->core_class.widget_size - 36u);
            int kind = -1;
            for (ancestor = child_class; ancestor;
                 ancestor = ancestor->core_class.superclass) {
                const char *name = ancestor->core_class.class_name;
                if (!name) continue;
                if (!strcmp(name, "XmRowColumn")) { kind = 0; break; }
                if (!strcmp(name, "XfeToolBox")) { kind = 1; break; }
                if (!strcmp(name, "XfeDashBoard")) { kind = 2; break; }
            }
            if (kind < 0) kind = 3;
            if (kind < 3) part[kind] = child->widget_object;
            else {
                unsigned i;
                for (i = 3; i < 8; ++i)
                    if (!part[i]) { part[i] = child->widget_object; break; }
            }
        } else {
            Widget *part = (Widget *)((char *)parent->widget_object +
                                      wc->core_class.widget_size - 60u);
            for (ancestor = child_class; ancestor;
                 ancestor = ancestor->core_class.superclass) {
                const char *name = ancestor->core_class.class_name;
                if (!name) continue;
                if (!strcmp(name, "XfeTaskBar")) { part[7] = child->widget_object; break; }
                if (!strcmp(name, "XfeToolBar")) { part[4] = child->widget_object; break; }
                if (!strcmp(name, "XfeProgressBar")) { part[6] = child->widget_object; break; }
            }
            if (child->widget_object->core.name &&
                !strcmp(child->widget_object->core.name, "statusBar"))
                part[5] = child->widget_object;
        }
        return;
    }
    /* A native manager's InsertChild may itself call XtCreateWidget for
     * internal children. Xt normally inserts those with its superclass
     * Composite method; re-entering the manager override treats each
     * internal tab as a new application item and recurses without bound. */
    if (host_native_insert_depth) {
        WidgetClass base = host_system_composite_class();
        CompositeWidgetClass base_composite = (CompositeWidgetClass)base;
        if (base_composite && base_composite->composite_class.insert_child &&
            base_composite->composite_class.insert_child != XtInheritInsertChild)
            base_composite->composite_class.insert_child(child->widget_object);
    } else if (composite->composite_class.insert_child &&
        composite->composite_class.insert_child != XtInheritInsertChild)
    {
        ++host_native_insert_depth;
        composite->composite_class.insert_child(child->widget_object);
        --host_native_insert_depth;
    }
}

static void host_native_composite_change_managed(IrixHostOpaqueHandle *parent)
{
    WidgetClass wc;
    CompositeWidgetClass composite;
    if (!parent || !parent->widget_object) return;
    wc = XtClass(parent->widget_object);
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
        wc->core_class.class_name &&
        !strcmp(wc->core_class.class_name, "XmLGrid"))
        fprintf(stderr, "host ABI change_managed class=%s method=%p\n",
            wc->core_class.class_name ? wc->core_class.class_name : "?",
            (void *)((CompositeWidgetClass)wc)->composite_class.change_managed);
    if (!host_native_class_has_manager_ancestor(wc)) return;
    composite = (CompositeWidgetClass)wc;
    if (composite->composite_class.change_managed &&
        composite->composite_class.change_managed != XtInheritChangeManaged)
        composite->composite_class.change_managed(parent->widget_object);
}

static void host_native_prepare_constraint(IrixHostOpaqueHandle *parent,
                                            IrixHostOpaqueHandle *child)
{
    WidgetClass wc;
    ConstraintWidgetClass constraint_class;
    Cardinal size;
    if (!parent || !child || !parent->widget_object || !child->widget_object)
        return;
    wc = XtClass(parent->widget_object);
    if (!host_native_class_has_manager_ancestor(wc)) return;
    constraint_class = (ConstraintWidgetClass)wc;
    size = constraint_class->constraint_class.constraint_size;
    if (!size || size > 65536u) return;
    child->host_constraint_storage = calloc(1, size);
    if (child->host_constraint_storage)
        child->widget_object->core.constraints =
            child->host_constraint_storage;
}

/* Xt initializes a Constraint parent's per-child resources and then runs
 * ConstraintInitialize from superclass to subclass.  The host ABI widget
 * path creates the native widget record itself, so it must perform that
 * part of Xt's lifecycle too.  XfeChrome depends on its chromeChildType
 * constraint to keep toolbars and page views out of the wrong slots. */
static void host_native_constraint_initialize(IrixHostOpaqueHandle *parent,
    IrixHostOpaqueHandle *child, ArgList args, Cardinal arg_count)
{
    WidgetClass chain[32], current;
    ConstraintWidgetClass constraint_class;
    Cardinal count = 0, i, j;
    if (!parent || !child || !parent->widget_object || !child->widget_object ||
        !child->widget_object->core.constraints)
        return;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
        XtClass(parent->widget_object)->core_class.class_name &&
        !strcmp(XtClass(parent->widget_object)->core_class.class_name,
                 "XfeChrome"))
        fprintf(stderr, "host ABI constraint init child=%s storage=%p size=%u\n",
            child->widget_object->core.name ? child->widget_object->core.name : "?",
            child->widget_object->core.constraints,
            ((ConstraintWidgetClass)XtClass(parent->widget_object))->constraint_class.constraint_size);
    for (current = XtClass(parent->widget_object);
         current && !host_class_is_motif(current) && count < 32;
         current = current->core_class.superclass)
        chain[count++] = current;
    while (count) {
        current = chain[--count];
        constraint_class = (ConstraintWidgetClass)current;
        for (i = 0; i < constraint_class->constraint_class.num_resources; ++i) {
            XtResource *resource =
                &constraint_class->constraint_class.resources[i];
            char *destination = (char *)child->widget_object->core.constraints +
                                resource->resource_offset;
            uintptr_t value = (uintptr_t)resource->default_addr;
            int supplied = 0;
            if (resource->resource_offset >
                    constraint_class->constraint_class.constraint_size ||
                resource->resource_size >
                    constraint_class->constraint_class.constraint_size -
                        resource->resource_offset)
                continue;
            for (j = 0; j < arg_count; ++j) {
                if (host_resource_matches(args[j].name,
                                          resource->resource_name)) {
                    value = args[j].value;
                    supplied = 1;
                    break;
                }
            }
            if (supplied || host_resource_matches(resource->default_type,
                                                   "Immediate")) {
                if (resource->resource_size <= sizeof(value))
                    memcpy(destination, &value, resource->resource_size);
            } else if (host_resource_matches(resource->default_type,
                                              "CallProc") &&
                       resource->default_addr) {
                /* XfeChrome's default child-type proc uses Motif subclass
                 * predicates against a facade class graph.  Run it only for
                 * other native classes; Chrome's own class names are exact
                 * and avoid misclassifying every Xfe child as a RowColumn. */
                WidgetClass child_class = XtClass(child->widget_object);
                unsigned char chrome_type = 3u; /* XmCHROME_IGNORE */
                if (child_class && child_class->core_class.class_name &&
                    !strcmp(XtClass(parent->widget_object)->core_class.class_name,
                            "XfeChrome") &&
                    !strcmp(resource->resource_name, "chromeChildType")) {
                    WidgetClass ancestor;
                    for (ancestor = child_class; ancestor;
                         ancestor = ancestor->core_class.superclass) {
                        const char *class_name = ancestor->core_class.class_name;
                        if (!class_name) continue;
                        if (!strcmp(class_name, "XmRowColumn")) { chrome_type = 5u; break; }
                        if (!strcmp(class_name, "XfeToolBox")) { chrome_type = 7u; break; }
                        if (!strcmp(class_name, "XfeDashBoard")) { chrome_type = 2u; break; }
                    }
                    if (chrome_type == 3u) {
                        /* Default unknown children into the first unused view. */
                        size_t part_offset = XtClass(parent->widget_object)->core_class.widget_size - 36u;
                        Widget *slots = (Widget *)((char *)parent->widget_object + part_offset + 12u);
                        if (!slots[0]) chrome_type = 1u;
                        else if (!slots[1]) chrome_type = 8u;
                        else if (!slots[2]) chrome_type = 0u;
                        else if (!slots[3]) chrome_type = 4u;
                        else if (!slots[4]) chrome_type = 6u;
                    }
                    if (resource->resource_size == 1u)
                        memcpy(destination, &chrome_type, 1u);
                } else {
                    XtResourceDefaultProc default_proc =
                        (XtResourceDefaultProc)resource->default_addr;
                    XrmValue result = {0};
                    default_proc(child->widget_object,
                                 (int)resource->resource_offset, &result);
                    if (result.addr && result.size <= resource->resource_size)
                        memcpy(destination, result.addr, result.size);
                }
            } else if (host_resource_matches(resource->default_type,
                                              resource->resource_type) &&
                       resource->default_addr) {
                memcpy(destination, resource->default_addr,
                       resource->resource_size);
            }
        }
        if (constraint_class->constraint_class.initialize)
            constraint_class->constraint_class.initialize(
                NULL, child->widget_object, args, &arg_count);
    }
}

static void host_native_resource_defaults(Widget widget, WidgetClass wc,
                                          ArgList args, Cardinal count)
{
    Cardinal i, j;
    if (!wc || host_class_is_motif(wc)) return;
    host_native_resource_defaults(widget, wc->core_class.superclass, args, count);
    for (i = 0; i < wc->core_class.num_resources; ++i) {
        XtResource *r = &wc->core_class.resources[i];
        void *destination = (char *)widget + r->resource_offset;
        uintptr_t value = (uintptr_t)r->default_addr;
        int supplied = 0;
        if (r->resource_offset > XtClass(widget)->core_class.widget_size ||
            r->resource_size > XtClass(widget)->core_class.widget_size - r->resource_offset)
            continue;
        for (j = 0; j < count; ++j)
            if (host_resource_matches(args[j].name, r->resource_name)) {
                value = args[j].value; supplied = 1; break;
            }
        if (supplied || host_resource_matches(r->default_type, "Immediate")) {
            if (r->resource_size <= sizeof(value)) memcpy(destination, &value, r->resource_size);
        } else if (host_resource_matches(r->default_type, "CallProc")) {
            /* Do not invoke an untyped default proc from a native class
             * resource table: imported Xt callbacks may point into translated
             * IRIX Xt and expect N32 Widget/resource pointers.  Host display
             * defaults are populated separately by host_widget_set_display. */
            continue;
        } else if (host_resource_matches(r->default_type, r->resource_type)) {
            if (host_resource_matches(r->resource_type, "String"))
                memcpy(destination, &value, sizeof(String));
            else if (r->default_addr)
                memcpy(destination, r->default_addr, r->resource_size);
        }
    }
}

static void host_native_widget_initialize(IrixHostOpaqueHandle *handle,
    WidgetClass wc, ArgList args, Cardinal arg_count)
{
    WidgetClass chain[32], current;
    unsigned count = 0;
    unsigned i;
    Widget request;
    if (host_class_is_motif(wc)) return;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
        fprintf(stderr, "host ABI widget initialize %s %s\n", wc->core_class.class_name,
                handle->widget_rec.core.name ? handle->widget_rec.core.name : "?");
    host_native_class_initialize(wc);
    /* Subclasses can reintroduce Motif's inherit marker in their primitive
     * class part during ClassPartInit, so normalize every native class in the
     * chain after all class-part initializers have run. */
    for (current = wc; current && !host_class_is_motif(current) && count < 32;
         current = current->core_class.superclass)
        chain[count++] = current;
    for (i = 0; i < count; ++i) {
        XmPrimitiveWidgetClass primitive = (XmPrimitiveWidgetClass)chain[i];
        if (primitive->primitive_class.border_highlight == XmInheritBorderHighlight)
            primitive->primitive_class.border_highlight = NULL;
        if (primitive->primitive_class.border_unhighlight == XmInheritBorderUnhighlight)
            primitive->primitive_class.border_unhighlight = NULL;
    }
    count = 0;
    host_native_resource_defaults(handle->widget_object, wc, args, arg_count);
    request = malloc(handle->widget_storage_size);
    if (!request) return;
    memcpy(request, handle->widget_object, handle->widget_storage_size);
    for (current = wc; current && !host_class_is_motif(current) && count < 32;
         current = current->core_class.superclass)
        chain[count++] = current;
    while (count) {
        current = chain[--count];
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
            fprintf(stderr, "host ABI initialize method %s\n", current->core_class.class_name);
        if (current->core_class.initialize)
            current->core_class.initialize(request, handle->widget_object, args, &arg_count);
        if (current->core_class.initialize_hook)
            current->core_class.initialize_hook(handle->widget_object, args, &arg_count);
    }
    free(request);
}
