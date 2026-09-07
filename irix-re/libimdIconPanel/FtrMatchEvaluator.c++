#include "FtrMatchEvaluator.h"

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Rule
{
    std::string type;
    std::string expression;
    bool specialFile;
};

enum ValueKind { ValueInteger, ValueString, ValueInvalid };

struct Value
{
    ValueKind kind;
    int64_t integer;
    std::string string;

    Value() : kind(ValueInvalid), integer(0) {}

    static Value fromInteger(int64_t v)
    {
        Value out;
        out.kind = ValueInteger;
        out.integer = v;
        return out;
    }

    static Value fromString(const std::string &v)
    {
        Value out;
        out.kind = ValueString;
        out.string = v;
        return out;
    }

    bool truth() const
    {
        if (kind == ValueString)
            return !string.empty();
        if (kind == ValueInteger)
            return integer != 0;
        return false;
    }
};

static std::string trim(const std::string &s)
{
    const std::string::size_type a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    const std::string::size_type b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string basenameOf(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool readFile(const std::string &path, std::vector<unsigned char> &data)
{
    data.clear();
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.good())
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(end));
    if (!data.empty())
        in.read(reinterpret_cast<char *>(&data[0]), data.size());
    return in.good() || in.eof();
}

static bool looksAscii512(const std::vector<unsigned char> &data)
{
    const std::size_t limit = std::min<std::size_t>(512, data.size());
    for (std::size_t i = 0; i < limit; ++i) {
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

static uint64_t readUnsignedBE(const std::vector<unsigned char> &data,
                               std::size_t off,
                               std::size_t width,
                               bool &ok)
{
    if (width == 0 || width > 8 || off > data.size() || width > data.size() - off) {
        ok = false;
        return 0;
    }
    uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value = (value << 8) | data[off + i];
    ok = true;
    return value;
}

static int64_t signExtend(uint64_t value, unsigned bits)
{
    if (bits == 0 || bits >= 64)
        return static_cast<int64_t>(value);
    const uint64_t sign = uint64_t(1) << (bits - 1);
    if (value & sign)
        value |= (~uint64_t(0)) << bits;
    return static_cast<int64_t>(value);
}

static void expandBraces(const std::string &pattern, std::vector<std::string> &out)
{
    std::string::size_type left = std::string::npos;
    bool escaped = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (pattern[i] == '\\') {
            escaped = true;
            continue;
        }
        if (pattern[i] == '{') {
            left = i;
            break;
        }
    }
    if (left == std::string::npos) {
        out.push_back(pattern);
        return;
    }

    int depth = 0;
    std::string::size_type right = std::string::npos;
    for (std::size_t i = left; i < pattern.size(); ++i) {
        if (pattern[i] == '{')
            ++depth;
        else if (pattern[i] == '}' && --depth == 0) {
            right = i;
            break;
        }
    }
    if (right == std::string::npos) {
        out.push_back(pattern);
        return;
    }

    const std::string prefix = pattern.substr(0, left);
    const std::string suffix = pattern.substr(right + 1);
    const std::string body = pattern.substr(left + 1, right - left - 1);
    std::size_t start = 0;
    depth = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i < body.size() && body[i] == '{')
            ++depth;
        else if (i < body.size() && body[i] == '}')
            --depth;
        if (i == body.size() || (body[i] == ',' && depth == 0)) {
            expandBraces(prefix + body.substr(start, i - start) + suffix, out);
            start = i + 1;
        }
    }
}

static bool shellPatternMatch(const std::string &pattern, const std::string &subject)
{
    std::vector<std::string> expanded;
    expandBraces(pattern, expanded);
    for (std::size_t i = 0; i < expanded.size(); ++i)
        if (fnmatch(expanded[i].c_str(), subject.c_str(), 0) == 0)
            return true;
    return false;
}

static bool wildcardMatch(const std::string &pattern, const std::string &path)
{
    return shellPatternMatch(pattern, basenameOf(path));
}

static bool dirContains(const std::string &path, const std::string &pattern)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return false;

    DIR *dir = opendir(path.c_str());
    if (!dir)
        return false;
    bool found = false;
    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
            continue;
        if (shellPatternMatch(pattern, entry->d_name)) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

static uint16_t read16At(const std::vector<unsigned char> &data,
                         std::size_t off,
                         bool bigEndian)
{
    if (off + 2 > data.size())
        return 0;
    return bigEndian
        ? static_cast<uint16_t>((uint16_t(data[off]) << 8) | data[off + 1])
        : static_cast<uint16_t>(data[off] | (uint16_t(data[off + 1]) << 8));
}

static uint32_t read32At(const std::vector<unsigned char> &data,
                         std::size_t off,
                         bool bigEndian)
{
    if (off + 4 > data.size())
        return 0;
    if (bigEndian)
        return (uint32_t(data[off]) << 24) | (uint32_t(data[off + 1]) << 16) |
               (uint32_t(data[off + 2]) << 8) | data[off + 3];
    return uint32_t(data[off]) | (uint32_t(data[off + 1]) << 8) |
           (uint32_t(data[off + 2]) << 16) | (uint32_t(data[off + 3]) << 24);
}

/* IRIX 6.5.7m FTR documentation specifies -1 for an untagged file. */
static int64_t executableTag(const std::vector<unsigned char> &data)
{
    if (data.size() >= 52 && data[0] == 0x7f && data[1] == 'E' &&
        data[2] == 'L' && data[3] == 'F' && data[4] == 1 &&
        (data[5] == 1 || data[5] == 2)) {
        const bool be = data[5] == 2;
        const uint32_t shoff = read32At(data, 32, be);
        const uint16_t shentsize = read16At(data, 46, be);
        const uint16_t shnum = read16At(data, 48, be);
        if (shoff && shentsize >= 40) {
            for (uint16_t i = 0; i < shnum; ++i) {
                const std::size_t sh = std::size_t(shoff) + std::size_t(i) * shentsize;
                if (sh + 40 > data.size())
                    break;
                if (read32At(data, sh + 4, be) != 0x70000006u)
                    continue;
                const uint32_t off = read32At(data, sh + 16, be);
                const uint32_t size = read32At(data, sh + 20, be);
                if (off <= data.size() && size >= 12 && size <= data.size() - off &&
                    read32At(data, off + 4, be) != 0)
                    return static_cast<int64_t>(read32At(data, off + 8, be));
                return -1;
            }
        }
    }

    if (data.size() >= 3 && data[0] == '#' && data[1] == '!') {
        std::size_t p = 0;
        while (p < data.size() && data[p] != '\n')
            ++p;
        if (p < data.size())
            ++p;
        static const char prefix[] = "#Tag ";
        if (p + sizeof(prefix) - 1 <= data.size() &&
            std::memcmp(&data[p], prefix, sizeof(prefix) - 1) == 0) {
            p += sizeof(prefix) - 1;
            std::string number;
            while (p < data.size() && data[p] != '\r' && data[p] != '\n')
                number += static_cast<char>(data[p++]);
            char *end = 0;
            const unsigned long value = std::strtoul(number.c_str(), &end, 0);
            if (end && end != number.c_str())
                return static_cast<int64_t>(static_cast<uint32_t>(value));
        }
    }
    return -1;
}

enum TokenKind
{
    TokEnd, TokNumber, TokString, TokIdent, TokOp,
    TokLParen, TokRParen, TokComma
};

struct Token
{
    TokenKind kind;
    std::string text;
    uint64_t number;
};

class Lexer
{
public:
    explicit Lexer(const std::string &input) : _input(input), _pos(0) {}

    Token next()
    {
        while (_pos < _input.size() &&
               std::isspace(static_cast<unsigned char>(_input[_pos])))
            ++_pos;
        if (_pos >= _input.size())
            return make(TokEnd, "");

        const char c = _input[_pos];
        if (c == '(') { ++_pos; return make(TokLParen, "("); }
        if (c == ')') { ++_pos; return make(TokRParen, ")"); }
        if (c == ',') { ++_pos; return make(TokComma, ","); }

        if (c == '"') {
            ++_pos;
            std::string out;
            bool closed = false;
            while (_pos < _input.size()) {
                char ch = _input[_pos++];
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\' && _pos < _input.size()) {
                    const char e = _input[_pos++];
                    if (e == 'n') out += '\n';
                    else if (e == 'r') out += '\r';
                    else if (e == 't') out += '\t';
                    else out += e;
                } else {
                    out += ch;
                }
            }
            Token t = make(TokString, out);
            if (!closed)
                t.kind = TokOp;
            return t;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t start = _pos;
            if (c == '0' && _pos + 1 < _input.size() &&
                (_input[_pos + 1] == 'x' || _input[_pos + 1] == 'X')) {
                _pos += 2;
                while (_pos < _input.size() &&
                       std::isxdigit(static_cast<unsigned char>(_input[_pos])))
                    ++_pos;
            } else {
                while (_pos < _input.size() &&
                       std::isdigit(static_cast<unsigned char>(_input[_pos])))
                    ++_pos;
            }
            Token t = make(TokNumber, _input.substr(start, _pos - start));
            char *end = 0;
            t.number = std::strtoull(t.text.c_str(), &end, 0);
            return t;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t start = _pos++;
            while (_pos < _input.size()) {
                const char ch = _input[_pos];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
                    break;
                ++_pos;
            }
            return make(TokIdent, _input.substr(start, _pos - start));
        }

        static const char *const two[] = {
            "||", "&&", "==", "!=", "<=", ">=", "<<", ">>", 0
        };
        for (int i = 0; two[i]; ++i) {
            if (_input.compare(_pos, 2, two[i]) == 0) {
                _pos += 2;
                return make(TokOp, two[i]);
            }
        }
        if (std::strchr("!~<>|&^+-*/%", c)) {
            ++_pos;
            return make(TokOp, std::string(1, c));
        }

        ++_pos;
        return make(TokOp, std::string(1, c));
    }

private:
    Token make(TokenKind kind, const std::string &text) const
    {
        Token t;
        t.kind = kind;
        t.text = text;
        t.number = 0;
        return t;
    }

    const std::string &_input;
    std::size_t _pos;
};

class Parser
{
public:
    Parser(const std::string &expression,
           const std::string &path,
           const struct stat &st,
           const std::vector<unsigned char> &data)
        : _lex(expression), _path(path), _st(st), _data(data), _ok(true),
          _skipEvaluation(false)
    {
        _tok = _lex.next();
    }

    bool evaluate(bool &result)
    {
        Value v = parseLogicalOr();
        if (!_ok || v.kind == ValueInvalid || _tok.kind != TokEnd)
            return false;
        result = v.truth();
        return true;
    }

private:
    void consume() { _tok = _lex.next(); }
    bool isOp(const char *s) const { return _tok.kind == TokOp && _tok.text == s; }

    Value parseLogicalOr()
    {
        Value left = parseLogicalAnd();
        while (isOp("||")) {
            consume();
            const bool shortCircuit = !_skipEvaluation && left.truth();
            const bool oldSkip = _skipEvaluation;
            if (shortCircuit)
                _skipEvaluation = true;
            Value right = parseLogicalAnd();
            _skipEvaluation = oldSkip;
            left = Value::fromInteger(left.truth() || right.truth());
        }
        return left;
    }

    Value parseLogicalAnd()
    {
        Value left = parseBitOr();
        while (isOp("&&")) {
            consume();
            const bool shortCircuit = !_skipEvaluation && !left.truth();
            const bool oldSkip = _skipEvaluation;
            if (shortCircuit)
                _skipEvaluation = true;
            Value right = parseBitOr();
            _skipEvaluation = oldSkip;
            left = Value::fromInteger(left.truth() && right.truth());
        }
        return left;
    }

    Value parseBitOr()
    {
        Value left = parseBitXor();
        while (isOp("|")) {
            consume();
            left = integerBinary(left, parseBitXor(), '|');
        }
        return left;
    }

    Value parseBitXor()
    {
        Value left = parseBitAnd();
        while (isOp("^")) {
            consume();
            left = integerBinary(left, parseBitAnd(), '^');
        }
        return left;
    }

    Value parseBitAnd()
    {
        Value left = parseEquality();
        while (isOp("&")) {
            consume();
            left = integerBinary(left, parseEquality(), '&');
        }
        return left;
    }

    Value parseEquality()
    {
        Value left = parseRelational();
        while (isOp("==") || isOp("!=")) {
            const bool neq = isOp("!=");
            consume();
            Value right = parseRelational();
            bool equal = false;
            if (left.kind == ValueString && right.kind == ValueString)
                equal = left.string == right.string;
            else if (left.kind == ValueInteger && right.kind == ValueInteger)
                equal = left.integer == right.integer;
            else {
                _ok = false;
                return Value();
            }
            left = Value::fromInteger(neq ? !equal : equal);
        }
        return left;
    }

    Value parseRelational()
    {
        Value left = parseShift();
        while (isOp("<") || isOp("<=") || isOp(">") || isOp(">=")) {
            const std::string op = _tok.text;
            consume();
            Value right = parseShift();
            if (left.kind != right.kind || left.kind == ValueInvalid) {
                _ok = false;
                return Value();
            }
            bool matched;
            if (left.kind == ValueString) {
                matched = op == "<" ? left.string < right.string :
                          op == "<=" ? left.string <= right.string :
                          op == ">" ? left.string > right.string : left.string >= right.string;
            } else {
                matched = op == "<" ? left.integer < right.integer :
                          op == "<=" ? left.integer <= right.integer :
                          op == ">" ? left.integer > right.integer : left.integer >= right.integer;
            }
            left = Value::fromInteger(matched);
        }
        return left;
    }

    Value parseShift()
    {
        Value left = parseAdditive();
        while (isOp("<<") || isOp(">>")) {
            const bool rightShift = isOp(">>");
            consume();
            Value right = parseAdditive();
            if (!requireIntegers(left, right))
                return Value();
            if (right.integer < 0 || right.integer >= 64) {
                if (!_skipEvaluation)
                    _ok = false;
                return Value();
            }
            left = Value::fromInteger(rightShift ? left.integer >> right.integer
                                                 : left.integer << right.integer);
        }
        return left;
    }

    Value parseAdditive()
    {
        Value left = parseMultiplicative();
        while (isOp("+") || isOp("-")) {
            const bool subtract = isOp("-");
            consume();
            Value right = parseMultiplicative();
            if (!requireIntegers(left, right))
                return Value();
            left = Value::fromInteger(subtract ? left.integer - right.integer
                                               : left.integer + right.integer);
        }
        return left;
    }

    Value parseMultiplicative()
    {
        Value left = parseUnary();
        while (isOp("*") || isOp("/") || isOp("%")) {
            const char op = _tok.text[0];
            consume();
            Value right = parseUnary();
            if (!requireIntegers(left, right))
                return Value();
            if ((op == '/' || op == '%') && right.integer == 0) {
                if (!_skipEvaluation)
                    _ok = false;
                return Value::fromInteger(0);
            }
            if (op == '*') left.integer *= right.integer;
            else if (op == '/') left.integer /= right.integer;
            else left.integer %= right.integer;
        }
        return left;
    }

    Value parseUnary()
    {
        if (isOp("!")) {
            consume();
            Value v = parseUnary();
            return v.kind == ValueInvalid ? Value() : Value::fromInteger(!v.truth());
        }
        if (isOp("~")) {
            consume();
            Value v = parseUnary();
            if (v.kind != ValueInteger) { _ok = false; return Value(); }
            return Value::fromInteger(~v.integer);
        }
        if (isOp("-")) {
            consume();
            Value v = parseUnary();
            if (v.kind != ValueInteger) { _ok = false; return Value(); }
            return Value::fromInteger(-v.integer);
        }
        if (isOp("+")) {
            consume();
            return parseUnary();
        }
        return parsePrimary();
    }

    Value parsePrimary()
    {
        if (_tok.kind == TokNumber) {
            const uint64_t n = _tok.number;
            consume();
            return Value::fromInteger(static_cast<int64_t>(n));
        }
        if (_tok.kind == TokString) {
            const std::string s = _tok.text;
            consume();
            return Value::fromString(s);
        }
        if (_tok.kind == TokLParen) {
            consume();
            Value v = parseLogicalOr();
            if (_tok.kind != TokRParen) {
                _ok = false;
                return Value();
            }
            consume();
            return v;
        }
        if (_tok.kind != TokIdent) {
            _ok = false;
            return Value();
        }

        const std::string name = _tok.text;
        consume();
        if (_tok.kind == TokLParen)
            return parseFunction(name);

        if (name == "true") return Value::fromInteger(1);
        if (name == "false") return Value::fromInteger(0);
        if (_skipEvaluation) return Value::fromInteger(0);
        if (name == "size") return Value::fromInteger(static_cast<int64_t>(_st.st_size));
        if (name == "mode") return Value::fromInteger(static_cast<int64_t>(_st.st_mode));
        if (name == "linkcount") return Value::fromInteger(static_cast<int64_t>(_st.st_nlink));
        if (name == "ascii") return Value::fromInteger(looksAscii512(_data));
        if (name == "tag") return Value::fromInteger(executableTag(_data));
        if (name == "mactype" || name == "maccreator")
            return Value::fromString("????");

        _ok = false;
        return Value();
    }

    Value parseFunction(const std::string &name)
    {
        consume();
        std::vector<Value> args;
        if (_tok.kind != TokRParen) {
            for (;;) {
                args.push_back(parseLogicalOr());
                if (_tok.kind == TokComma) {
                    consume();
                    continue;
                }
                break;
            }
        }
        if (_tok.kind != TokRParen) {
            _ok = false;
            return Value();
        }
        consume();

        if (_skipEvaluation)
            return Value::fromInteger(0);

        if (name == "glob" && args.size() == 1 && args[0].kind == ValueString)
            return Value::fromInteger(wildcardMatch(args[0].string, _path));
        if (name == "dircontains" && args.size() == 1 && args[0].kind == ValueString)
            return Value::fromInteger(dirContains(_path, args[0].string));
        if (name == "string" && args.size() == 2 &&
            args[0].kind == ValueInteger && args[1].kind == ValueInteger) {
            if (args[0].integer < 0 || args[1].integer < 0)
                return Value::fromString("");
            const std::size_t off = static_cast<std::size_t>(args[0].integer);
            const std::size_t len = static_cast<std::size_t>(args[1].integer);
            if (off > _data.size() || len > _data.size() - off)
                return Value::fromString("");
            return Value::fromString(std::string(
                reinterpret_cast<const char *>(&_data[off]), len));
        }

        if ((name == "char" || name == "uchar" || name == "short" ||
             name == "ushort" || name == "long" || name == "ulong") &&
            args.size() == 1 && args[0].kind == ValueInteger) {
            if (args[0].integer < 0)
                return Value::fromInteger(0);
            const std::size_t off = static_cast<std::size_t>(args[0].integer);
            const std::size_t width = (name == "char" || name == "uchar") ? 1 :
                                      (name == "short" || name == "ushort") ? 2 : 4;
            bool ok = false;
            const uint64_t raw = readUnsignedBE(_data, off, width, ok);
            if (!ok)
                return Value::fromInteger(0);
            if (name == "char") return Value::fromInteger(signExtend(raw, 8));
            if (name == "short") return Value::fromInteger(signExtend(raw, 16));
            if (name == "long") return Value::fromInteger(signExtend(raw, 32));
            return Value::fromInteger(static_cast<int64_t>(raw));
        }

        _ok = false;
        return Value();
    }

    bool requireIntegers(const Value &a, const Value &b)
    {
        if (a.kind == ValueInteger && b.kind == ValueInteger)
            return true;
        if (!_skipEvaluation)
            _ok = false;
        return false;
    }

    Value integerBinary(const Value &a, const Value &b, char op)
    {
        if (!requireIntegers(a, b))
            return Value();
        if (op == '|') return Value::fromInteger(a.integer | b.integer);
        if (op == '^') return Value::fromInteger(a.integer ^ b.integer);
        return Value::fromInteger(a.integer & b.integer);
    }

    Lexer _lex;
    Token _tok;
    const std::string &_path;
    const struct stat &_st;
    const std::vector<unsigned char> &_data;
    bool _ok;
    bool _skipEvaluation;
};

static std::string stripFtrComment(const std::string &line)
{
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && c == '#')
            return line.substr(0, i);
    }
    return line;
}

static void parseRulesFromFile(const std::string &path,
                               std::vector<Rule> &rules)
{
    std::ifstream in(path.c_str());
    if (!in.good())
        return;

    std::string currentType;
    bool currentSpecial = false;
    bool collectingMatch = false;
    std::string match;
    std::string completedMatch;
    std::string line;

    const auto finishType = [&]() {
        if (!currentType.empty() && !completedMatch.empty()) {
            Rule r;
            r.type = currentType;
            r.expression = completedMatch;
            r.specialFile = currentSpecial;
            rules.push_back(r);
        }
    };

    while (std::getline(in, line)) {
        const std::string t = trim(stripFtrComment(line));
        if (t.empty())
            continue;

        if (t.compare(0, 5, "TYPE ") == 0) {
            finishType();
            currentType = trim(t.substr(5));
            currentSpecial = false;
            collectingMatch = false;
            match.clear();
            completedMatch.clear();
            continue;
        }
        if (currentType.empty())
            continue;
        if (t.compare(0, 11, "SPECIALFILE") == 0) {
            currentSpecial = true;
            continue;
        }

        if (!collectingMatch && t.compare(0, 5, "MATCH") == 0) {
            collectingMatch = true;
            match = trim(t.substr(5));
        } else if (collectingMatch) {
            if (!match.empty())
                match += ' ';
            match += t;
        }

        if (collectingMatch) {
            bool quoted = false;
            bool escaped = false;
            for (std::size_t i = 0; i < match.size(); ++i) {
                const char c = match[i];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (quoted && c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == '"') {
                    quoted = !quoted;
                    continue;
                }
                if (!quoted && c == ';') {
                    completedMatch = trim(match.substr(0, i));
                    collectingMatch = false;
                    match.clear();
                    break;
                }
            }
        }
    }
    finishType();
}

static void collectFtrFiles(const std::string &root,
                            const std::string &relative,
                            std::vector<std::string> &files)
{
    const std::string full = root + "/" + relative;
    DIR *dir = opendir(full.c_str());
    if (!dir)
        return;

    std::vector<std::string> names;
    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        const std::string name(entry->d_name);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".ftr") == 0)
            names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());

    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string path = full + "/" + names[i];
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            files.push_back(path);
    }
}

static const std::vector<Rule> &rulesForRoot(const std::string &root)
{
    static std::string cachedRoot;
    static std::vector<Rule> cached;
    if (cachedRoot == root)
        return cached;

    cachedRoot = root;
    cached.clear();
    static const char *const sourceClasses[] = {
        "local", "install", "system", "default", 0
    };
    for (int i = 0; sourceClasses[i]; ++i) {
        std::vector<std::string> files;
        collectFtrFiles(root, sourceClasses[i], files);
        for (std::size_t j = 0; j < files.size(); ++j)
            parseRulesFromFile(files[j], cached);
    }
    return cached;
}
}

bool
ImdEvaluateFtrType(const std::string &filetypeRoot,
                   const std::string &path,
                   bool knownDirectory,
                   std::string &typeName)
{
    typeName.clear();
    if (filetypeRoot.empty() || path.empty())
        return false;

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (!knownDirectory)
            return false;
        std::memset(&st, 0, sizeof(st));
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 1;
    }

    std::vector<unsigned char> data;
    if (S_ISREG(st.st_mode))
        readFile(path, data);

    const std::vector<Rule> &rules = rulesForRoot(filetypeRoot);
    if (rules.empty())
        return false;

    const bool plainFile = S_ISREG(st.st_mode);
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].specialFile == plainFile)
            continue;
        Parser parser(rules[i].expression, path, st, data);
        bool matched = false;
        if (parser.evaluate(matched) && matched) {
            typeName = rules[i].type;
            return true;
        }
    }
    return false;
}
