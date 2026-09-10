#include "pandora_runtime.h"
#include "reconstruction/PandoraViewKit.h"

#include <Vk/VkApp.h>
#include <Xm/Xm.h>

#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <limits.h>

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

/*
 * Host portability seam for Xt app-default discovery.
 *
 * The shipped Clogin binary relies on its installed app-defaults resource
 * file for widget offsets, widths, labels, fonts, and Imd panel resources.
 * A login VT cannot be assumed to have an SGI/MaXX XFILESEARCHPATH, so the
 * reconstruction points Xt at the cleanroom runtime-resource copy before
 * VkApp initializes. CLOGIN_APP_DEFAULTS_DIR may override the location.
 */
void prepareCloginAppDefaults()
{
    const char *configured = std::getenv("CLOGIN_APP_DEFAULTS_DIR");
    if (configured && *configured)
    {
        setenv("XAPPLRESDIR", configured, 1);
        return;
    }

    if (access("app-defaults/Clogin", R_OK) != 0)
        return;

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        return;

    char resourceDir[PATH_MAX];
    const int written = std::snprintf(
        resourceDir,
        sizeof(resourceDir),
        "%s/app-defaults",
        cwd);
    if (written <= 0 || written >= static_cast<int>(sizeof(resourceDir)))
        return;

    setenv("XAPPLRESDIR", resourceDir, 1);
}

/*
 * Host-side cursor setup. The earlier portable Motif greeter explicitly
 * installed left_ptr after realization; the ViewKit runtime did not, leaving
 * the login VT with no visible pointer on some X servers. This is portability
 * glue only and is not claimed as original SGI Clogin implementation detail.
 */
void installPandoraCursor(Widget widget)
{
    if (!widget || !XtIsRealized(widget))
        return;

    Display *display = XtDisplay(widget);
    Window window = XtWindow(widget);
    if (!display || !window)
        return;

    Cursor cursor = None;
    XcursorImage *image = XcursorLibraryLoadImage("left_ptr", NULL, 24);
    if (image)
    {
        cursor = XcursorImageLoadCursor(display, image);
        XcursorImageDestroy(image);
    }

    if (cursor == None)
        cursor = XCreateFontCursor(display, XC_left_ptr);

    if (cursor != None)
    {
        XDefineCursor(display, window, cursor);
        XFreeCursor(display, cursor);
        XFlush(display);
    }
}

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

    prepareCloginAppDefaults();

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
    installPandoraCursor(window.baseWidget());
    app.run();

    return context.authenticated ? 1 : 0;
}
