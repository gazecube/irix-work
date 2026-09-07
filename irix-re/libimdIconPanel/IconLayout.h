#ifndef IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H
#define IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H

#include "ImdIconPanel.h"

#include <map>
#include <string>

/*
 * ImdIconLayout
 *
 * Public names/signatures below are recovered from the original
 * libimdIconPanel.so symbol table. The portable implementation keeps only
 * state whose meaning is directly corroborated by binary/resource evidence.
 */
class ImdIconLayout : public ImdLayout
{
public:
    explicit ImdIconLayout(Widget widget);
    virtual ~ImdIconLayout();

    /* Original exported symbol @ 0x0969d1fc. */
    virtual bool moveView(ImdView &view, long x, long y);

    void setSnapToGrid(bool value);
    void truncate(bool value);
    void setScrollIncrement();

    bool portableSnapToGrid() const { return _snapToGrid; }
    int portableGridWidth() const { return _gridWidth; }
    int portableStaggerOffset() const { return _staggerOffset; }

    /*
     * Host-side persistence seam.
     *
     * Original libimdIconPanel.so has the binary-proven mapping API:
     *   addMapping(const ImdString &, long, long, ImdAlign)
     *   getMapping(ImdView &, long &, long &, ImdAlign &)
     *   applyMappings(ImdView &)
     *   loadMappings(ImdInitFile &)
     *   saveMappings(ImdInitFile &)
     *
     * Original fm's ImdBackWindow::restorePresentationState() and
     * savePresentationState() call ImdViewPanelLoadMappings()/SaveMappings(),
     * and the shipped state keys include "FmState" and "IconMappings".
     *
     * Until ImdInitFile itself is reconstructed, these helpers preserve the
     * same source-level mapping concept in a separate RE cache file. They do
     * not claim the original ImdInitFile on-disk encoding.
     */
    bool portableLoadMappings(const char *path);
    bool portableSaveMappings() const;
    bool portableApplyMapping(ImdView &view) const;

private:
    struct PortableMapping
    {
        long x;
        long y;
        int align;

        PortableMapping(long px = 0, long py = 0, int pa = 0)
            : x(px), y(py), align(pa) {}
    };

    std::string portableMappingName(const ImdView &view) const;
    void portableRememberView(const ImdView &view);

    bool _snapToGrid;
    bool _truncateRequested;

    int _gridWidth;
    int _staggerOffset;
    int _minimumRightIconPadding;
    int _baseOfIconPadding;
    int _topMargin;
    int _bottomMargin;
    int _leftMargin;
    int _rightMargin;
    int _overlapLimit;
    int _minimumIconWidth;
    int _maximumIconWidth;

    std::string _portableMappingPath;
    std::map<std::string, PortableMapping> _portableMappings;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H */
