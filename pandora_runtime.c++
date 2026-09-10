#include "pandora_runtime.h"
#include "reconstruction/PandoraViewKit.h"

#include <Vk/VkApp.h>
#include <Xm/Xm.h>

#include <cstdlib>
#include <cstring>

extern "C" int authenticateLogin(char *username, char *password);

namespace
{
struct PandoraRuntimeContext
{
    char *usernameOut;
    size_t usernameLen;
    Widget exitWidget;
    bool authenticated;
};

void pandoraHostLoginHandoff(
    const char *username,
    const char *password,
    bool,
    XtPointer clientData)
{
    PandoraRuntimeContext *context =
        static_cast<PandoraRuntimeContext *>(clientData);
    if (!context || !username || !*username || !context->usernameOut ||
        context->usernameLen == 0)
        return;

    char *mutableUser = ::strdup(username);
    char *mutablePassword = ::strdup(password ? password : "");
    if (!mutableUser || !mutablePassword)
    {
        std::free(mutableUser);
        std::free(mutablePassword);
        return;
    }

    if (authenticateLogin(mutableUser, mutablePassword))
    {
        std::strncpy(context->usernameOut, username, context->usernameLen - 1);
        context->usernameOut[context->usernameLen - 1] = '\0';
        context->authenticated = true;

        if (context->exitWidget)
            XtAppSetExitFlag(XtWidgetToApplicationContext(context->exitWidget));
    }

    std::memset(mutablePassword, 0, std::strlen(mutablePassword));
    std::free(mutablePassword);
    std::free(mutableUser);
}
}

extern "C" int run_pandora_gui(
    int argc,
    char **argv,
    char *username_out,
    size_t username_len)
{
    if (!username_out || username_len == 0)
        return 0;

    username_out[0] = '\0';

    VkApp app("Clogin", &argc, argv);
    PandoraWindow window("pandoraWindow");
    PandoraRuntimeContext context;
    context.usernameOut = username_out;
    context.usernameLen = username_len;
    context.exitWidget = window.baseWidget();
    context.authenticated = false;

    app.setMainWindow(&window);
    if (window.component())
        window.component()->setLoginHandoff(&pandoraHostLoginHandoff, &context);

    window.setTitle("Clogin");
    window.show();
    app.run();

    return context.authenticated ? 1 : 0;
}
