#include "PandoraViewKit.h"

#include <Vk/VkApp.h>
#include <stdlib.h>

#ifndef PANDORA_APP_DEFAULTS_PATH
#define PANDORA_APP_DEFAULTS_PATH "%N"
#endif

#ifndef PANDORA_USER_TOKEN_FTI_PATH
#define PANDORA_USER_TOKEN_FTI_PATH ""
#endif

int main(int argc, char **argv)
{
    /*
     * Exercise the normal Xt app-defaults path used by IRIX Clogin instead of
     * baking presentation resources into the probe. CMake points this at the
     * staged probe-safe Clogin app-defaults file.
     */
    setenv("XFILESEARCHPATH", PANDORA_APP_DEFAULTS_PATH, 1);

    /*
     * CMake extracts the exact Users.Token.fti member from evidence/icons.tar
     * and verifies its SHA-256 before defining this path. Expose the verified
     * test vector through the probe process environment so the portable Imd
     * renderer path can consume it without baking evidence-tree locations into
     * presentation classes. The runtime mapping from Clogin to this artwork is
     * still under reconstruction; this is a deterministic visual test vector.
     */
    if (PANDORA_USER_TOKEN_FTI_PATH[0] != '\0')
        setenv("PANDORA_USER_TOKEN_FTI", PANDORA_USER_TOKEN_FTI_PATH, 1);

    VkApp app("Clogin", &argc, argv);
    PandoraWindow window("pandoraWindow");

    app.setMainWindow(&window);
    window.setTitle("Clogin Pandora ViewKit probe");
    window.show();
    app.run();
    return 0;
}
