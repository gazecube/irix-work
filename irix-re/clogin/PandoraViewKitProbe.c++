#include "PandoraViewKit.h"

#include <Vk/VkApp.h>

int main(int argc, char **argv)
{
    VkApp app("Clogin", &argc, argv);
    PandoraWindow window("pandoraWindow");

    app.setMainWindow(&window);
    window.setTitle("Clogin Pandora ViewKit probe");
    window.show();
    app.run();
    return 0;
}
