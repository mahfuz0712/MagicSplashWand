#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

struct Parser {
    const std::string& t;
    size_t i = 0;
    int depth = 0;
    explicit Parser(const std::string& s) : t(s) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
    }

    static void putUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& v) {
        if (i + 4 > t.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = t[i + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        i += 4;
        return true;
    }

    bool str(std::string& out) {
        if (i >= t.size() || t[i] != '"') return false;
        i++;
        out.clear();
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool value(JVal& v) {
        if (++depth > 64) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        bool ok = false;
        if (c == '{') {
            i++;
            v.type = JVal::Obj;
            ws();
            if (i < t.size() && t[i] == '}') {
                i++;
                ok = true;
            } else {
                while (true) {
                    ws();
                    std::string k;
                    if (!str(k)) break;
                    ws();
                    if (i >= t.size() || t[i] != ':') break;
                    i++;
                    JVal child;
                    if (!value(child)) break;
                    v.o.emplace_back(std::move(k), std::move(child));
                    ws();
                    if (i < t.size() && t[i] == ',') { i++; continue; }
                    if (i < t.size() && t[i] == '}') { i++; ok = true; }
                    break;
                }
            }
        } else if (c == '[') {
            i++;
            v.type = JVal::Arr;
            ws();
            if (i < t.size() && t[i] == ']') {
                i++;
                ok = true;
            } else {
                while (true) {
                    JVal child;
                    if (!value(child)) break;
                    v.a.push_back(std::move(child));
                    ws();
                    if (i < t.size() && t[i] == ',') { i++; continue; }
                    if (i < t.size() && t[i] == ']') { i++; ok = true; }
                    break;
                }
            }
        } else if (c == '"') {
            v.type = JVal::Str;
            ok = str(v.s);
        } else if (t.compare(i, 4, "true") == 0) {
            v.type = JVal::Bool; v.b = true; i += 4; ok = true;
        } else if (t.compare(i, 5, "false") == 0) {
            v.type = JVal::Bool; v.b = false; i += 5; ok = true;
        } else if (t.compare(i, 4, "null") == 0) {
            v.type = JVal::Null; i += 4; ok = true;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t st = i;
            i++;
            while (i < t.size() && (isdigit((unsigned char)t[i]) || t[i] == '.' || t[i] == 'e' ||
                                    t[i] == 'E' || t[i] == '+' || t[i] == '-'))
                i++;
            v.type = JVal::Num;
            v.s = t.substr(st, i - st);
            v.n = std::strtod(v.s.c_str(), nullptr);
            ok = true;
        }
        depth--;
        return ok;
    }
};

}  // namespace

bool parseJson(const std::string& text, JVal& out) {
    Parser p(text);
    // skip UTF-8 BOM
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        p.i = 3;
    if (!p.value(out)) return false;
    p.ws();
    return p.i == text.size();
}

std::string jsonQuote(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    r += buf;
                } else {
                    r += (char)c;
                }
        }
    }
    r += '"';
    return r;
}
