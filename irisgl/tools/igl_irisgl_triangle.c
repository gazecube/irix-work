#include <gl/gl.h>

#include <stdio.h>

int main(void)
{
    float a[3] = { 64.0f, 192.0f, 0.0f };
    float b[3] = { 128.0f, 64.0f, 0.0f };
    float c[3] = { 192.0f, 192.0f, 0.0f };

    prefsize(256, 256);
    if (winopen("IRIS GL native Juniper triangle") < 0) {
        fprintf(stderr, "irisgltri: winopen failed\n");
        return 1;
    }

    RGBmode();
    singlebuffer();
    gconfig();

    RGBcolor(0, 0, 0);
    clear();

    RGBcolor(15, 92, 127);
    bgnpolygon();
    v3f(a);
    v3f(b);
    v3f(c);
    endpolygon();

    /* Keep the window visible long enough for manual verification. */
    sginap(1000);
    return 0;
}
