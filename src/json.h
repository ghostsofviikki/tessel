// Minimal JSON reader — enough for the FancyZones config files.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal* get(const char* key) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    int asInt(int def = 0) const { return t == Num ? (int)n : def; }
    bool asBool(bool def = false) const { return t == Bool ? b : def; }
    std::string asStr() const { return t == Str ? s : std::string(); }
    JVal* get(const char* key) { return const_cast<JVal*>(static_cast<const JVal*>(this)->get(key)); }
    // Object member, created (as null) if missing.
    JVal& at(const char* key) {
        if (t != Obj) { *this = JVal{}; t = Obj; }
        if (JVal* v = get(key)) return *v;
        o.emplace_back(key, JVal{});
        return o.back().second;
    }
    static JVal Int(long long v) { JVal j; j.t = Num; j.n = (double)v; return j; }
    static JVal String(const std::string& v) { JVal j; j.t = Str; j.s = v; return j; }
    static JVal Boolean(bool v) { JVal j; j.t = Bool; j.b = v; return j; }
    static JVal IntArray(const std::vector<int>& v) { JVal j; j.t = Arr; for (int x : v) j.a.push_back(Int(x)); return j; }
    int intAt(const char* key, int def = 0) const { auto v = get(key); return v ? v->asInt(def) : def; }
    bool boolAt(const char* key, bool def = false) const { auto v = get(key); return v ? v->asBool(def) : def; }
    std::string strAt(const char* key) const { auto v = get(key); return v ? v->asStr() : std::string(); }
};

class JParser {
public:
    explicit JParser(const std::string& text) : p_(text.c_str()), end_(text.c_str() + text.size()) {
        if (end_ - p_ >= 3 && (unsigned char)p_[0] == 0xEF && (unsigned char)p_[1] == 0xBB && (unsigned char)p_[2] == 0xBF) p_ += 3;
    }
    bool parse(JVal& out) {
        if (!value(out)) return false;
        ws();
        return p_ == end_;
    }

private:
    const char* p_;
    const char* end_;

    void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }

    bool lit(const char* w) {
        size_t n = strlen(w);
        if ((size_t)(end_ - p_) < n || strncmp(p_, w, n) != 0) return false;
        p_ += n;
        return true;
    }

    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }

    bool hex4(unsigned& v) {
        if (end_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool str(std::string& s) {
        if (p_ >= end_ || *p_ != '"') return false;
        ++p_;
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c != '\\') { s += c; continue; }
            if (p_ >= end_) return false;
            char e = *p_++;
            switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                    p_ += 2;
                    unsigned lo;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(s, cp);
                break;
            }
            default: return false;
            }
        }
        if (p_ >= end_) return false;
        ++p_;
        return true;
    }

    bool value(JVal& v) {
        ws();
        if (p_ >= end_) return false;
        char c = *p_;
        if (c == '{') {
            v.t = JVal::Obj;
            ++p_;
            ws();
            if (p_ < end_ && *p_ == '}') { ++p_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p_ >= end_ || *p_ != ':') return false;
                ++p_;
                JVal child;
                if (!value(child)) return false;
                v.o.emplace_back(std::move(k), std::move(child));
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.t = JVal::Arr;
            ++p_;
            ws();
            if (p_ < end_ && *p_ == ']') { ++p_; return true; }
            for (;;) {
                JVal child;
                if (!value(child)) return false;
                v.a.push_back(std::move(child));
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') { v.t = JVal::Str; return str(v.s); }
        if (lit("true")) { v.t = JVal::Bool; v.b = true; return true; }
        if (lit("false")) { v.t = JVal::Bool; v.b = false; return true; }
        if (lit("null")) { v.t = JVal::Null; return true; }
        char* e = nullptr;
        v.n = strtod(p_, &e);
        if (e == p_) return false;
        v.t = JVal::Num;
        p_ = e;
        return true;
    }
};

inline bool JParse(const std::string& text, JVal& out) { return JParser(text).parse(out); }

// Pretty printer, 2-space indent like the FancyZones files.
inline void JWrite(const JVal& v, std::string& out, int indent = 0) {
    auto pad = [&](int n) { out += '\n'; out.append((size_t)n * 2, ' '); };
    switch (v.t) {
    case JVal::Null: out += "null"; break;
    case JVal::Bool: out += v.b ? "true" : "false"; break;
    case JVal::Num: {
        char buf[32];
        if (v.n == (double)(long long)v.n) snprintf(buf, sizeof(buf), "%lld", (long long)v.n);
        else snprintf(buf, sizeof(buf), "%.17g", v.n);
        out += buf;
        break;
    }
    case JVal::Str:
        out += '"';
        for (unsigned char c : v.s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
            else out += (char)c;
        }
        out += '"';
        break;
    case JVal::Arr:
        out += '[';
        for (size_t i = 0; i < v.a.size(); ++i) {
            if (i) out += ',';
            pad(indent + 1);
            JWrite(v.a[i], out, indent + 1);
        }
        if (!v.a.empty()) pad(indent);
        out += ']';
        break;
    case JVal::Obj:
        out += '{';
        for (size_t i = 0; i < v.o.size(); ++i) {
            if (i) out += ',';
            pad(indent + 1);
            JWrite(JVal::String(v.o[i].first), out, indent + 1);
            out += ": ";
            JWrite(v.o[i].second, out, indent + 1);
        }
        if (!v.o.empty()) pad(indent);
        out += '}';
        break;
    }
}
