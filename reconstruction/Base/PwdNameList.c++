#include "PwdNameList.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::vector<std::string> gIgnoreList;
std::vector<std::string> gShowAnywayList;
std::vector<std::string> gNoshowList;

static std::string trimLine(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}

static std::vector<std::string> splitColon(const std::string &line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = line.find(':', start);
        fields.push_back(line.substr(start, pos - start));
        if (pos == std::string::npos)
            break;
        start = pos + 1;
    }
    return fields;
}
}

PwdNameList::PwdNameList(const char *path)
{
    parse(path);
}

void PwdNameList::parse(const char *path)
{
    // Original constructor defaults recovered from MIPS stores at entry:
    // +0x0c = 0, +0x10 = 1, +0x14 = 1, +0x18 = 0.
    displayIcons_ = false;
    picturesAnywhere_ = true;
    homeAnywhere_ = true;
    largeWindow_ = false;

    if (path == nullptr)
        return;

    std::ifstream input(path);
    if (!input)
        return;

    std::string line;
    while (std::getline(input, line)) {
        line = trimLine(line);
        if (line.empty() || line[0] == '#')
            continue;

        const std::vector<std::string> fields = splitColon(line);
        if (fields.empty() || fields[0].empty())
            continue;

        const std::string &first = fields[0];

        // The original parser handles /hosts:ignore and /net:ignore before
        // ordinary entries and places the pseudo-name in getIgnoreList().
        if (first[0] == '/' && fields.size() > 1 && fields[1] == "ignore") {
            gIgnoreList.push_back(first);
            continue;
        }

        if (first.rfind("display ", 0) == 0) {
            if (first == "display faces")
                displayIcons_ = false;
            else if (first == "display icons")
                displayIcons_ = true;
            continue;
        }

        if (first.rfind("look ", 0) == 0) {
            if (first == "look for pictures locally")
                picturesAnywhere_ = false;
            else if (first == "look for pictures anywhere")
                picturesAnywhere_ = true;
            continue;
        }

        if (first.rfind("home ", 0) == 0) {
            if (first == "home directory must be local")
                homeAnywhere_ = false;
            else if (first == "home directory can be anywhere")
                homeAnywhere_ = true;
            continue;
        }

        if (first.rfind("use ", 0) == 0) {
            if (first == "use small window")
                largeWindow_ = false;
            else if (first == "use large window")
                largeWindow_ = true;
            continue;
        }

        PwdNameListEntry entry;
        entry.name = first;

        for (std::size_t i = 1; i < fields.size(); ++i) {
            const std::string &attr = fields[i];
            if (attr == "noshow") {
                entry.flags |= PwdNameListEntry::kNoShow;
                gNoshowList.push_back(entry.name);
            } else if (attr == "noicon") {
                entry.flags |= PwdNameListEntry::kNoIcon;
                gNoshowList.push_back(entry.name);
            } else if (attr == "show") {
                entry.flags |= PwdNameListEntry::kShow;
                gShowAnywayList.push_back(entry.name);
            } else if (attr == "nodelete") {
                entry.flags |= PwdNameListEntry::kNoDelete;
            } else if (attr == "machineicon") {
                entry.flags |= PwdNameListEntry::kMachineIcon;
            }
        }

        entries_.push_back(entry);
    }
}

const PwdNameListEntry *PwdNameList::findEntry(const char *name) const
{
    if (name == nullptr)
        return nullptr;

    const auto it = std::find_if(entries_.begin(), entries_.end(),
        [name](const PwdNameListEntry &entry) { return entry.name == name; });
    return it == entries_.end() ? nullptr : &*it;
}

const std::vector<std::string> &PwdNameList::getIgnoreList()
{
    return gIgnoreList;
}

const std::vector<std::string> &PwdNameList::getShowAnywayList()
{
    return gShowAnywayList;
}

const std::vector<std::string> &PwdNameList::getNoshowList()
{
    return gNoshowList;
}
