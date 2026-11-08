/*
 * vlcd - the libvlc helper for vlcmotif (64-bit).  Protocol: see PROTOCOL.md.
 *
 * Reads commands on stdin, writes events on stdout, one line each, fields separated by TAB.  The video goes to an X window
 * the front end owns (EMBED).  Written against libvlc 3.0.
 *
 * build: gcc -O2 -Wall -o vlcd vlcd.c $(pkg-config --cflags --libs libvlc)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vlc/vlc.h>

#define MAXPL 4096

static libvlc_instance_t *inst;
static libvlc_media_player_t *mp;
static char *playlist[MAXPL];
static int npl, cur = -1;
static int state_sent = -1, volume_sent = -1, mute_sent = -1;
static long time_sent = -1;
static int tracks_due;               /* ticks until the track lists are sent after playback starts */
static int meta_due;
static int want_stop_at_end = 0;

/* ---- wire --------------------------------------------------------------------------------------------------- */
static void enc(const char *s, FILE *o)
{
    for (; *s; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\r' || *s == '%') fprintf(o, "%%%02x", (unsigned char)*s);
        else fputc(*s, o);
    }
}

static void out(const char *cmd, ...)
{
    va_list ap;
    const char *a;
    va_start(ap, cmd);
    fputs(cmd, stdout);
    while ((a = va_arg(ap, const char *)) != NULL) {
        fputc('\t', stdout);
        enc(a, stdout);
    }
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static void outf(const char *cmd, const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    out(cmd, b, NULL);
}

static void dec(char *s)
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

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    if (s && s[1]) return s + 1;
    return p;
}

/* ---- playlist ----------------------------------------------------------------------------------------------- */
static void send_playlist(void)
{
    int i;
    char a[32], b[32], c[32];
    snprintf(a, sizeof a, "%d", npl);
    snprintf(b, sizeof b, "%d", cur);
    out("PLAYLIST", a, b, NULL);
    for (i = 0; i < npl; i++) {
        snprintf(c, sizeof c, "%d", i);
        out("ITEM", c, base_name(playlist[i]), NULL);
    }
}

static libvlc_media_t *make_media(const char *mrl)
{
    return strstr(mrl, "://") ? libvlc_media_new_location(inst, mrl) : libvlc_media_new_path(inst, mrl);
}

static void play_index(int i)
{
    libvlc_media_t *m;
    if (i < 0 || i >= npl) return;
    cur = i;
    m = make_media(playlist[i]);
    if (!m) { out("ERROR", "cannot open", NULL); return; }
    libvlc_media_player_set_media(mp, m);
    libvlc_media_release(m);
    if (libvlc_media_player_play(mp) < 0) out("ERROR", "cannot play", NULL);
    tracks_due = 4;                    /* tracks are known a moment after playback starts */
    meta_due = 4;
    send_playlist();
}

static void add_items(char **f, int n, int replace)
{
    int i, first;
    if (replace) {
        for (i = 0; i < npl; i++) free(playlist[i]);
        npl = 0;
        cur = -1;
    }
    first = npl;
    for (i = 0; i < n && npl < MAXPL; i++)
        if (f[i][0]) playlist[npl++] = strdup(f[i]);
    send_playlist();
    {
        libvlc_state_t st = libvlc_media_player_get_state(mp);
        if (replace || st == libvlc_NothingSpecial || st == libvlc_Stopped || st == libvlc_Ended || st == libvlc_Error)
            play_index(first);
    }
}

/* ---- track and title lists --------------------------------------------------------------------------------------- */
static void send_tracklist(const char *kind, libvlc_track_description_t *list, int current)
{
    libvlc_track_description_t *t;
    int n = 0;
    char a[32], id[32];
    for (t = list; t; t = t->p_next) n++;
    snprintf(a, sizeof a, "%d", n);
    out("TRACKS", kind, a, NULL);
    for (t = list; t; t = t->p_next) {
        snprintf(id, sizeof id, "%d", t->i_id);
        out("TRACK", kind, id, t->i_id == current ? "1" : "0", t->psz_name ? t->psz_name : "?", NULL);
    }
}

static void send_tracks(void)
{
    libvlc_track_description_t *l;
    libvlc_title_description_t **titles = NULL;
    int n, i, ct;
    char a[32], id[32], nm[128];

    if ((l = libvlc_audio_get_track_description(mp))) {
        send_tracklist("audio", l, libvlc_audio_get_track(mp));
        libvlc_track_description_list_release(l);
    }
    if ((l = libvlc_video_get_spu_description(mp))) {
        send_tracklist("sub", l, libvlc_video_get_spu(mp));
        libvlc_track_description_list_release(l);
    }
    n = libvlc_media_player_get_full_title_descriptions(mp, &titles);
    if (n > 0) {
        snprintf(a, sizeof a, "%d", n);
        out("TRACKS", "title", a, NULL);
        ct = libvlc_media_player_get_title(mp);
        for (i = 0; i < n; i++) {
            snprintf(id, sizeof id, "%d", i);
            snprintf(nm, sizeof nm, "%s", titles[i]->psz_name ? titles[i]->psz_name : "");
            if (!nm[0]) snprintf(nm, sizeof nm, "Title %d", i + 1);
            out("TRACK", "title", id, i == ct ? "1" : "0", nm, NULL);
        }
        libvlc_title_descriptions_release(titles, n);
        {
            libvlc_chapter_description_t **chaps = NULL;
            int nc = libvlc_media_player_get_full_chapter_descriptions(mp, ct < 0 ? 0 : ct, &chaps);
            if (nc > 0) {
                int cc = libvlc_media_player_get_chapter(mp);
                snprintf(a, sizeof a, "%d", nc);
                out("TRACKS", "chapter", a, NULL);
                for (i = 0; i < nc; i++) {
                    snprintf(id, sizeof id, "%d", i);
                    snprintf(nm, sizeof nm, "%s", chaps[i]->psz_name ? chaps[i]->psz_name : "");
                    if (!nm[0]) snprintf(nm, sizeof nm, "Chapter %d", i + 1);
                    out("TRACK", "chapter", id, i == cc ? "1" : "0", nm, NULL);
                }
                libvlc_chapter_descriptions_release(chaps, nc);
            }
        }
    }
}

static void send_meta(void)
{
    libvlc_media_t *m = libvlc_media_player_get_media(mp);
    char *title = NULL, *artist = NULL, *album = NULL, *np = NULL;
    if (!m) return;
    title = libvlc_media_get_meta(m, libvlc_meta_Title);
    artist = libvlc_media_get_meta(m, libvlc_meta_Artist);
    album = libvlc_media_get_meta(m, libvlc_meta_Album);
    np = libvlc_media_get_meta(m, libvlc_meta_NowPlaying);
    out("META", title ? title : (cur >= 0 ? base_name(playlist[cur]) : ""), artist ? artist : "", album ? album : "",
        np ? np : "", NULL);
    free(title); free(artist); free(album); free(np);
    libvlc_media_release(m);
}

/* ---- polling ----------------------------------------------------------------------------------------------- */
static const char *state_name(libvlc_state_t s)
{
    switch (s) {
    case libvlc_Opening: return "opening";
    case libvlc_Buffering: return "buffering";
    case libvlc_Playing: return "playing";
    case libvlc_Paused: return "paused";
    case libvlc_Stopped: return "stopped";
    case libvlc_Ended: return "ended";
    case libvlc_Error: return "error";
    default: return "idle";
    }
}

static void tick(void)
{
    libvlc_state_t st = libvlc_media_player_get_state(mp);
    long t, len;
    int v, m;
    if ((int)st != state_sent) {
        state_sent = (int)st;
        out("STATE", state_name(st), NULL);
        if (st == libvlc_Playing) { tracks_due = 4; meta_due = 4; }
        if (st == libvlc_Ended && cur + 1 < npl && !want_stop_at_end) play_index(cur + 1);
    }
    if (st == libvlc_Playing || st == libvlc_Paused) {
        t = (long)libvlc_media_player_get_time(mp);
        len = (long)libvlc_media_player_get_length(mp);
        if (t != time_sent) {
            char a[32], b[32];
            time_sent = t;
            snprintf(a, sizeof a, "%ld", t);
            snprintf(b, sizeof b, "%ld", len);
            out("TIME", a, b, NULL);
        }
        if (tracks_due > 0 && --tracks_due == 0) send_tracks();
        if (meta_due > 0 && --meta_due == 0) send_meta();
    }
    v = libvlc_audio_get_volume(mp);
    m = libvlc_audio_get_mute(mp) > 0;
    if (v >= 0 && (v != volume_sent || m != mute_sent)) {
        char a[16];
        volume_sent = v;
        mute_sent = m;
        snprintf(a, sizeof a, "%d", v);
        out("VOLUME", a, m ? "1" : "0", NULL);
    }
}

/* ---- commands ---------------------------------------------------------------------------------------------- */
static void command(char **f, int n)
{
    const char *c = f[0];
    if (!strcmp(c, "EMBED") && n > 1) {
        libvlc_media_player_set_xwindow(mp, (uint32_t)strtoul(f[1], NULL, 10));
    } else if (!strcmp(c, "OPEN")) {
        add_items(f + 1, n - 1, 1);
    } else if (!strcmp(c, "ADD")) {
        add_items(f + 1, n - 1, 0);
    } else if (!strcmp(c, "PLAYINDEX") && n > 1) {
        play_index(atoi(f[1]));
    } else if (!strcmp(c, "CLEAR")) {
        libvlc_media_player_stop(mp);
        while (npl > 0) free(playlist[--npl]);
        cur = -1;
        send_playlist();
    } else if (!strcmp(c, "NEXT")) {
        if (cur + 1 < npl) play_index(cur + 1);
    } else if (!strcmp(c, "PREV")) {
        if (cur > 0) play_index(cur - 1);
    } else if (!strcmp(c, "PLAY")) {
        if (libvlc_media_player_get_state(mp) == libvlc_Paused) libvlc_media_player_set_pause(mp, 0);
        else if (npl > 0) play_index(cur < 0 ? 0 : cur);
    } else if (!strcmp(c, "PAUSE")) {
        libvlc_media_player_set_pause(mp, 1);
    } else if (!strcmp(c, "TOGGLE")) {
        libvlc_state_t st = libvlc_media_player_get_state(mp);
        if (st == libvlc_Playing || st == libvlc_Paused || st == libvlc_Buffering || st == libvlc_Opening) libvlc_media_player_pause(mp);
        else if (npl > 0) play_index(cur < 0 ? 0 : cur);
    } else if (!strcmp(c, "STOP")) {
        want_stop_at_end = 0;
        libvlc_media_player_stop(mp);
        time_sent = -1;
        out("TIME", "0", "0", NULL);
    } else if (!strcmp(c, "SEEK") && n > 1) {
        libvlc_media_player_set_position(mp, atoi(f[1]) / 1000.0f);
    } else if (!strcmp(c, "JUMP") && n > 1) {
        libvlc_time_t t = libvlc_media_player_get_time(mp) + atol(f[1]);
        libvlc_media_player_set_time(mp, t < 0 ? 0 : t);
    } else if (!strcmp(c, "VOLUME") && n > 1) {
        libvlc_audio_set_volume(mp, atoi(f[1]));
    } else if (!strcmp(c, "MUTE") && n > 1) {
        libvlc_audio_set_mute(mp, atoi(f[1]));
    } else if (!strcmp(c, "RATE") && n > 1) {
        libvlc_media_player_set_rate(mp, atoi(f[1]) / 100.0f);
    } else if (!strcmp(c, "AUDIOTRACK") && n > 1) {
        libvlc_audio_set_track(mp, atoi(f[1]));
    } else if (!strcmp(c, "SPUTRACK") && n > 1) {
        libvlc_video_set_spu(mp, atoi(f[1]));
    } else if (!strcmp(c, "TITLE") && n > 1) {
        libvlc_media_player_set_title(mp, atoi(f[1]));
        tracks_due = 4;
    } else if (!strcmp(c, "CHAPTER") && n > 1) {
        libvlc_media_player_set_chapter(mp, atoi(f[1]));
    } else if (!strcmp(c, "ADDSUB") && n > 1) {
        libvlc_video_set_subtitle_file(mp, f[1]);
        tracks_due = 4;
    } else if (!strcmp(c, "ASPECT") && n > 1) {
        libvlc_video_set_aspect_ratio(mp, !strcmp(f[1], "default") ? NULL : f[1]);
    } else if (!strcmp(c, "DVDMENU")) {
        libvlc_media_player_set_title(mp, 0);
    } else if (!strcmp(c, "QUIT")) {
        libvlc_media_player_stop(mp);
        libvlc_media_player_release(mp);
        libvlc_release(inst);
        exit(0);
    }
}

int main(void)
{
    static const char *args[] = { "--no-xlib", "--no-video-title-show", "--no-mouse-events", "--no-keyboard-events",
                                  "--quiet", "--no-stats", "--no-snapshot-preview" };
    char line[32768];
    size_t len = 0;
    struct pollfd p = { STDIN_FILENO, POLLIN, 0 };

    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    inst = libvlc_new((int)(sizeof args / sizeof args[0]), args);
    if (!inst) {
        out("ERROR", "libvlc did not start", NULL);
        return 1;
    }
    mp = libvlc_media_player_new(inst);
    if (!mp) {
        out("ERROR", "no media player", NULL);
        return 1;
    }
    out("READY", NULL);

    for (;;) {
        int r = poll(&p, 1, 200);
        if (r > 0) {
            ssize_t n = read(STDIN_FILENO, line + len, sizeof line - len - 1);
            char *nl, *start = line;
            if (n <= 0) break;                   /* front end gone */
            len += (size_t)n;
            line[len] = 0;
            while ((nl = strchr(start, '\n')) != NULL) {
                char *f[256], *q = start;
                int nf = 0, i;
                *nl = 0;
                while (nf < 256) {
                    f[nf++] = q;
                    q = strchr(q, '\t');
                    if (!q) break;
                    *q++ = 0;
                }
                for (i = 0; i < nf; i++) dec(f[i]);
                command(f, nf);
                start = nl + 1;
            }
            len = (size_t)(line + len - start);
            memmove(line, start, len);
        }
        tick();
    }
    libvlc_media_player_stop(mp);
    libvlc_media_player_release(mp);
    libvlc_release(inst);
    return 0;
}
