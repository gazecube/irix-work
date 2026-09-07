#ifndef CLOGIN_PWDNAMELIST_H
#define CLOGIN_PWDNAMELIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PwdNameListEntry
{
    enum Flags : std::uint32_t
    {
        kNoShow      = 0x01,
        kNoIcon      = 0x02,
        kNoDelete    = 0x04,
        kMachineIcon = 0x08,
        kShow        = 0x10
    };

    std::string name;
    std::uint32_t flags = 0;
};

class PwdNameList
{
public:
    explicit PwdNameList(const char *path);

    const PwdNameListEntry *findEntry(const char *name) const;

    // These names are present in the original clogin dynamic symbol table.
    static const std::vector<std::string> &getIgnoreList();
    static const std::vector<std::string> &getShowAnywayList();
    static const std::vector<std::string> &getNoshowList();

    const std::vector<PwdNameListEntry> &entries() const { return entries_; }

    // Reconstructed semantic accessors. The original object stores the
    // opposite-mode booleans: display-icons, pictures-anywhere,
    // home-anywhere, and large-window.
    bool displayFaces() const { return !displayIcons_; }
    bool lookForPicturesLocally() const { return !picturesAnywhere_; }
    bool homeDirectoryMustBeLocal() const { return !homeAnywhere_; }
    bool useSmallWindow() const { return !largeWindow_; }

private:
    void parse(const char *path);

    std::vector<PwdNameListEntry> entries_;
    bool displayIcons_ = false;
    bool picturesAnywhere_ = true;
    bool homeAnywhere_ = true;
    bool largeWindow_ = false;
};

#endif
