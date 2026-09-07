#include "FtrResolver.h"

#include "FileModel.h"
#include "FtrMatchEvaluator.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
static const char *const kDefaultDB = "/usr/lib/filetype/desktop.otr";

static std::string trim(const std::string &s)
{
    const std::string::size_type a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    const std::string::size_type b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool hasCoreFtr(const std::string &root)
{
    std::ifstream in((root + "/default/sgidefault.ftr").c_str());
    return in.good();
}

static std::string filetypeRoot()
{
    const char *env = std::getenv("SGI_FILETYPE_DIR");
    if (env && *env && hasCoreFtr(env))
        return env;

    const char *irixRoot = std::getenv("IRIX_ROOT");
    if (irixRoot && *irixRoot) {
        const std::string root = std::string(irixRoot) + "/usr/lib/filetype";
        if (hasCoreFtr(root))
            return root;
    }

    static const char *const roots[] = {
        "/usr/lib/filetype",
        "/usr/share/sgi/filetype",
        "/opt/MaXX/usr/lib/filetype",
        "/opt/MaXX/share/filetype",
        0
    };
    for (int i = 0; roots[i]; ++i)
        if (hasCoreFtr(roots[i]))
            return roots[i];

    return std::string();
}

static bool exists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static bool containsEntry(const std::string &dir, const char *name)
{
    return exists(dir + "/" + name);
}

static bool readPrefix(const std::string &path, std::vector<unsigned char> &data)
{
    data.clear();
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.good())
        return false;

    char buf[4096];
    in.read(buf, sizeof(buf));
    const std::streamsize n = in.gcount();
    if (n <= 0)
        return true;

    data.assign(reinterpret_cast<unsigned char *>(buf),
                reinterpret_cast<unsigned char *>(buf) + n);
    return true;
}

static bool looksAscii(const std::vector<unsigned char> &data)
{
    for (std::size_t i = 0; i < data.size(); ++i) {
        const unsigned char c = data[i];
        if (c == 0)
            return false;
        if (c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\b')
            continue;
        if (c >= 0x20 && c < 0x7f)
            continue;
        return false;
    }
    return true;
}

static unsigned short read16(const std::vector<unsigned char> &d,
                             std::size_t off,
                             bool bigEndian)
{
    if (off + 1 >= d.size())
        return 0;
    if (bigEndian)
        return static_cast<unsigned short>((d[off] << 8) | d[off + 1]);
    return static_cast<unsigned short>(d[off] | (d[off + 1] << 8));
}

static bool elfHasProgramHeaders(const std::vector<unsigned char> &d)
{
    if (d.size() < 6 || d[0] != 0x7f || d[1] != 'E' || d[2] != 'L' || d[3] != 'F')
        return false;

    const bool bigEndian = d[5] == 2;
    if (d[4] == 1)
        return read16(d, 44, bigEndian) > 0;
    if (d[4] == 2)
        return read16(d, 56, bigEndian) > 0;
    return false;
}

static std::string classifyCore(const ImdDNA &dna, const std::string &path)
{
    /*
     * Compatibility fallback only. The primary path executes the shipped FTR
     * source rules. Directory model identity remains useful when a reference
     * path cannot be stat()ed locally.
     */
    if (dna.type() == "ImdDirModel") {
        if (containsEntry(path, ".dumpster"))
            return "Dumpster";
        return "Directory";
    }

    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return "Unknown";

    if (S_ISDIR(st.st_mode)) {
        if (containsEntry(path, ".dumpster"))
            return "Dumpster";
        return "Directory";
    }
    if (S_ISCHR(st.st_mode))
        return "CharSpecial";
    if (S_ISBLK(st.st_mode))
        return "BlockSpecial";
    if (S_ISFIFO(st.st_mode))
        return "NamedPipe";
    if (!S_ISREG(st.st_mode))
        return "UnknownSpecial";

    if (st.st_size == 0)
        return "EmptyFile";

    std::vector<unsigned char> data;
    readPrefix(path, data);
    const bool ascii = looksAscii(data);

    if (elfHasProgramHeaders(data) ||
        (((st.st_mode & 0400) == 0) && (st.st_mode & 0111) != 0))
        return "GenericExecutable";

    if ((st.st_mode & 0111) != 0 && ascii)
        return "Script";

    return ascii ? "AsciiTextFile" : "BinaryDataFile";
}

static std::string dirnameOf(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return std::string();
    return path.substr(0, slash);
}

static std::string normalizeRelative(const std::string &base,
                                     const std::string &include)
{
    std::vector<std::string> parts;
    std::string combined = base.empty() ? include : base + "/" + include;
    std::string part;
    for (std::size_t i = 0; i <= combined.size(); ++i) {
        if (i == combined.size() || combined[i] == '/') {
            if (part.empty() || part == ".") {
                part.clear();
                continue;
            }
            if (part == "..") {
                if (!parts.empty())
                    parts.pop_back();
            } else {
                parts.push_back(part);
            }
            part.clear();
        } else {
            part += combined[i];
        }
    }

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += '/';
        out += parts[i];
    }
    return out;
}

static std::string quotedInclude(const std::string &line)
{
    const std::string::size_type call = line.find("include(");
    if (call == std::string::npos)
        return std::string();
    const std::string::size_type q1 = line.find('"', call);
    if (q1 == std::string::npos)
        return std::string();
    const std::string::size_type q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return std::string();
    return line.substr(q1 + 1, q2 - q1 - 1);
}

struct TypeIcons
{
    std::vector<ImdString> resources;
};

typedef std::map<std::string, TypeIcons> TypeIconMap;

static void parseFtrFile(const std::string &root,
                         const std::string &relative,
                         TypeIconMap &map)
{
    std::ifstream in((root + "/" + relative).c_str());
    if (!in.good())
        return;

    std::string currentType;
    bool inIcon = false;
    int braceDepth = 0;
    std::vector<std::string> includes;
    std::string line;

    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;

        if (t.compare(0, 5, "TYPE ") == 0) {
            currentType = trim(t.substr(5));
            inIcon = false;
            braceDepth = 0;
            includes.clear();
            continue;
        }

        if (currentType.empty())
            continue;

        if (!inIcon) {
            if (t.compare(0, 4, "ICON") == 0 && t.find('{') != std::string::npos) {
                inIcon = true;
                braceDepth = 1;
                includes.clear();
            }
            continue;
        }

        const std::string inc = quotedInclude(t);
        if (!inc.empty())
            includes.push_back(inc);

        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '{')
                ++braceDepth;
            else if (t[i] == '}')
                --braceDepth;
        }

        if (braceDepth <= 0) {
            if (map.find(currentType) == map.end()) {
                TypeIcons ti;
                const std::string base = dirnameOf(relative);
                std::set<std::string> seen;
                for (std::size_t i = 0; i < includes.size(); ++i) {
                    if (includes[i].find(".open.fti") != std::string::npos)
                        continue;
                    const std::string normalized = normalizeRelative(base, includes[i]);
                    if (normalized.empty() || !seen.insert(normalized).second)
                        continue;
                    if (exists(root + "/" + normalized))
                        ti.resources.push_back(ImdString(normalized));
                }
                map[currentType] = ti;
            }
            inIcon = false;
            braceDepth = 0;
            includes.clear();
        }
    }
}

static void scanFtrDir(const std::string &root,
                       const std::string &relativeDir,
                       TypeIconMap &map)
{
    const std::string full = root + (relativeDir.empty() ? "" : "/" + relativeDir);
    DIR *dir = opendir(full.c_str());
    if (!dir)
        return;

    std::vector<std::string> names;
    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..")
            continue;
        names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());

    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string rel = relativeDir.empty()
            ? names[i] : relativeDir + "/" + names[i];
        const std::string path = root + "/" + rel;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            scanFtrDir(root, rel, map);
        } else if (S_ISREG(st.st_mode) &&
                   names[i].size() > 4 &&
                   names[i].compare(names[i].size() - 4, 4, ".ftr") == 0) {
            parseFtrFile(root, rel, map);
        }
    }
}

static const TypeIconMap &typeIcons()
{
    static TypeIconMap map;
    static std::string cachedRoot;
    static bool initialized = false;

    const std::string root = filetypeRoot();
    if (!initialized || root != cachedRoot) {
        initialized = true;
        cachedRoot = root;
        map.clear();

        if (!root.empty()) {
            static const char *const dirs[] = {
                "local", "install", "system", "default", 0
            };
            for (int i = 0; dirs[i]; ++i)
                scanFtrDir(root, dirs[i], map);
        }
    }
    return map;
}
}

ImdDNA
ImdPortableFtrTypeDNAForFile(const ImdDNA &fileDNA)
{
    const ImdString pathString = ImdFileModel::fileNameFromDNA(fileDNA);
    const std::string path = pathString.stdString();
    std::string type;

    const std::string root = filetypeRoot();
    if (!root.empty() &&
        ImdEvaluateFtrType(root, path, fileDNA.type() == "ImdDirModel", type)) {
        return ImdDNA(ImdString(type), ImdString(kDefaultDB));
    }

    type = classifyCore(fileDNA, path);
    return ImdDNA(ImdString(type), ImdString(kDefaultDB));
}

bool
ImdPortableFtrIconResources(const ImdDNA &typeDNA,
                            std::vector<ImdString> &resources)
{
    resources.clear();

    const TypeIconMap &map = typeIcons();
    TypeIconMap::const_iterator it = map.find(typeDNA.type().stdString());
    if (it != map.end() && !it->second.resources.empty()) {
        resources = it->second.resources;
        return true;
    }

    if (typeDNA.type() == "Directory" || typeDNA.type() == "Dumpster") {
        resources.push_back(ImdString("iconlib/generic.folder.closed.fti"));
        return true;
    }
    if (typeDNA.type() != "") {
        resources.push_back(ImdString("iconlib/generic.doc.fti"));
        return true;
    }

    return false;
}
