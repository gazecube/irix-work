#define _XOPEN_SOURCE 700
#include "IrixX11Color.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s\n", #expr); ok = 0; } } while (0)

int main(void)
{
    char root[] = "/tmp/irix-color-test-XXXXXX", path[4096];
    const char *display_name = getenv("IRIX_TEST_X11_DISPLAY");
    IrixGuestRuntime rt;
    XColor color, exact, screen;
    FILE *file;
    int ok = 1;
    memset(&rt, 0, sizeof(rt));
    if (!mkdtemp(root)) return 1;
    rt.rootfs = root;
    snprintf(path, sizeof(path), "%s/usr", root); CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/usr/lib", root); CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/usr/lib/X11", root); CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/usr/lib/X11/rgb.txt", root);
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("! fixture\n197 193 170 SGIBrightGray\n85 85 85 SGIDarkGray\n"
          "300 0 0 InvalidColor\n19 73 121 IRIXTestOnlyColor\n", file);
    fclose(file);
    CHECK(irix_x11_guest_named_color(&rt, "sgi bright gray", &color));
    CHECK(color.red == 197u * 257u && color.green == 193u * 257u && color.blue == 170u * 257u);
    CHECK(irix_x11_guest_named_color(&rt, "SGIDarkGray", &color));
    CHECK(color.red == 85u * 257u && color.flags == (DoRed | DoGreen | DoBlue));
    CHECK(!irix_x11_guest_named_color(&rt, "InvalidColor", &color));
    CHECK(!irix_x11_guest_named_color(&rt, "AbsentColor", &color));
    CHECK(!irix_x11_guest_named_color(&rt, "#123456", &color));
    if (display_name) {
        Display *display = XOpenDisplay(display_name);
        CHECK(display != 0);
        if (display) {
            Colormap cmap = DefaultColormap(display, DefaultScreen(display));
            CHECK(irix_x11_parse_color(&rt, display, cmap, "IRIXTestOnlyColor", &color));
            CHECK(color.red == 19u * 257u && color.green == 73u * 257u && color.blue == 121u * 257u);
            CHECK(irix_x11_alloc_named_color(&rt, display, cmap, "IRIXTestOnlyColor", &screen, &exact));
            CHECK(exact.red == 19u * 257u && exact.green == 73u * 257u && exact.blue == 121u * 257u);
            XFreeColors(display, cmap, &screen.pixel, 1, 0);
            CHECK(irix_x11_lookup_color(&rt, display, cmap, "IRIXTestOnlyColor", &exact, &screen));
            CHECK(exact.red == 19u * 257u && exact.green == 73u * 257u && exact.blue == 121u * 257u);
            CHECK(irix_x11_parse_color(&rt, display, cmap, "#123456", &color));
            CHECK(XParseColor(display, cmap, "#123456", &exact));
            CHECK(color.red == exact.red && color.green == exact.green && color.blue == exact.blue);
            CHECK(!irix_x11_parse_color(&rt, display, cmap, "AbsentColor", &color));
            XCloseDisplay(display);
        }
    }
    unlink(path);
    CHECK(!irix_x11_guest_named_color(&rt, "SGIDarkGray", &color));
    snprintf(path, sizeof(path), "%s/usr/lib/X11", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/usr/lib", root); rmdir(path);
    snprintf(path, sizeof(path), "%s/usr", root); rmdir(path);
    rmdir(root);
    return ok ? 0 : 1;
}
