#include <Vk/VkApp.h>
#include <Vk/VkSimpleWindow.h>
#include <Xm/Label.h>

class ViewKitProbeWindow : public VkSimpleWindow
{
public:
    ViewKitProbeWindow(const char *name)
        : VkSimpleWindow(name)
    {
        Widget label = XtVaCreateManagedWidget(
            "probeLabel",
            xmLabelWidgetClass,
            mainWindowWidget(),
            XtVaTypedArg, XmNlabelString, XmRString,
            "ViewKlass is alive", 19,
            NULL);

        addView(label);
    }

    virtual ~ViewKitProbeWindow() {}

    virtual const char *className()
    {
        return "ViewKitProbeWindow";
    }
};

int main(int argc, char **argv)
{
    VkApp app("ViewKitProbe", &argc, argv);
    ViewKitProbeWindow window("viewkitProbe");

    app.setMainWindow(&window);
    window.show();
    app.run();
    return 0;
}
