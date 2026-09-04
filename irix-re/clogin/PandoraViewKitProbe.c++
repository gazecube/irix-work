#include "PandoraViewKit.h"

#include <Vk/VkApp.h>
#include <stdlib.h>

#ifndef PANDORA_APP_DEFAULTS_PATH
#define PANDORA_APP_DEFAULTS_PATH "%N"
#endif

int main(int argc, char **argv)
{
    /*
     * Exercise the normal Xt app-defaults path used by IRIX Clogin instead of
     * baking presentation resources into the probe.  CMake points this at the
     * staged probe-safe Clogin app-defaults file.
     */
    setenv("XFILESEARCHPATH", PANDORA_APP_DEFAULTS_PATH, 1);

    VkApp app("Clogin", &argc, argv);
    PandoraWindow window("pandoraWindow");

    app.setMainWindow(&window);
    window.setTitle("Clogin Pandora ViewKit probe");
    window.show();
    app.run();
    return 0;
}
