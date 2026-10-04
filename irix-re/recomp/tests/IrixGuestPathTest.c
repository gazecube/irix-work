#define _XOPEN_SOURCE 700

#include "IrixGuest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_file(const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    fputs("resource-test\n", file);
    return fclose(file) == 0;
}

static int require_path(IrixGuestRuntime *rt, const char *guest,
                        const char *expected)
{
    char buffer[4096];
    const char *actual = irix_guest_resolve_path(
        rt, guest, buffer, sizeof(buffer), "path test", 0);
    if (!actual || strcmp(actual, expected) != 0) {
        fprintf(stderr, "path mismatch: guest=%s expected=%s actual=%s\n",
                guest, expected, actual ? actual : "<null>");
        return 0;
    }
    return 1;
}

int main(void)
{
    char root[] = "/tmp/irix-guest-path-XXXXXX";
    char path[4096];
    char expected[4096];
    const char *home = getenv("HOME");
    struct stat st;
    IrixGuestRuntime rt;
    int ok = 1;

    memset(&rt, 0, sizeof(rt));
    if (!mkdtemp(root)) return 1;
    snprintf(path, sizeof(path), "%s/usr", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/lib", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/lib/X11", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/lib/X11/system.chestrc", root);
    if (!make_file(path)) ok = 0;

    snprintf(path, sizeof(path), "%s/usr/include", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/include/X11", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/include/X11/bitmaps", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path), "%s/usr/include/X11/bitmaps/sgidesktop", root);
    if (mkdir(path, 0700) != 0) ok = 0;
    snprintf(path, sizeof(path),
             "%s/usr/include/X11/bitmaps/sgidesktop/pathfinder.bm", root);
    if (!make_file(path)) ok = 0;

    rt.rootfs = root;
    snprintf(expected, sizeof(expected), "%s/usr/lib/X11/system.chestrc", root);
    if (!require_path(&rt, "/usr/lib/X11/system.chestrc", expected)) ok = 0;

    snprintf(expected, sizeof(expected),
             "%s/usr/include/X11/bitmaps/sgidesktop/pathfinder.bm", root);
    if (!require_path(&rt,
            "/usr/include/X11/bitmaps/sgidesktop/pathfinder.bm", expected))
        ok = 0;

    /* A missing system path must stay inside the guest root, never host /usr. */
    snprintf(expected, sizeof(expected), "%s/usr/share/absent-resource", root);
    if (!require_path(&rt, "/usr/share/absent-resource", expected)) ok = 0;

    /* Existing host home files remain user overrides. */
    if (home) {
        snprintf(path, sizeof(path), "%s/.Xdefaults", home);
        if (stat(path, &st) == 0 && !require_path(&rt, path, path)) ok = 0;
        snprintf(path, sizeof(path), "%s/.irix-resource-absent", home);
        if (path[0] == '/' && strncmp(home, "/home/", 6u) == 0 &&
            !require_path(&rt, path, path)) ok = 0;
    }

    if (!require_path(&rt, "/tmp/irix-guest-path-temp", "/tmp/irix-guest-path-temp"))
        ok = 0;

    snprintf(path, sizeof(path),
             "%s/usr/include/X11/bitmaps/sgidesktop/pathfinder.bm", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/usr/include/X11/bitmaps/sgidesktop", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/include/X11/bitmaps", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/include/X11", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/include", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/lib/X11/system.chestrc", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/usr/lib/X11", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/lib", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/usr", root);
    rmdir(path);
    rmdir(root);
    return ok ? 0 : 1;
}
