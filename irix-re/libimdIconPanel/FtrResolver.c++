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

struct TypeIcons
{
    std::vector<ImdString> resources;
};

typedef std::map<std::string, TypeIcons> TypeIconMap;

/*
 * The ICON rule is a small C/GL-like program. The old host resolver flattened
 * every include() it found in the rule, which incorrectly combined mutually
 * exclusive opened/selected/located branches. SGI documents inclusion order as
 * painter order: each later included picture is superimposed on the previous
 * one. Evaluate the branch structure for the normal closed/inactive icon state
 * so the ordered include stream matches what the Desktop would execute.
 *
 * Dynamic state-specific ICON re-evaluation is still a later seam; the renderer
 * currently expresses selected/located state through the FTR color/state path.
 */
class IconProgramParser
{
public:
    IconProgramParser(const std::string &program,
                      std::vector<std::string> &includes)
        : _program(program), _pos(0), _includes(includes)
    {
    }

    void run()
    {
        skipSpace();
        if (peek() == '{')
            parseBlock(true);
        else
            parseSequence(true, false);
    }

private:
    char peek() const
    {
        return _pos < _program.size() ? _program[_pos] : '\0';
    }

    void skipSpace()
    {
        while (_pos < _program.size() &&
               std::isspace(static_cast<unsigned char>(_program[_pos])))
            ++_pos;
    }

    bool wordAt(const char *word) const
    {
        const std::size_t n = std::strlen(word);
        if (_program.compare(_pos, n, word) != 0)
            return false;
        if (_pos + n < _program.size()) {
            const unsigned char c = static_cast<unsigned char>(_program[_pos + n]);
            if (std::isalnum(c) || c == '_')
                return false;
        }
        return true;
    }

    std::string parenthesized()
    {
        skipSpace();
        if (peek() != '(')
            return std::string();
        ++_pos;
        const std::size_t start = _pos;
        int depth = 1;
        while (_pos < _program.size() && depth > 0) {
            if (_program[_pos] == '(')
                ++depth;
            else if (_program[_pos] == ')')
                --depth;
            if (depth > 0)
                ++_pos;
        }
        const std::size_t end = _pos;
        if (peek() == ')')
            ++_pos;
        return _program.substr(start, end - start);
    }

    static std::string compact(const std::string &s)
    {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (!std::isspace(static_cast<unsigned char>(s[i])))
                out += s[i];
        return out;
    }

    static bool baselineVariable(const std::string &name)
    {
        if (name == "true")
            return true;
        if (name == "false")
            return false;
        if (name == "current" || name == "disabled" ||
            name == "opened" || name == "located" || name == "selected")
            return false;
        return false;
    }

    static std::size_t findTopLevel(const std::string &s, const char *op)
    {
        const std::size_t n = std::strlen(op);
        int depth = 0;
        for (std::size_t i = 0; i + n <= s.size(); ++i) {
            if (s[i] == '(')
                ++depth;
            else if (s[i] == ')')
                --depth;
            if (depth == 0 && s.compare(i, n, op) == 0)
                return i;
        }
        return std::string::npos;
    }

    static bool evalCondition(std::string expr)
    {
        expr = compact(expr);
        while (expr.size() >= 2 && expr[0] == '(' && expr[expr.size() - 1] == ')')
            expr = expr.substr(1, expr.size() - 2);

        std::size_t p = findTopLevel(expr, "||");
        if (p != std::string::npos)
            return evalCondition(expr.substr(0, p)) || evalCondition(expr.substr(p + 2));
        p = findTopLevel(expr, "&&");
        if (p != std::string::npos)
            return evalCondition(expr.substr(0, p)) && evalCondition(expr.substr(p + 2));
        p = findTopLevel(expr, "==");
        if (p != std::string::npos)
            return evalCondition(expr.substr(0, p)) == evalCondition(expr.substr(p + 2));
        p = findTopLevel(expr, "!=");
        if (p != std::string::npos)
            return evalCondition(expr.substr(0, p)) != evalCondition(expr.substr(p + 2));
        if (!expr.empty() && expr[0] == '!')
            return !evalCondition(expr.substr(1));
        return baselineVariable(expr);
    }

    void parseInclude(bool active)
    {
        _pos += 7;
        skipSpace();
        if (peek() != '(') {
            skipStatement();
            return;
        }
        ++_pos;
        skipSpace();
        if (peek() != '"') {
            skipStatement();
            return;
        }
        ++_pos;
        const std::size_t start = _pos;
        while (_pos < _program.size() && _program[_pos] != '"')
            ++_pos;
        const std::string path = _program.substr(start, _pos - start);
        if (peek() == '"')
            ++_pos;
        while (_pos < _program.size() && _program[_pos] != ';' && _program[_pos] != '}')
            ++_pos;
        if (peek() == ';')
            ++_pos;
        if (active && !path.empty())
            _includes.push_back(path);
    }

    void skipStatement()
    {
        int parenDepth = 0;
        while (_pos < _program.size()) {
            const char c = _program[_pos];
            if (c == '(')
                ++parenDepth;
            else if (c == ')' && parenDepth > 0)
                --parenDepth;
            else if (c == ';' && parenDepth == 0) {
                ++_pos;
                return;
            } else if ((c == '{' || c == '}') && parenDepth == 0)
                return;
            ++_pos;
        }
    }

    void parseIf(bool active)
    {
        _pos += 2;
        const bool condition = evalCondition(parenthesized());
        parseStatement(active && condition);
        skipSpace();
        if (wordAt("else")) {
            _pos += 4;
            parseStatement(active && !condition);
        }
    }

    void parseStatement(bool active)
    {
        skipSpace();
        if (_pos >= _program.size())
            return;
        if (peek() == '{') {
            parseBlock(active);
            return;
        }
        if (wordAt("if")) {
            parseIf(active);
            return;
        }
        if (wordAt("include")) {
            parseInclude(active);
            return;
        }
        skipStatement();
    }

    void parseBlock(bool active)
    {
        if (peek() == '{')
            ++_pos;
        parseSequence(active, true);
    }

    void parseSequence(bool active, bool stopAtBrace)
    {
        while (_pos < _program.size()) {
            skipSpace();
            if (_pos >= _program.size())
                return;
            if (stopAtBrace && peek() == '}') {
                ++_pos;
                return;
            }
            parseStatement(active);
        }
    }

    const std::string &_program;
    std::size_t _pos;
    std::vector<std::string> &_includes;
};

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
    std::string iconProgram;
    std::string line;

    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;

        if (t.compare(0, 5, "TYPE ") == 0) {
            currentType = trim(t.substr(5));
            inIcon = false;
            braceDepth = 0;
            iconProgram.clear();
            continue;
        }

        if (currentType.empty())
            continue;

        if (!inIcon) {
            if (t.compare(0, 4, "ICON") == 0) {
                const std::string::size_type open = t.find('{');
                if (open != std::string::npos) {
                    inIcon = true;
                    braceDepth = 0;
                    iconProgram = t.substr(open);
                    for (std::size_t i = open; i < t.size(); ++i) {
                        if (t[i] == '{')
                            ++braceDepth;
                        else if (t[i] == '}')
                            --braceDepth;
                    }
                }
            }
            if (!inIcon || braceDepth > 0)
                continue;
        } else {
            iconProgram += '\n';
            iconProgram += t;
            for (std::size_t i = 0; i < t.size(); ++i) {
                if (t[i] == '{')
                    ++braceDepth;
                else if (t[i] == '}')
                    --braceDepth;
            }
            if (braceDepth > 0)
                continue;
        }

        if (map.find(currentType) == map.end()) {
            std::vector<std::string> includes;
            IconProgramParser(iconProgram, includes).run();

            TypeIcons ti;
            const std::string base = dirnameOf(relative);
            std::set<std::string> seen;
            for (std::size_t i = 0; i < includes.size(); ++i) {
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
        iconProgram.clear();
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
