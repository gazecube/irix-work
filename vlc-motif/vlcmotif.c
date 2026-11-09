/*
 * vlcmotif - a Motif front end for VLC.
 *
 * 32-bit, built against SGI's Motif 1.2 headers and run on the lifted SGI libXm, so it wears the IRIX look (schemes,
 * palettes).  The player itself is vlcd, a 64-bit helper that owns libvlc; see PROTOCOL.md.  Video is drawn by VLC
 * into the DrawingArea below the menu bar (its window id goes to the helper with EMBED).
 *
 * usage: vlcmotif [--play|--enqueue] [file|mrl ...]
 *   A second invocation hands its arguments to the running instance (single instance, control socket in ~/.vlcmotif).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Xm/CascadeB.h>
#include <Xm/CascadeBG.h>
#include <Xm/PushBG.h>
#include <Xm/SeparatoG.h>
#include <Xm/ToggleBG.h>
#include <Xm/DrawingA.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/Protocols.h>
#include <Xm/AtomMgr.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/ScrolledW.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <Sgm/Dial.h>

/* ---- state ---------------------------------------------------------------------------------------------------- */
static XtAppContext app;
static Widget top, mainw, menubar, video, seek, timelbl, volume, mute_btn, status, plist, plist_frame, play_btn;
static Widget audio_menu, sub_menu, title_menu, chapter_menu, formw, transport_row, ctlform;
static int fullscreen, fs_plist_managed, fs_x, fs_y, fs_w, fs_h;
static XtIntervalId fs_timer, fs_hide_timer;
static int fs_shown, fs_menu_open;
static Widget open_dlg, url_dlg, disc_dlg, sub_dlg;
static int helper_fd = -1, helper_pid, ctl_listen = -1;
static char helper_buf[65536];
static int helper_len;
static int seeking, updating, playing_state;
static long cur_len;
static char ctl_path[PATH_MAX];
static char *pending[256];
static int npending;
static int pending_mode_enqueue;
static int helper_ready;

static void dial_set(Widget w, int v);
static int dial_get(Widget w);

/* ---- wire helpers ------------------------------------------------------------------------------------------------ */
static void enc_to(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 4 < n; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\r' || *s == '%') o += snprintf(out + o, n - o, "%%%02x", (unsigned char)*s);
        else out[o++] = *s;
    }
    out[o] = 0;
}

static void dec_inplace(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *d++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *d++ = *s;
        }
    }
    *d = 0;
}

/* send_cmd("OPEN", path, NULL): fields are encoded and joined by TAB */
static void send_cmd(const char *cmd, ...)
{
    char line[8192], f[4096];
    va_list ap;
    const char *a;
    size_t o;
    if (helper_fd < 0) return;
    o = snprintf(line, sizeof line, "%s", cmd);
    va_start(ap, cmd);
    while ((a = va_arg(ap, const char *)) != NULL) {
        enc_to(a, f, sizeof f);
        o += snprintf(line + o, sizeof line - o, "\t%s", f);
        if (o > sizeof line - 4200) break;
    }
    va_end(ap);
    line[o++] = '\n';
    if (write(helper_fd, line, o) < 0 && errno != EAGAIN) { /* helper gone: the read side notices */ }
}

static void send_int(const char *cmd, long v)
{
    char b[32];
    snprintf(b, sizeof b, "%ld", v);
    send_cmd(cmd, b, NULL);
}

static void set_status(const char *fmt, ...)
{
    char t[512];
    XmString xs;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    xs = XmStringCreateLocalized(t);
    XtVaSetValues(status, XmNlabelString, xs, NULL);
    XmStringFree(xs);
}

static void fmt_time(long ms, char *b)
{
    long s = ms / 1000;
    if (s >= 3600) sprintf(b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else sprintf(b, "%ld:%02ld", s / 60, s % 60);
}

/* ---- opening things ---------------------------------------------------------------------------------------------- */
static char *absolute(const char *a)
{
    static char buf[PATH_MAX];
    if (strstr(a, "://") || a[0] == '/') return (char *)a;
    if (!getcwd(buf, sizeof buf)) return (char *)a;
    strncat(buf, "/", sizeof buf - strlen(buf) - 1);
    strncat(buf, a, sizeof buf - strlen(buf) - 1);
    return buf;
}

static void open_items(int enqueue, char **items, int n)
{
    char *args[260];
    int i;
    if (n <= 0) return;
    for (i = 0; i < n; i++) args[i] = items[i];
    /* the helper takes the fields of one message: send in one go */
    {
        char line[16384], f[4096];
        size_t o = snprintf(line, sizeof line, "%s", enqueue ? "ADD" : "OPEN");
        for (i = 0; i < n; i++) {
            enc_to(args[i], f, sizeof f);
            o += snprintf(line + o, sizeof line - o, "\t%s", f);
            if (o > sizeof line - 4200) break;
        }
        line[o++] = '\n';
        if (helper_fd >= 0 && write(helper_fd, line, o) < 0) { /* see send_cmd */ }
    }
}

static void raise_window(void)
{
    if (XtIsRealized(top)) {
        XMapRaised(XtDisplay(top), XtWindow(top));
    }
}

/* ---- events from the helper ---------------------------------------------------------------------------------------- */
static void clear_menu(Widget menu)
{
    WidgetList kids;
    Cardinal n, i;
    XtVaGetValues(menu, XmNchildren, &kids, XmNnumChildren, &n, NULL);
    for (i = n; i > 0; i--) XtDestroyWidget(kids[i - 1]);
}

static void track_cb(Widget w, XtPointer client, XtPointer call)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call;
    char *spec = (char *)client;                 /* "audio:3" */
    char *c = strchr(spec, ':');
    char cmd[16];
    if (!cbs->set || !c) return;
    if (!strncmp(spec, "audio", 5)) strcpy(cmd, "AUDIOTRACK");
    else if (!strncmp(spec, "sub", 3)) strcpy(cmd, "SPUTRACK");
    else if (!strncmp(spec, "title", 5)) strcpy(cmd, "TITLE");
    else strcpy(cmd, "CHAPTER");
    send_cmd(cmd, c + 1, NULL);
}

static Widget menu_for(const char *kind)
{
    return !strcmp(kind, "audio") ? audio_menu : !strcmp(kind, "sub") ? sub_menu : !strcmp(kind, "title") ? title_menu : chapter_menu;
}

static char tracks_kind[16];
static int tracks_left;

static void handle_line(char *line)
{
    char *f[16];
    int n = 0, i;
    char *p = line;
    for (; n < 16;) {
        f[n++] = p;
        p = strchr(p, '\t');
        if (!p) break;
        *p++ = 0;
    }
    for (i = 0; i < n; i++) dec_inplace(f[i]);

    if (!strcmp(f[0], "READY")) {
        helper_ready = 1;
        if (XtIsRealized(video)) {
            char x[32];
            snprintf(x, sizeof x, "%lu", (unsigned long)XtWindow(video));
            send_cmd("EMBED", x, NULL);
        }
        if (npending) {
            open_items(pending_mode_enqueue, pending, npending);
            npending = 0;
        }
        set_status("Ready");
    } else if (!strcmp(f[0], "STATE") && n > 1) {
        const char *s = f[1];
        playing_state = !strcmp(s, "playing");
        {
            XmString xs = XmStringCreateLocalized(playing_state ? "Pause" : "Play");
            XtVaSetValues(play_btn, XmNlabelString, xs, NULL);
            XmStringFree(xs);
        }
        if (!strcmp(s, "error")) set_status("Error");
        else if (!strcmp(s, "ended") || !strcmp(s, "stopped") || !strcmp(s, "idle")) set_status("%s", s);
        else set_status("%s", !strcmp(s, "opening") ? "Opening..." : !strcmp(s, "buffering") ? "Buffering..." : s);
    } else if (!strcmp(f[0], "TIME") && n > 2) {
        long t = atol(f[1]), len = atol(f[2]);
        char a[32], b[32], both[80];
        XmString xs;
        cur_len = len;
        fmt_time(t, a);
        fmt_time(len, b);
        snprintf(both, sizeof both, "%s / %s", a, len > 0 ? b : "--:--");
        xs = XmStringCreateLocalized(both);
        XtVaSetValues(timelbl, XmNlabelString, xs, NULL);
        XmStringFree(xs);
        if (!seeking && len > 0) {
            updating = 1;
            XmScaleSetValue(seek, (int)(t * 1000L / len));
            updating = 0;
        }
    } else if (!strcmp(f[0], "META") && n > 1) {
        char t[600];
        snprintf(t, sizeof t, "VLC - %s", f[1][0] ? f[1] : "Media Player");
        XtVaSetValues(top, XmNtitle, t, XmNiconName, "VLC", NULL);
        if (n > 2 && f[2][0]) set_status("%s - %s", f[2], f[1]);
        else if (f[1][0]) set_status("%s", f[1]);
    } else if (!strcmp(f[0], "VOLUME") && n > 2) {
        updating = 1;
        dial_set(volume, atoi(f[1]));
        XmToggleButtonSetState(mute_btn, atoi(f[2]) != 0, False);
        updating = 0;
    } else if (!strcmp(f[0], "TRACKS") && n > 2) {
        strncpy(tracks_kind, f[1], sizeof tracks_kind - 1);
        tracks_left = atoi(f[2]);
        clear_menu(menu_for(tracks_kind));
        if (tracks_left == 0) {
            Widget b = XtVaCreateManagedWidget("none", xmPushButtonGadgetClass, menu_for(tracks_kind), XmNsensitive, False, NULL);
            (void)b;
        }
    } else if (!strcmp(f[0], "TRACK") && n > 4 && tracks_left > 0) {
        char spec[64];
        char *keep;
        XmString xs = XmStringCreateLocalized(f[4]);
        Widget b;
        snprintf(spec, sizeof spec, "%s:%s", f[1], f[2]);
        keep = strdup(spec);
        b = XtVaCreateManagedWidget("track", xmToggleButtonGadgetClass, menu_for(f[1]), XmNlabelString, xs,
                                    XmNset, atoi(f[3]) != 0, XmNindicatorType, XmONE_OF_MANY, NULL);
        XtAddCallback(b, XmNvalueChangedCallback, track_cb, keep);
        XmStringFree(xs);
        tracks_left--;
    } else if (!strcmp(f[0], "PLAYLIST") && n > 2) {
        XmListDeleteAllItems(plist);
        XmListSelectPos(plist, 0, False);
        tracks_left = -atoi(f[1]) - 1;          /* ITEM lines follow */
        XtVaSetValues(plist, XmNuserData, (XtPointer)(long)atoi(f[2]), NULL);
    } else if (!strcmp(f[0], "ITEM") && n > 2) {
        XmString xs = XmStringCreateLocalized(f[2]);
        XmListAddItemUnselected(plist, xs, 0);
        XmStringFree(xs);
        {
            XtPointer cur;
            XtVaGetValues(plist, XmNuserData, &cur, NULL);
            if (atoi(f[1]) == (int)(long)cur) XmListSelectPos(plist, atoi(f[1]) + 1, False);
        }
    } else if (!strcmp(f[0], "ERROR")) {
        set_status("Error: %s", n > 1 ? f[1] : "");
    }
}

static void helper_input(XtPointer closure, int *fd, XtInputId *id)
{
    int r = (int)read(helper_fd, helper_buf + helper_len, sizeof helper_buf - helper_len - 1);
    char *nl, *start;
    (void)closure; (void)fd; (void)id;
    if (r <= 0) {
        if (r < 0 && (errno == EAGAIN || errno == EINTR)) return;
        set_status("The player process has ended");
        XtRemoveInput(*id);
        close(helper_fd);
        helper_fd = -1;
        return;
    }
    helper_len += r;
    helper_buf[helper_len] = 0;
    start = helper_buf;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = 0;
        handle_line(start);
        start = nl + 1;
    }
    helper_len = (int)(helper_buf + helper_len - start);
    memmove(helper_buf, start, helper_len);
}

static void start_helper(void)
{
    int sv[2];
    const char *prog = getenv("VLCMOTIF_HELPER");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { perror("socketpair"); exit(1); }
    helper_pid = fork();
    if (helper_pid == 0) {
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);
        unsetenv("LD_PRELOAD");                  /* the helper is 64-bit: the IRIX shim is not for it */
        unsetenv("LD_LIBRARY_PATH");
        if (!prog) prog = "vlcd";
        execlp(prog, prog, (char *)NULL);
        perror(prog);
        _exit(127);
    }
    close(sv[1]);
    helper_fd = sv[0];
    fcntl(helper_fd, F_SETFL, fcntl(helper_fd, F_GETFL) | O_NONBLOCK);
    XtAppAddInput(app, helper_fd, (XtPointer)XtInputReadMask, helper_input, NULL);
}

/* ---- callbacks ---------------------------------------------------------------------------------------------------- */
static void quit_cb(Widget w, XtPointer c, XtPointer d)
{
    send_cmd("QUIT", NULL);
    if (helper_pid > 0) {
        usleep(100000);
        kill(helper_pid, SIGTERM);
        waitpid(helper_pid, NULL, WNOHANG);
    }
    if (ctl_path[0]) unlink(ctl_path);
    exit(0);
}

static void simple_cb(Widget w, XtPointer client, XtPointer call)
{
    send_cmd((const char *)client, NULL);
}

static void toggle_cb(Widget w, XtPointer c, XtPointer d)
{
    send_cmd("TOGGLE", NULL);
}

static void jump_cb(Widget w, XtPointer client, XtPointer call)
{
    send_int("JUMP", (long)client);
}

static void rate_cb(Widget w, XtPointer client, XtPointer call)
{
    send_int("RATE", (long)client);
}

static void aspect_cb(Widget w, XtPointer client, XtPointer call)
{
    send_cmd("ASPECT", (const char *)client, NULL);
}

static void seek_cb(Widget w, XtPointer client, XtPointer call)
{
    XmScaleCallbackStruct *cbs = (XmScaleCallbackStruct *)call;
    if (cbs->reason == XmCR_DRAG) {
        seeking = 1;
        return;
    }
    seeking = 0;
    if (!updating && cur_len > 0) send_int("SEEK", cbs->value);
}

static void volume_cb(Widget w, XtPointer client, XtPointer call)
{
    XmScaleCallbackStruct *cbs = (XmScaleCallbackStruct *)call;
    if (!updating) send_int("VOLUME", cbs->value);
}

static void mute_cb(Widget w, XtPointer client, XtPointer call)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call;
    if (!updating) send_int("MUTE", cbs->set ? 1 : 0);
}

/* the volume knob is an SgDial: its value is a short resource */
static int dial_get(Widget w)
{
    short v = 0;
    XtVaGetValues(w, XmNvalue, &v, NULL);
    return v;
}

static void dial_set(Widget w, int v)
{
    XtVaSetValues(w, XmNvalue, (short)v, NULL);
}

static void volstep_cb(Widget w, XtPointer client, XtPointer call)
{
    int v = dial_get(volume);
    v += (int)(long)client;
    if (v < 0) v = 0;
    if (v > 125) v = 125;
    dial_set(volume, v);
}

static void playlist_sel_cb(Widget w, XtPointer client, XtPointer call)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)call;
    send_int("PLAYINDEX", cbs->item_position - 1);
}

static void playlist_toggle_cb(Widget w, XtPointer client, XtPointer call)
{
    if (XtIsManaged(plist_frame)) XtUnmanageChild(plist_frame);
    else XtManageChild(plist_frame);
}

static void clear_cb(Widget w, XtPointer client, XtPointer call)
{
    send_cmd("CLEAR", NULL);
}

/* file / url / disc dialogs */
static void file_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call;
    char *f = NULL;
    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &f) && f) {
        char *items[1] = { f };
        open_items(client != NULL, items, 1);
        XtFree(f);
    }
    XtUnmanageChild(w);
}

static void sub_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call;
    char *f = NULL;
    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &f) && f) {
        send_cmd("ADDSUB", f, NULL);
        XtFree(f);
    }
    XtUnmanageChild(w);
}

static void dlg_cancel_cb(Widget w, XtPointer client, XtPointer call)
{
    XtUnmanageChild(w);
}

static void url_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call;
    char *f = NULL;
    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &f) && f) {
        char *items[1] = { f };
        if (f[0]) open_items(0, items, 1);
        XtFree(f);
    }
    XtUnmanageChild(w);
}

static void disc_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call;
    char *f = NULL;
    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &f) && f) {
        char mrl[PATH_MAX + 16];
        char *items[1] = { mrl };
        const char *scheme = (const char *)XtParent(w) == NULL ? "dvd" : "dvd";
        XtPointer ud;
        XtVaGetValues(w, XmNuserData, &ud, NULL);
        scheme = (const char *)ud;
        snprintf(mrl, sizeof mrl, "%s://%s", scheme, f);
        if (f[0]) open_items(0, items, 1);
        XtFree(f);
    }
    XtUnmanageChild(w);
}

static Widget make_prompt(const char *name, const char *title, const char *label, const char *initial, XtCallbackProc ok)
{
    Arg a[8];
    int n = 0;
    Widget d;
    XmString t = XmStringCreateLocalized((char *)title), l = XmStringCreateLocalized((char *)label), i = XmStringCreateLocalized((char *)initial);
    XtSetArg(a[n], XmNdialogTitle, t); n++;
    XtSetArg(a[n], XmNselectionLabelString, l); n++;
    XtSetArg(a[n], XmNtextString, i); n++;
    XtSetArg(a[n], XmNautoUnmanage, False); n++;
    d = XmCreatePromptDialog(top, (char *)name, a, n);
    XtUnmanageChild(XmSelectionBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtAddCallback(d, XmNokCallback, ok, NULL);
    XtAddCallback(d, XmNcancelCallback, dlg_cancel_cb, NULL);
    XmStringFree(t); XmStringFree(l); XmStringFree(i);
    return d;
}

static void open_file_cb(Widget w, XtPointer client, XtPointer call)
{
    if (!open_dlg) {
        Arg a[4];
        int n = 0;
        XmString t = XmStringCreateLocalized("Open Media File");
        XtSetArg(a[n], XmNdialogTitle, t); n++;
        XtSetArg(a[n], XmNautoUnmanage, False); n++;
        open_dlg = XmCreateFileSelectionDialog(top, "openFile", a, n);
        XtAddCallback(open_dlg, XmNokCallback, file_ok_cb, NULL);
        XtAddCallback(open_dlg, XmNcancelCallback, dlg_cancel_cb, NULL);
        XtUnmanageChild(XmFileSelectionBoxGetChild(open_dlg, XmDIALOG_HELP_BUTTON));
        XmStringFree(t);
    }
    XtRemoveAllCallbacks(open_dlg, XmNokCallback);
    XtAddCallback(open_dlg, XmNokCallback, file_ok_cb, client);     /* client != NULL: add to playlist */
    XtManageChild(open_dlg);
}

static void open_url_cb(Widget w, XtPointer client, XtPointer call)
{
    if (!url_dlg) url_dlg = make_prompt("openUrl", "Open Network Stream", "URL:", "http://", url_ok_cb);
    XtManageChild(url_dlg);
}

static void open_disc_cb(Widget w, XtPointer client, XtPointer call)
{
    const char *scheme = (const char *)client;
    if (!disc_dlg) disc_dlg = make_prompt("openDisc", "Open Disc", "Drive or folder:", "/dev/sr0", disc_ok_cb);
    XtVaSetValues(disc_dlg, XmNuserData, (XtPointer)scheme, NULL);
    XtManageChild(disc_dlg);
}

static void add_sub_cb(Widget w, XtPointer client, XtPointer call)
{
    if (!sub_dlg) {
        Arg a[4];
        int n = 0;
        XmString t = XmStringCreateLocalized("Add Subtitle File");
        XtSetArg(a[n], XmNdialogTitle, t); n++;
        XtSetArg(a[n], XmNautoUnmanage, False); n++;
        sub_dlg = XmCreateFileSelectionDialog(top, "addSub", a, n);
        XtAddCallback(sub_dlg, XmNokCallback, sub_ok_cb, NULL);
        XtAddCallback(sub_dlg, XmNcancelCallback, dlg_cancel_cb, NULL);
        XtUnmanageChild(XmFileSelectionBoxGetChild(sub_dlg, XmDIALOG_HELP_BUTTON));
        XmStringFree(t);
    }
    XtManageChild(sub_dlg);
}

static void about_cb(Widget w, XtPointer client, XtPointer call)
{
    Widget d;
    Arg a[4];
    int n = 0;
    XmString m = XmStringCreateLtoR("VLC for Motif\n\nA Motif front end for libVLC.\nVideo, audio, DVD and Blu-ray.", XmFONTLIST_DEFAULT_TAG);
    XmString t = XmStringCreateLocalized("About VLC");
    XtSetArg(a[n], XmNmessageString, m); n++;
    XtSetArg(a[n], XmNdialogTitle, t); n++;
    d = XmCreateInformationDialog(top, "about", a, n);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtManageChild(d);
    XmStringFree(m); XmStringFree(t);
}

static void video_realize_cb(Widget w, XtPointer client, XtPointer call)
{
    if (helper_ready) {
        char x[32];
        snprintf(x, sizeof x, "%lu", (unsigned long)XtWindow(video));
        send_cmd("EMBED", x, NULL);
    }
}


/* ---- fullscreen ---------------------------------------------------------------------------------------------------
 * Plain Motif window manager hints, no EWMH: the Mwm decorations go to none and the shell is placed over the whole
 * screen; leaving restores the decorations, the controls and the saved geometry. */
static void fs_controls(int show)
{
    Widget ctl[2];
    int i;
    ctl[0] = ctlform; ctl[1] = menubar;
    if (show) {
        for (i = 0; i < 2; i++) XtManageChild(ctl[i]);
        XtVaSetValues(video, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, ctlform, NULL);
    } else {
        XtVaSetValues(video, XmNbottomAttachment, XmATTACH_FORM, NULL);
        for (i = 0; i < 2; i++) XtUnmanageChild(ctl[i]);
    }
    fs_shown = show;
}

static void fs_hide_cb(XtPointer closure, XtIntervalId *id)
{
    Window root_ret, child_ret;
    int rx, ry, wx, wy;
    unsigned int mask;
    Position sx, sy;
    Dimension mbh = 0;
    (void)closure; (void)id;
    fs_hide_timer = 0;
    if (!fullscreen || !fs_shown) return;
    XQueryPointer(XtDisplay(top), RootWindowOfScreen(XtScreen(top)), &root_ret, &child_ret, &rx, &ry, &wx, &wy, &mask);
    XtVaGetValues(menubar, XmNheight, &mbh, NULL);
    XtTranslateCoords(ctlform, 0, 0, &sx, &sy);
    /* keep them while a menu is open or the pointer is still over the menu bar or the controls */
    if (fs_menu_open > 0 || ry < (int)mbh + 2 || ry >= sy) {
        fs_hide_timer = XtAppAddTimeOut(app, 1000, fs_hide_cb, NULL);
        return;
    }
    fs_controls(0);
}

static void fs_arm_hide(void)
{
    if (fs_hide_timer) XtRemoveTimeOut(fs_hide_timer);
    fs_hide_timer = XtAppAddTimeOut(app, 3000, fs_hide_cb, NULL);
}

static void fs_place_cb(XtPointer closure, XtIntervalId *id)
{
    Screen *scr = XtScreen(top);
    (void)closure; (void)id;
    fs_timer = 0;
    if (!fullscreen) return;
    XtVaSetValues(top, XmNx, 0, XmNy, 0, XmNwidth, (Dimension)WidthOfScreen(scr), XmNheight, (Dimension)HeightOfScreen(scr), NULL);
    XRaiseWindow(XtDisplay(top), XtWindow(top));
}

static void fs_restore_cb(XtPointer closure, XtIntervalId *id)
{
    (void)closure; (void)id;
    fs_timer = 0;
    if (fullscreen) return;
    XtVaSetValues(top, XmNx, fs_x, XmNy, fs_y, XmNwidth, (Dimension)fs_w, XmNheight, (Dimension)fs_h, NULL);
}

static void fullscreen_set(int on)
{
    Display *dpy = XtDisplay(top);
    if (!XtIsRealized(top) || on == fullscreen) return;
    if (fs_timer) { XtRemoveTimeOut(fs_timer); fs_timer = 0; }
    if (on) {
        Window win = XtWindow(top), child;
        XWindowAttributes wa;
        XGetWindowAttributes(dpy, win, &wa);
        /* where the client sits on the root: the window manager honours a configure request in those terms */
        XTranslateCoordinates(dpy, win, RootWindowOfScreen(XtScreen(top)), 0, 0, &fs_x, &fs_y, &child);
        fs_w = wa.width; fs_h = wa.height;
        fs_plist_managed = XtIsManaged(plist_frame);
        if (fs_plist_managed) XtUnmanageChild(plist_frame);
        fs_controls(0);
        fullscreen = 1;
        XtVaSetValues(top, XmNmwmDecorations, 0, NULL);
        fs_timer = XtAppAddTimeOut(app, 150, fs_place_cb, NULL);   /* let the window manager drop the frame first */
    } else {
        fullscreen = 0;
        XtVaSetValues(top, XmNmwmDecorations, -1, NULL);          /* -1: back to the manager's default decorations */
        if (fs_hide_timer) { XtRemoveTimeOut(fs_hide_timer); fs_hide_timer = 0; }
        fs_controls(1);
        if (fs_plist_managed) XtManageChild(plist_frame);
        fs_timer = XtAppAddTimeOut(app, 150, fs_restore_cb, NULL);
    }
}

static void fullscreen_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    fullscreen_set(!fullscreen);
}

static void video_input_cb(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    static Time last;
    (void)w; (void)client; (void)cont;
    if (ev->type == ButtonPress && ev->xbutton.button == Button1) {
        if (last && ev->xbutton.time - last < 400) { fullscreen_set(!fullscreen); last = 0; }
        else last = ev->xbutton.time;
    } else if (ev->type == MotionNotify && fullscreen) {
        Dimension h = 0;
        int y = ev->xmotion.y;
        XtVaGetValues(video, XmNheight, &h, NULL);
        if (!fs_shown && (y < 3 || y >= (int)h - 3)) fs_controls(1);
        if (fs_shown) fs_arm_hide();
    } else if (ev->type == KeyPress) {
        KeySym ks = XLookupKeysym(&ev->xkey, 0);
        if (ks == XK_Escape && fullscreen) fullscreen_set(0);   /* F and Space are menu accelerators */
    }
}

/* ---- single instance: control socket ------------------------------------------------------------------------------ */
static void ctl_input(XtPointer closure, int *fd, XtInputId *id)
{
    int c = accept(ctl_listen, NULL, NULL);
    char buf[16384];
    int r, n = 0, enqueue = 0, i = 0;
    char *items[256], *p;
    if (c < 0) return;
    r = (int)read(c, buf, sizeof buf - 1);
    close(c);
    if (r <= 0) return;
    buf[r] = 0;
    if ((p = strchr(buf, '\n'))) *p = 0;
    p = buf;
    while (p && n < 256) {
        items[n++] = p;
        p = strchr(p, '\t');
        if (p) *p++ = 0;
    }
    for (i = 0; i < n; i++) dec_inplace(items[i]);
    if (n >= 2 && !strcmp(items[0], "ARGS")) {
        enqueue = !strcmp(items[1], "--enqueue");
        open_items(enqueue, items + 2, n - 2);
        raise_window();
    }
}

static int try_forward(int enqueue, char **files, int nfiles)
{
    struct sockaddr_un sa;
    char line[16384], f[4096];
    size_t o;
    int s, i;
    s = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, ctl_path, sizeof sa.sun_path - 1);
    if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
        if (s >= 0) close(s);
        return 0;
    }
    o = snprintf(line, sizeof line, "ARGS\t%s", enqueue ? "--enqueue" : "--play");
    for (i = 0; i < nfiles; i++) {
        enc_to(absolute(files[i]), f, sizeof f);
        o += snprintf(line + o, sizeof line - o, "\t%s", f);
    }
    line[o++] = '\n';
    if (write(s, line, o) < 0) { close(s); return 0; }
    close(s);
    return 1;
}

static void start_control(void)
{
    struct sockaddr_un sa;
    ctl_listen = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, ctl_path, sizeof sa.sun_path - 1);
    unlink(ctl_path);
    if (ctl_listen < 0 || bind(ctl_listen, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(ctl_listen, 4) < 0) {
        ctl_listen = -1;
        return;
    }
    XtAppAddInput(app, ctl_listen, (XtPointer)XtInputReadMask, ctl_input, NULL);
}

/* ---- the window -------------------------------------------------------------------------------------------------- */
static void menu_popup_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    fs_menu_open += (int)(long)client;
    if (fs_menu_open < 0) fs_menu_open = 0;
}

static Widget add_menu(Widget bar, const char *name, const char *label, char mnem, Widget *cascade_out)
{
    Widget pd, cb;
    XmString xs = XmStringCreateLocalized((char *)label);
    pd = XmCreatePulldownMenu(bar, (char *)name, NULL, 0);
    XtAddCallback(XtParent(pd), XtNpopupCallback, menu_popup_cb, (XtPointer)1L);
    XtAddCallback(XtParent(pd), XtNpopdownCallback, menu_popup_cb, (XtPointer)-1L);
    cb = XtVaCreateManagedWidget(name, xmCascadeButtonWidgetClass, bar, XmNlabelString, xs, XmNmnemonic, mnem, XmNsubMenuId, pd, NULL);
    if (!strcmp(name, "help")) XtVaSetValues(bar, XmNmenuHelpWidget, cb, NULL);
    if (cascade_out) *cascade_out = cb;
    XmStringFree(xs);
    return pd;
}

static Widget add_item(Widget menu, const char *label, char mnem, const char *accel, const char *acceltext,
                       XtCallbackProc cb, XtPointer data)
{
    Widget b;
    XmString xs = XmStringCreateLocalized((char *)label);
    XmString at = acceltext ? XmStringCreateLocalized((char *)acceltext) : NULL;
    b = XtVaCreateManagedWidget("item", xmPushButtonGadgetClass, menu, XmNlabelString, xs, XmNmnemonic, mnem, NULL);
    if (accel) XtVaSetValues(b, XmNaccelerator, accel, XmNacceleratorText, at, NULL);
    if (cb) XtAddCallback(b, XmNactivateCallback, cb, data);
    XmStringFree(xs);
    if (at) XmStringFree(at);
    return b;
}

static Widget add_submenu(Widget menu, const char *name, const char *label, char mnem)
{
    Widget pd, cb;
    Arg a[2];
    XmString xs = XmStringCreateLocalized((char *)label);
    XtSetArg(a[0], XmNradioBehavior, True);
    pd = XmCreatePulldownMenu(menu, (char *)name, a, 1);
    cb = XtVaCreateManagedWidget(name, xmCascadeButtonGadgetClass, menu, XmNlabelString, xs, XmNmnemonic, mnem, XmNsubMenuId, pd, NULL);
    (void)cb;
    XmStringFree(xs);
    return pd;
}

static Widget button(Widget parent, const char *label, XtCallbackProc cb, XtPointer data)
{
    Widget b;
    XmString xs = XmStringCreateLocalized((char *)label);
    b = XtVaCreateManagedWidget("btn", xmPushButtonWidgetClass, parent, XmNlabelString, xs, NULL);
    XtAddCallback(b, XmNactivateCallback, cb, data);
    XmStringFree(xs);
    return b;
}

static void center_row_cb(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    Dimension rw = 0, cw = 0;
    int off;
    static int last = -1;
    (void)w; (void)client; (void)cont;
    if (ev->type != ConfigureNotify) return;
    XtVaGetValues(transport_row, XmNwidth, &rw, NULL);
    XtVaGetValues(ctlform, XmNwidth, &cw, NULL);
    off = (int)cw > (int)rw ? ((int)cw - (int)rw) / 2 : 0;
    if (off == last) return;
    last = off;
    XtVaSetValues(transport_row, XmNleftOffset, off, NULL);
}

static void build_ui(void)
{
    Widget form, file, play, aud, subm, nav, viewm, help, sep, row, vol_lbl;
    Arg a[16];
    int n;
    XmString xs;

    mainw = XmCreateMainWindow(top, "main", NULL, 0);
    XtManageChild(mainw);
    menubar = XmCreateMenuBar(mainw, "menubar", NULL, 0);
    XtManageChild(menubar);

    file = add_menu(menubar, "file", "File", 'F', NULL);
    add_item(file, "Open File...", 'O', "Ctrl<Key>o", "Ctrl+O", open_file_cb, NULL);
    add_item(file, "Add File to Playlist...", 'A', NULL, NULL, open_file_cb, (XtPointer)1);
    add_item(file, "Open Network Stream...", 'N', "Ctrl<Key>n", "Ctrl+N", open_url_cb, NULL);
    sep = XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, file, NULL);
    add_item(file, "Open DVD...", 'D', NULL, NULL, open_disc_cb, (XtPointer)"dvd");
    add_item(file, "Open Blu-ray...", 'B', NULL, NULL, open_disc_cb, (XtPointer)"bluray");
    add_item(file, "Open Audio CD...", 'C', NULL, NULL, open_disc_cb, (XtPointer)"cdda");
    sep = XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, file, NULL);
    add_item(file, "Quit", 'Q', "Ctrl<Key>q", "Ctrl+Q", quit_cb, NULL);

    play = add_menu(menubar, "playback", "Playback", 'P', NULL);
    add_item(play, "Play / Pause", 'P', "<Key>space", "Space", toggle_cb, NULL);
    add_item(play, "Stop", 'S', "<Key>s", "S", simple_cb, (XtPointer)"STOP");
    add_item(play, "Previous", 'r', "<Key>p", "P", simple_cb, (XtPointer)"PREV");
    add_item(play, "Next", 'N', "<Key>n", "N", simple_cb, (XtPointer)"NEXT");
    sep = XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, play, NULL);
    add_item(play, "Jump Forward 10 s", 'F', "<Key>Right", "Right", jump_cb, (XtPointer)10000L);
    add_item(play, "Jump Backward 10 s", 'B', "<Key>Left", "Left", jump_cb, (XtPointer)-10000L);
    add_item(play, "Jump Forward 1 min", 'o', "Ctrl<Key>Right", "Ctrl+Right", jump_cb, (XtPointer)60000L);
    add_item(play, "Jump Backward 1 min", 'k', "Ctrl<Key>Left", "Ctrl+Left", jump_cb, (XtPointer)-60000L);
    sep = XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, play, NULL);
    add_item(play, "Speed 0.5x", 0, NULL, NULL, rate_cb, (XtPointer)50L);
    add_item(play, "Speed 1.0x", 0, NULL, NULL, rate_cb, (XtPointer)100L);
    add_item(play, "Speed 1.5x", 0, NULL, NULL, rate_cb, (XtPointer)150L);
    add_item(play, "Speed 2.0x", 0, NULL, NULL, rate_cb, (XtPointer)200L);

    aud = add_menu(menubar, "audio", "Audio", 'A', NULL);
    audio_menu = add_submenu(aud, "audiotrack", "Audio Track", 'T');
    add_item(aud, "Volume Up", 'U', "<Key>Up", "Up", volstep_cb, (XtPointer)5L);
    add_item(aud, "Volume Down", 'D', "<Key>Down", "Down", volstep_cb, (XtPointer)-5L);

    subm = add_menu(menubar, "subtitles", "Subtitles", 'S', NULL);
    sub_menu = add_submenu(subm, "subtrack", "Subtitle Track", 'T');
    add_item(subm, "Add Subtitle File...", 'A', NULL, NULL, add_sub_cb, NULL);

    nav = add_menu(menubar, "navigate", "Navigate", 'N', NULL);
    title_menu = add_submenu(nav, "title", "Title", 'T');
    chapter_menu = add_submenu(nav, "chapter", "Chapter", 'C');
    add_item(nav, "Disc Menu", 'M', NULL, NULL, simple_cb, (XtPointer)"DVDMENU");

    viewm = add_menu(menubar, "view", "View", 'V', NULL);
    add_item(viewm, "Fullscreen", 'F', "<Key>f", "F", fullscreen_cb, NULL);
    add_item(viewm, "Playlist", 'P', "Ctrl<Key>l", "Ctrl+L", playlist_toggle_cb, NULL);
    add_item(viewm, "Clear Playlist", 'C', NULL, NULL, clear_cb, NULL);
    sep = XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, viewm, NULL);
    add_item(viewm, "Aspect: Default", 0, NULL, NULL, aspect_cb, (XtPointer)"default");
    add_item(viewm, "Aspect: 16:9", 0, NULL, NULL, aspect_cb, (XtPointer)"16:9");
    add_item(viewm, "Aspect: 4:3", 0, NULL, NULL, aspect_cb, (XtPointer)"4:3");
    add_item(viewm, "Aspect: 21:9", 0, NULL, NULL, aspect_cb, (XtPointer)"21:9");

    help = add_menu(menubar, "help", "Help", 'H', NULL);
    add_item(help, "About VLC for Motif", 'A', NULL, NULL, about_cb, NULL);

    /* work area: video, seek bar, transport, playlist, status */
    form = XmCreateForm(mainw, "form", NULL, 0);
    XtManageChild(form);
    formw = form;
    /* one container for the seek bar, time, buttons and status line: the video sits above it, so nothing overlaps */
    ctlform = XtVaCreateManagedWidget("controls", xmFormWidgetClass, form, XmNbottomAttachment, XmATTACH_FORM,
                                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);

    status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, ctlform, XmNalignment, XmALIGNMENT_BEGINNING,
                                     XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                                     XmNrightAttachment, XmATTACH_FORM, XmNmarginLeft, 6, NULL);
    xs = XmStringCreateLocalized("Starting...");
    XtVaSetValues(status, XmNlabelString, xs, NULL);
    XmStringFree(xs);

    row = XtVaCreateManagedWidget("transport", xmRowColumnWidgetClass, ctlform, XmNorientation, XmHORIZONTAL,
                                  XmNpacking, XmPACK_TIGHT, XmNspacing, 4, XmNmarginHeight, 4,
                                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, status,
                                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_NONE, NULL);
    transport_row = row;
    XtAddEventHandler(row, StructureNotifyMask, False, center_row_cb, NULL);
    XtAddEventHandler(ctlform, StructureNotifyMask, False, center_row_cb, NULL);
    button(row, "|<", simple_cb, (XtPointer)"PREV");
    button(row, "<<", jump_cb, (XtPointer)-10000L);
    play_btn = button(row, "Play", toggle_cb, NULL);
    button(row, "Stop", simple_cb, (XtPointer)"STOP");
    button(row, ">>", jump_cb, (XtPointer)10000L);
    button(row, ">|", simple_cb, (XtPointer)"NEXT");
    vol_lbl = XtVaCreateManagedWidget("vol", xmLabelWidgetClass, row, NULL);
    xs = XmStringCreateLocalized("  Volume");
    XtVaSetValues(vol_lbl, XmNlabelString, xs, NULL);
    XmStringFree(xs);
    n = 0;
    XtSetArg(a[n], XmNminimum, 0); n++;
    XtSetArg(a[n], XmNmaximum, 125); n++;
    XtSetArg(a[n], XmNvalue, 100); n++;
    XtSetArg(a[n], "startAngle", 225); n++;           /* a real volume knob: stops at 7:30 and 4:30 */
    XtSetArg(a[n], "angleRange", 270); n++;
    XtSetArg(a[n], "dialMarkers", 11); n++;
    XtSetArg(a[n], XmNshadowThickness, 0); n++;        /* no border: just the knob and its marks */
    XtSetArg(a[n], XmNhighlightThickness, 0); n++;
    XtSetArg(a[n], XmNwidth, 65); n++;
    XtSetArg(a[n], XmNheight, 65); n++;
    volume = XtCreateManagedWidget("volume", sgDialWidgetClass, row, a, n);
    XtAddCallback(volume, XmNvalueChangedCallback, volume_cb, NULL);
    XtAddCallback(volume, XmNdragCallback, volume_cb, NULL);
    mute_btn = XtVaCreateManagedWidget("mute", xmToggleButtonWidgetClass, row, NULL);
    xs = XmStringCreateLocalized("Mute");
    XtVaSetValues(mute_btn, XmNlabelString, xs, NULL);
    XmStringFree(xs);
    XtAddCallback(mute_btn, XmNvalueChangedCallback, mute_cb, NULL);

    timelbl = XtVaCreateManagedWidget("time", xmLabelWidgetClass, ctlform, XmNbottomAttachment, XmATTACH_WIDGET,
                                      XmNbottomWidget, row, XmNrightAttachment, XmATTACH_FORM, XmNrecomputeSize, False,
                                      XmNwidth, 130, XmNmarginRight, 6, NULL);
    xs = XmStringCreateLocalized("0:00 / --:--");
    XtVaSetValues(timelbl, XmNlabelString, xs, NULL);
    XmStringFree(xs);

    n = 0;
    XtSetArg(a[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(a[n], XmNminimum, 0); n++;
    XtSetArg(a[n], XmNmaximum, 1000); n++;
    XtSetArg(a[n], XmNshowValue, False); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNbottomWidget, row); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNrightWidget, timelbl); n++;
    seek = XtCreateManagedWidget("seek", xmScaleWidgetClass, ctlform, a, n);
    XtAddCallback(seek, XmNvalueChangedCallback, seek_cb, NULL);
    XtAddCallback(seek, XmNdragCallback, seek_cb, NULL);

    plist_frame = XtVaCreateWidget("playlistFrame", xmFrameWidgetClass, form, XmNbottomAttachment, XmATTACH_WIDGET,
                                   XmNbottomWidget, ctlform, XmNleftAttachment, XmATTACH_FORM,
                                   XmNrightAttachment, XmATTACH_FORM, NULL);
    n = 0;
    XtSetArg(a[n], XmNvisibleItemCount, 6); n++;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    plist = XmCreateScrolledList(plist_frame, "playlist", a, n);
    XtManageChild(plist);
    XtAddCallback(plist, XmNdefaultActionCallback, playlist_sel_cb, NULL);

    n = 0;
    XtSetArg(a[n], XmNwidth, 640); n++;
    XtSetArg(a[n], XmNheight, 360); n++;
    XtSetArg(a[n], XmNbackground, BlackPixel(XtDisplay(top), DefaultScreen(XtDisplay(top)))); n++;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNbottomWidget, ctlform); n++;
    video = XtCreateManagedWidget("video", xmDrawingAreaWidgetClass, form, a, n);
    XtAddCallback(video, XmNexposeCallback, video_realize_cb, NULL);
    XtAddEventHandler(video, ButtonPressMask | KeyPressMask | PointerMotionMask, False, video_input_cb, NULL);

    XmMainWindowSetAreas(mainw, menubar, NULL, NULL, NULL, form);
}

int main(int argc, char **argv)
{
    int i, enqueue = 0, nfiles = 0;
    char *files[256];
    char dir[PATH_MAX];
    const char *home = getenv("HOME");

    signal(SIGPIPE, SIG_IGN);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--enqueue")) enqueue = 1;
        else if (!strcmp(argv[i], "--play")) enqueue = 0;
        else if (argv[i][0] != '-' && nfiles < 256) files[nfiles++] = argv[i];
    }
    snprintf(dir, sizeof dir, "%s/.vlcmotif", home ? home : "/tmp");
    mkdir(dir, 0700);
    snprintf(ctl_path, sizeof ctl_path, "%s/control", dir);
    if (try_forward(enqueue, files, nfiles)) return 0;      /* an instance is running: it has the files now */

    /* strip our own options before Xt sees the command line */
    {
        char *xargv[argc + 1];
        int xargc = 0;
        for (i = 0; i < argc; i++)
            if (i == 0 || (argv[i][0] == '-' && strcmp(argv[i], "--play") && strcmp(argv[i], "--enqueue"))) xargv[xargc++] = argv[i];
        xargv[xargc] = NULL;
        XtSetLanguageProc(NULL, NULL, NULL);
        top = XtVaOpenApplication(&app, "Vlcmotif", NULL, 0, &xargc, xargv, NULL, sessionShellWidgetClass,
                                  XmNtitle, "VLC", XmNiconName, "VLC", XmNdeleteResponse, XmDO_NOTHING, NULL);
    }
    {
        Atom wm_delete = XInternAtom(XtDisplay(top), "WM_DELETE_WINDOW", False);
        XmAddWMProtocolCallback(top, wm_delete, quit_cb, NULL);
    }
    build_ui();
    XtRealizeWidget(top);
    start_control();
    start_helper();
    for (i = 0; i < nfiles && npending < 256; i++) pending[npending++] = strdup(absolute(files[i]));
    pending_mode_enqueue = enqueue;
    XtAppMainLoop(app);
    return 0;
}
