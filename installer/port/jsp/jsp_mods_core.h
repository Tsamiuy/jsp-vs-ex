// ENHANCEMENT: the core of the mods/ framework (GMOD round 1, layer L1 "virtual CPK"). Header only, no game, toolkit or OS calls:
// every file access goes through jmods::Io, so the host test (installer/tools/mods_overlay_test.cpp) drives it on a synthetic archive.
//
// What it does, in load order:
//   1. mods.txt: one mod id per line, '#' starts a comment, line order = load order, duplicates are logged and ignored.
//   2. <id>/mod.json, manifest schema 1: id, version, title, game.title_ids, interfaces, gameplay, groups, overrides, conflicts_ok.
//      An unknown interface or a higher version than the port supports refuses the mod. Any error refuses the whole mod (never
//      half-applied); the other mods still load.
//   3. Each override names an in-archive entry ("DirName/FileName"; the first folder names the archive partition_op_<folder>_ps3.cpk)
//      and a source: "file" (a file in the mod folder, checked against its "sha256") or "alias" (another entry of the same archive in
//      the player's own copy). A missing row is an error ("did you mean" for a case-insensitive match).
//   4. Conflicts: the later mod in the list wins an entry, with a "[mods] conflict" line ("conflicts_ok" only silences it). A group
//      (entries that load together, e.g. a model's _m/_i/_v) that loses any entry to a later mod is dropped whole.
//   5. One Plan per archive basename: the original bytes up to the TOC table, the TOC @UTF table with each overridden row's FileSize,
//      ExtractSize rewritten (unmasked, edited, re-masked; names, sort order, ITOC and ETOC untouched), the rest of the
//      original file. A replacement is written IN PLACE over its entry's own 2048-aligned slot: the archive keeps its size and layout
//      (GMOD R6: the game reacts to the archive's size or end layout; an archive that grew left the boot black even with unchanged
//      bytes). A replacement larger than its slot is refused for now (the appended layout is not proven in a game).
//   6. Class: an entry is cosmetic only under the allow-list (game/logo/, movie/, sound/); every other path is gameplay. The manifest's
//      "gameplay": true can raise a mod's class, never lower it.
//   7. Digest: SHA-256 over "jsp-modset 1\n", the EBOOT id, and for each applied gameplay mod in load order "id version\n" plus
//      "entry sha256-of-served-bytes\n" per served entry. No applied gameplay mod = no digest. A listed mod that was refused and is not
//      provably cosmetic makes the digest the fixed refusing value (kRefused): the set does not run as listed, so online must refuse.
// ASSUMPTION: CRI does not check an entry's FileSize against the header's EnabledPackedSize/EnabledDataSize (2x the sum of the FileSize
// values on the US disc; the header row is left as it is, so a smaller replacement leaves them unchanged). The first in-game run of a
// smaller replacement tests it.
// ASSUMPTION: entry data lies at TocOffset + FileOffset (true for every J-Stars archive; a CPK whose ContentOffset is below its
// TocOffset is refused rather than guessed).
// ASSUMPTION: a group that loses one entry to a later mod is dropped whole (design choice: a model is never mixed from two mods).
#ifndef JSP_MODS_CORE_H
#define JSP_MODS_CORE_H

#include "jsp_session_digest.h"   // jpsm::Sha256, jpsm::Digest (pure)

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace jmods {

using Digest = jpsm::Digest;
using Bytes = std::vector<uint8_t>;

constexpr uint64_t kAlign = 2048;   // CPK Align of every J-Stars archive; CRI reads in 2048 B units

// ---- I/O the caller provides -------------------------------------------------------------------------------------------------------
// Mod files are named relative to the mods folder with '/' ("<id>/mod.json", "<id>/files/..."); archives by basename.
class Io {
public:
    virtual ~Io() = default;
    virtual bool mod_size(const std::string& rel, uint64_t& size) = 0;
    virtual size_t mod_read(const std::string& rel, uint64_t off, void* dst, size_t n) = 0;
    // Why the last mod_size of `rel` failed, when the glue knows (a link, a sharing violation): added to the refusal line. "" = no reason known.
    virtual std::string mod_why(const std::string&) { return std::string(); }
    virtual bool archive_size(const std::string& base, uint64_t& size) = 0;
    virtual size_t archive_read(const std::string& base, uint64_t off, void* dst, size_t n) = 0;
};

// ---- small helpers ------------------------------------------------------------------------------------------------------------------
inline std::string hex(const uint8_t* d, size_t n)
{
    static const char k[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { s[2 * i] = k[d[i] >> 4]; s[2 * i + 1] = k[d[i] & 15]; }
    return s;
}
inline std::string hex(const Digest& d) { return hex(d.data(), d.size()); }
inline std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char* f, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
inline uint64_t align_up(uint64_t v) { return (v + kAlign - 1) / kAlign * kAlign; }
inline std::string lower(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
inline bool valid_id(const std::string& s)
{
    if (s.empty() || s.size() > 40) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
// A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, also with the superscript digits 1-3, CONIN$, CONOUT$), any case, with or
// without an extension: Windows opens the device, not a file, for such a path part.
inline bool windows_device_name(const std::string& part)
{
    std::string b = part.substr(0, part.find('.'));
    while (!b.empty() && b.back() == ' ') b.pop_back();   // "nul .txt" is the device too
    for (auto& c : b) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (b == "CON" || b == "PRN" || b == "AUX" || b == "NUL" || b == "CONIN$" || b == "CONOUT$") return true;
    if (b.size() >= 4 && (b.compare(0, 3, "COM") == 0 || b.compare(0, 3, "LPT") == 0)) {
        const std::string d = b.substr(3);
        if (d.size() == 1 && d[0] >= '0' && d[0] <= '9') return true;
        if (d == "\xC2\xB9" || d == "\xC2\xB2" || d == "\xC2\xB3") return true;
    }
    return false;
}
// A relative '/' path that cannot leave its folder and names a plain file on Windows too: no leading or trailing '/', no empty, '.' or
// '..' part, no control character or any of \ : < > " | ? *, no part ending in '.' or ' ' (Windows drops those), no device name.
inline bool valid_rel_path(const std::string& p)
{
    if (p.empty() || p.size() > 255 || p.front() == '/' || p.back() == '/') return false;
    size_t s = 0;
    while (s <= p.size()) {
        size_t e = p.find('/', s);
        if (e == std::string::npos) e = p.size();
        const std::string part = p.substr(s, e - s);
        if (part.empty() || part == "." || part == "..") return false;
        if (part.back() == '.' || part.back() == ' ' || windows_device_name(part)) return false;
        for (unsigned char c : part)
            if (c < 0x20 || c == 0x7F || strchr("\\:<>\"|?*", c)) return false;
        s = e + 1;
    }
    return true;
}
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
inline void put_be32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (24 - 8 * i)); }
inline void put_be64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (56 - 8 * i)); }
inline uint64_t le64(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

// ---- classification (D40: default gameplay, cosmetic allow-list) ------------------------------------------------------------------
inline bool is_cosmetic_path(const std::string& entry)
{
    static const char* const allow[] = { "game/logo/", "movie/", "sound/" };
    for (const char* a : allow)
        if (entry.compare(0, strlen(a), a) == 0) return true;
    return false;
}
// "battle/..." -> "partition_op_battle_ps3.cpk"; "" for a folder no overridable archive has (the patch archive is not overridable).
inline std::string archive_of(const std::string& entry)
{
    static const char* const folders[] = { "battle", "character", "game", "map", "progress", "sound" };
    const size_t s = entry.find('/');
    if (s == std::string::npos) return "";
    const std::string f = entry.substr(0, s);
    for (const char* k : folders)
        if (f == k) return "partition_op_" + f + "_ps3.cpk";
    return "";
}

// ---- mods.txt ----------------------------------------------------------------------------------------------------------------------
struct ListEntry { std::string id; int line; bool valid; };
inline std::vector<ListEntry> parse_mods_list(const std::string& text, std::vector<std::string>& log)
{
    std::vector<ListEntry> out;
    std::set<std::string> seen;
    int ln = 0;
    size_t s = 0;
    while (s < text.size()) {
        size_t e = text.find('\n', s);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(s, e - s);
        s = e + 1;
        ++ln;
        if (const size_t h = l.find('#'); h != std::string::npos) l.resize(h);
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        size_t b = 0;
        while (b < l.size() && (l[b] == ' ' || l[b] == '\t' || (b == 0 && ln == 1 && (unsigned char)l[0] == 0xEF))) {
            if ((unsigned char)l[b] == 0xEF && l.size() >= 3 && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF) b += 3;
            else ++b;
        }
        l = l.substr(b);
        if (l.empty()) continue;
        if (!seen.insert(l).second) { log.push_back(fmt("[mods] list: line %d: duplicate %s ignored", ln, l.c_str())); continue; }
        const bool ok = valid_id(l);
        if (!ok) log.push_back(fmt("[mods] list: line %d: '%s' is not a mod id ([a-z0-9-], 1-40 characters)", ln, l.c_str()));
        out.push_back({ l, ln, ok });
    }
    return out;
}

// ---- a small strict JSON reader (RFC 8259 grammar; objects, arrays, strings, integers, true/false/null) -------------------------------------------
struct Json {
    enum Type { Null, Bool, Int, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::string> keys;   // an object: keys[i] -> vals[i], in file order
    std::vector<Json> vals;
    const Json* get(const char* k) const;
};
inline const Json* Json::get(const char* k) const
{
    for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return &vals[i];
    return nullptr;
}
class JsonParser {
public:
    explicit JsonParser(const std::string& t) : t_(t) {}
    bool parse(Json& out, std::string& err)
    {
        out = Json();
        p_ = 0;
        err_.clear();
        if (t_.compare(0, 3, "\xEF\xBB\xBF") == 0) p_ = 3;   // one leading UTF-8 BOM (Windows editors write it)
        ws();
        if (!value(out, 0)) { err = err_.empty() ? fmt("bad JSON at byte %llu", (unsigned long long)p_) : err_; return false; }
        ws();
        if (p_ != t_.size()) { err = fmt("trailing data at byte %llu", (unsigned long long)p_); return false; }
        return true;
    }
private:
    const std::string& t_;
    size_t p_ = 0;
    std::string err_;
    void ws() { while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_; }
    bool lit(const char* w)
    {
        const size_t n = strlen(w);
        if (t_.compare(p_, n, w) != 0) return false;
        p_ += n;
        return true;
    }
    bool str(std::string& o)
    {
        if (p_ >= t_.size() || t_[p_] != '"') return false;
        ++p_;
        while (p_ < t_.size()) {
            const unsigned char c = (unsigned char)t_[p_++];
            if (c == '"') return true;
            if (c < 0x20) { err_ = fmt("control character in a string at byte %llu", (unsigned long long)(p_ - 1)); return false; }
            if (c != '\\') { o += char(c); continue; }
            if (p_ >= t_.size()) return false;
            const char e = t_[p_++];
            switch (e) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                if (p_ + 4 > t_.size()) return false;
                unsigned v = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = t_[p_++];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
                    else return false;
                }
                if (v >= 0xD800 && v <= 0xDFFF) { err_ = "surrogate \\u escapes are not supported"; return false; }
                if (v < 0x80) o += char(v);
                else if (v < 0x800) { o += char(0xC0 | (v >> 6)); o += char(0x80 | (v & 0x3F)); }
                else { o += char(0xE0 | (v >> 12)); o += char(0x80 | ((v >> 6) & 0x3F)); o += char(0x80 | (v & 0x3F)); }
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Json& v, int depth)
    {
        if (depth > 32 || p_ >= t_.size()) return false;
        const char c = t_[p_];
        if (c == '{') {
            ++p_;
            v.type = Json::Obj;
            ws();
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                if (v.get(k.c_str())) { err_ = "duplicate key \"" + k + "\""; return false; }
                ws();
                if (p_ >= t_.size() || t_[p_] != ':') return false;
                ++p_;
                ws();
                Json m;
                if (!value(m, depth + 1)) return false;
                v.keys.push_back(std::move(k));
                v.vals.push_back(std::move(m));
                ws();
                if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            v.type = Json::Arr;
            ws();
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            for (;;) {
                ws();
                Json m;
                if (!value(m, depth + 1)) return false;
                v.arr.push_back(std::move(m));
                ws();
                if (p_ < t_.size() && t_[p_] == ',') { ++p_; continue; }
                if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = Json::Str; return str(v.s); }
        if (lit("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Json::Bool; v.b = false; return true; }
        if (lit("null")) { v.type = Json::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            // RFC 8259: -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?  (no leading zeros, no bare '.', 'e' or sign)
            const size_t s = p_;
            auto digits = [&]() { const size_t d = p_; while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') ++p_; return p_ - d; };
            if (t_[p_] == '-') ++p_;
            if (p_ >= t_.size()) return false;
            if (t_[p_] == '0') ++p_;
            else if (!digits()) return false;
            bool frac = false;
            if (p_ < t_.size() && t_[p_] == '.') { ++p_; frac = true; if (!digits()) return false; }
            if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
                ++p_;
                frac = true;
                if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) ++p_;
                if (!digits()) return false;
            }
            const std::string num = t_.substr(s, p_ - s);
            if (frac || num.size() > 18) { v.type = Json::Num; return true; }   // not an integer this schema can use
            v.type = Json::Int;
            v.i = std::stoll(num);
            return true;
        }
        return false;
    }
};

// ---- manifest schema 1 -------------------------------------------------------------------------------------------------------------
struct Interface { std::string name; int version; };
// What this build supports. Round 1: cpk-entry 1 only ("balance" joins with its applier, step R8).
inline const std::vector<Interface>& supported_interfaces()
{
    static const std::vector<Interface> k = { { "cpk-entry", 1 } };
    return k;
}
struct OverrideSpec { std::string entry, alias, file, sha256; };
struct Manifest {
    int schema = 0;
    std::string id, version, title;
    std::vector<std::string> title_ids;
    std::vector<Interface> interfaces;
    bool gameplay = false;
    std::vector<std::vector<std::string>> groups;
    std::vector<OverrideSpec> overrides;
    std::vector<std::string> conflicts_ok;
};

inline bool parse_manifest(const std::string& text, const std::string& folder_id, const std::vector<Interface>& supported,
                           const std::string& title_id, Manifest& m, std::string& err)
{
    Json j;
    if (!JsonParser(text).parse(j, err)) { err = "mod.json: " + err; return false; }
    if (j.type != Json::Obj) { err = "mod.json: not an object"; return false; }
    const Json* v = j.get("schema");
    if (!v || v->type != Json::Int) { err = "mod.json: no integer \"schema\""; return false; }
    if (v->i != 1) { err = fmt("mod.json: schema %lld is not supported (this build reads schema 1)", (long long)v->i); return false; }
    m.schema = 1;
    v = j.get("id");
    if (!v || v->type != Json::Str || !valid_id(v->s)) { err = "mod.json: \"id\" must be [a-z0-9-], 1-40 characters"; return false; }
    if (v->s != folder_id) { err = "mod.json: id \"" + v->s + "\" is not the folder name \"" + folder_id + "\""; return false; }
    m.id = v->s;
    v = j.get("version");
    if (!v || v->type != Json::Str || v->s.empty() || v->s.size() > 40) { err = "mod.json: \"version\" must be a string of 1-40 characters"; return false; }
    for (unsigned char c : v->s) if (c <= 0x20 || c == 0x7F) { err = "mod.json: \"version\" may not contain spaces or control characters"; return false; }
    m.version = v->s;
    if ((v = j.get("title"))) {
        if (v->type != Json::Str) { err = "mod.json: \"title\" must be a string"; return false; }
        m.title = v->s;
    }
    if ((v = j.get("game"))) {
        if (v->type != Json::Obj) { err = "mod.json: \"game\" must be an object"; return false; }
        if (const Json* t = v->get("title_ids")) {
            if (t->type != Json::Arr) { err = "mod.json: \"game.title_ids\" must be an array"; return false; }
            for (const Json& x : t->arr) {
                if (x.type != Json::Str) { err = "mod.json: \"game.title_ids\" holds a non-string"; return false; }
                m.title_ids.push_back(x.s);
            }
        }
    }
    if (!title_id.empty() && !m.title_ids.empty() &&
        std::find(m.title_ids.begin(), m.title_ids.end(), title_id) == m.title_ids.end()) {
        err = "made for another game version (this one is " + title_id + ")";
        return false;
    }
    v = j.get("interfaces");
    if (!v || v->type != Json::Obj) { err = "mod.json: no \"interfaces\" object"; return false; }
    for (size_t k = 0; k < v->keys.size(); ++k) {
        const std::string& name = v->keys[k];
        const Json& ver = v->vals[k];
        if (ver.type != Json::Int || ver.i < 1 || ver.i > 1000000) {
            err = "mod.json: interface \"" + name + "\" needs a positive integer version";
            return false;
        }
        const auto it = std::find_if(supported.begin(), supported.end(), [&](const Interface& s) { return s.name == name; });
        if (it == supported.end()) { err = "interface \"" + name + "\" is not supported by this build"; return false; }
        if (ver.i > it->version) {
            err = fmt("interface \"%s\" %lld is newer than this build supports (%d)", name.c_str(), (long long)ver.i, it->version);
            return false;
        }
        m.interfaces.push_back({ name, int(ver.i) });
    }
    if ((v = j.get("gameplay"))) {
        if (v->type != Json::Bool) { err = "mod.json: \"gameplay\" must be true or false"; return false; }
        m.gameplay = v->b;
    }
    if ((v = j.get("overrides"))) {
        if (v->type != Json::Arr) { err = "mod.json: \"overrides\" must be an array"; return false; }
        std::set<std::string> seen;
        for (const Json& o : v->arr) {
            if (o.type != Json::Obj) { err = "mod.json: an override is not an object"; return false; }
            OverrideSpec s;
            const Json* e = o.get("entry");
            if (!e || e->type != Json::Str || !valid_rel_path(e->s)) { err = "mod.json: an override has no valid \"entry\""; return false; }
            s.entry = e->s;
            if (archive_of(s.entry).empty()) { err = "entry " + s.entry + ": its first folder names no archive a mod may override"; return false; }
            if (!seen.insert(s.entry).second) { err = "entry " + s.entry + " is overridden twice"; return false; }
            const Json* a = o.get("alias");
            const Json* f = o.get("file");
            if (!!a == !!f) { err = "entry " + s.entry + ": give exactly one of \"alias\" and \"file\""; return false; }
            if (a) {
                if (a->type != Json::Str || !valid_rel_path(a->s)) { err = "entry " + s.entry + ": bad \"alias\""; return false; }
                s.alias = a->s;
                if (o.get("sha256")) { err = "entry " + s.entry + ": an alias takes no \"sha256\""; return false; }
            } else {
                if (f->type != Json::Str || !valid_rel_path(f->s)) { err = "entry " + s.entry + ": bad \"file\" path"; return false; }
                s.file = f->s;
                const Json* h = o.get("sha256");
                if (!h || h->type != Json::Str || h->s.size() != 64) { err = "entry " + s.entry + ": \"file\" needs a 64-digit \"sha256\""; return false; }
                s.sha256 = lower(h->s);
                for (char c : s.sha256)
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { err = "entry " + s.entry + ": \"sha256\" is not hex"; return false; }
            }
            m.overrides.push_back(std::move(s));
        }
    }
    if (!m.overrides.empty() &&
        std::find_if(m.interfaces.begin(), m.interfaces.end(), [](const Interface& i) { return i.name == "cpk-entry"; }) == m.interfaces.end()) {
        err = "mod.json: overrides need interface \"cpk-entry\"";
        return false;
    }
    if ((v = j.get("groups"))) {
        if (v->type != Json::Arr) { err = "mod.json: \"groups\" must be an array of arrays"; return false; }
        std::set<std::string> grouped;
        for (const Json& g : v->arr) {
            if (g.type != Json::Arr || g.arr.empty()) { err = "mod.json: a group must be a non-empty array"; return false; }
            std::vector<std::string> one;
            for (const Json& x : g.arr) {
                if (x.type != Json::Str) { err = "mod.json: a group holds a non-string"; return false; }
                if (std::find_if(m.overrides.begin(), m.overrides.end(), [&](const OverrideSpec& s) { return s.entry == x.s; }) == m.overrides.end()) {
                    err = "group entry " + x.s + " has no override in this mod";
                    return false;
                }
                if (!grouped.insert(x.s).second) { err = "entry " + x.s + " is in two groups"; return false; }
                one.push_back(x.s);
            }
            m.groups.push_back(std::move(one));
        }
    }
    if ((v = j.get("conflicts_ok"))) {
        if (v->type != Json::Arr) { err = "mod.json: \"conflicts_ok\" must be an array of ids"; return false; }
        for (const Json& x : v->arr) {
            if (x.type != Json::Str) { err = "mod.json: \"conflicts_ok\" holds a non-string"; return false; }
            m.conflicts_ok.push_back(x.s);
        }
    }
    return true;
}

// ---- CRI @UTF tables -------------------------------------------------------------------------------------------------------------
// Layout (big-endian): "@UTF", u32 size of the rest; then, relative to byte 8: u16 version, u16 rows, u32 strings, u32 data,
// u32 table name, u16 columns, u16 row width, u32 rows; the column schema at byte 0x20: u8 flags (storage << 4 | type), u32 name,
// and for constant storage (3) the value. Per-row storage (5) is in each row, in column order. Types: 0/1 u8/s8, 2/3 u16/s16,
// 4/5 u32/s32, 6/7 u64/s64, 8 f32, 9 f64, 10 string (u32 offset), 11 data (u32 offset, u32 size).
// Masked tables: every byte XOR the low byte of m, m = 0x655F then m = m * 0x4115 (16 bits) per byte; masking is its own inverse.
inline void mask(uint8_t* d, size_t n)
{
    uint32_t m = 0x655F;
    for (size_t i = 0; i < n; ++i) { d[i] ^= uint8_t(m); m = (m * 0x4115) & 0xFFFF; }
}
inline int utf_type_size(int t)
{
    static const int k[12] = { 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8 };
    return t >= 0 && t < 12 ? k[t] : -1;
}
struct UtfColumn { std::string name; int storage, type; size_t pos; };   // pos: in the row (storage 5) or of the constant (storage 3)
struct UtfTable {
    Bytes b;   // unmasked
    bool masked = false;
    uint32_t rows_off = 0, strings_off = 0, data_off = 0, row_width = 0, nrows = 0;
    std::vector<UtfColumn> cols;
    const UtfColumn* col(const char* n) const
    {
        for (const auto& c : cols) if (c.name == n) return &c;
        return nullptr;
    }
    // Byte position of a column's value in row r (in b), or 0 if none (zero storage, or out of range).
    size_t at(const UtfColumn& c, uint32_t r) const
    {
        if (c.storage == 3) return c.pos;
        if (c.storage == 5 && r < nrows) return 8 + rows_off + size_t(r) * row_width + c.pos;
        return 0;
    }
    bool str_at(size_t pos, std::string& out) const
    {
        if (!pos || pos + 4 > b.size()) return false;
        const size_t o = 8 + size_t(strings_off) + be32(&b[pos]);
        const size_t end = 8 + size_t(data_off) <= b.size() ? 8 + size_t(data_off) : b.size();
        if (o >= end) return false;
        const void* z = memchr(&b[o], 0, end - o);
        if (!z) return false;
        out.assign((const char*)&b[o], (const char*)z);
        return true;
    }
    bool uint_at(const UtfColumn& c, uint32_t r, uint64_t& v) const
    {
        const size_t p = at(c, r);
        const int sz = utf_type_size(c.type);
        if (!p || c.type > 7 || p + size_t(sz) > b.size()) return false;
        v = 0;
        for (int i = 0; i < sz; ++i) v = v << 8 | b[p + i];
        return true;
    }
};
inline bool parse_utf(const uint8_t* d, size_t n, UtfTable& t, std::string& err)
{
    if (n < 0x20) { err = "@UTF table too short"; return false; }
    t.b.assign(d, d + n);
    t.masked = memcmp(t.b.data(), "@UTF", 4) != 0;
    if (t.masked) {
        mask(t.b.data(), t.b.size());
        if (memcmp(t.b.data(), "@UTF", 4) != 0) { err = "no @UTF table (masked or not)"; return false; }
    }
    const uint8_t* b = t.b.data();
    const uint64_t size = be32(b + 4);
    if (8 + size > n) { err = "@UTF size runs past its chunk"; return false; }
    t.rows_off = be32(b + 8) & 0xFFFF;
    t.strings_off = be32(b + 12);
    t.data_off = be32(b + 16);
    const uint32_t ncols = uint32_t(b[24]) << 8 | b[25];
    t.row_width = uint32_t(b[26]) << 8 | b[27];
    t.nrows = be32(b + 28);
    if (t.strings_off > size || t.data_off > size || t.data_off < t.strings_off || t.rows_off > t.strings_off) {
        err = "@UTF offsets out of order";
        return false;
    }
    if (uint64_t(t.rows_off) + uint64_t(t.row_width) * t.nrows > t.strings_off) { err = "@UTF rows run into the strings"; return false; }
    size_t p = 0x20, rowpos = 0;
    for (uint32_t i = 0; i < ncols; ++i) {
        if (p + 5 > 8 + size_t(t.rows_off)) { err = "@UTF column schema runs into the rows"; return false; }
        UtfColumn c;
        c.storage = b[p] >> 4;
        c.type = b[p] & 15;
        c.pos = 0;
        const int sz = utf_type_size(c.type);
        if (sz < 0) { err = fmt("@UTF column %u has type %d", i, c.type); return false; }
        t.cols.push_back(c);   // the name needs the string area, read below
        UtfColumn& cc = t.cols.back();
        cc.pos = p + 1;        // temporarily: where the name offset is
        p += 5;
        if (c.storage == 3) {
            const size_t name_pos = cc.pos;
            cc.pos = p;
            p += size_t(sz);
            if (p > 8 + size_t(t.rows_off)) { err = "@UTF constant runs into the rows"; return false; }
            if (!t.str_at(name_pos, cc.name)) { err = "@UTF column name"; return false; }
        } else if (c.storage == 5) {
            if (!t.str_at(cc.pos, cc.name)) { err = "@UTF column name"; return false; }
            cc.pos = rowpos;
            rowpos += size_t(sz);
        } else if (c.storage == 1 || c.storage == 0) {
            if (!t.str_at(cc.pos, cc.name)) { err = "@UTF column name"; return false; }
            cc.pos = 0;
        } else {
            err = fmt("@UTF column %u has storage %d", i, c.storage);
            return false;
        }
    }
    if (rowpos > t.row_width) { err = "@UTF per-row columns are wider than a row"; return false; }
    return true;
}

// ---- one archive: header and TOC ---------------------------------------------------------------------------------------------------
struct TocRow {
    std::string path;   // "DirName/FileName" (or FileName with no DirName)
    uint64_t size = 0, extract = 0, offset = 0;   // offset relative to toc_offset
    size_t pos_size = 0, pos_extract = 0, pos_offset = 0;   // byte positions in the unmasked table
};
struct Archive {
    std::string base;
    bool ok = false;
    std::string err;
    uint64_t file_size = 0, toc_offset = 0, toc_size = 0, content_offset = 0, etoc_offset = 0;   // etoc_offset 0 = no ETOC
    uint64_t utf_abs = 0, utf_len = 0;   // the TOC @UTF table: [utf_abs, utf_abs + utf_len) of the file
    UtfTable toc;
    std::vector<TocRow> rows;
    std::map<std::string, size_t> index;
    Digest prefix{};   // SHA-256 of [0, utf_abs + utf_len): the bytes a served copy must share with the copy the plan was made from
};
inline bool read_exact(Io& io, const std::string& base, uint64_t off, void* dst, size_t n)
{
    return io.archive_read(base, off, dst, n) == n;
}
inline Archive load_archive(Io& io, const std::string& base)
{
    Archive a;
    a.base = base;
    if (!io.archive_size(base, a.file_size)) { a.err = base + " not found"; return a; }
    uint8_t h[16];
    if (a.file_size < kAlign || !read_exact(io, base, 0, h, 16) || memcmp(h, "CPK ", 4) != 0) { a.err = base + ": not a CPK"; return a; }
    const uint64_t hlen = le64(h + 8);
    if (hlen < 0x20 || 16 + hlen > a.file_size || hlen > (1u << 20)) { a.err = base + ": bad CPK header chunk"; return a; }
    Bytes hb(hlen);
    if (!read_exact(io, base, 16, hb.data(), hb.size())) { a.err = base + ": short read"; return a; }
    UtfTable ht;
    std::string e;
    if (!parse_utf(hb.data(), hb.size(), ht, e)) { a.err = base + ": header " + e; return a; }
    const UtfColumn* c_to = ht.col("TocOffset");
    const UtfColumn* c_ts = ht.col("TocSize");
    const UtfColumn* c_co = ht.col("ContentOffset");
    if (!c_to || !c_ts || !c_co || !ht.uint_at(*c_to, 0, a.toc_offset) || !ht.uint_at(*c_ts, 0, a.toc_size) ||
        !ht.uint_at(*c_co, 0, a.content_offset)) {
        a.err = base + ": header has no TocOffset/TocSize/ContentOffset";
        return a;
    }
    if (const UtfColumn* c_eo = ht.col("EtocOffset")) ht.uint_at(*c_eo, 0, a.etoc_offset);
    if (a.toc_offset < 16 + hlen || a.content_offset < a.toc_offset || a.toc_offset + 16 > a.file_size) {
        a.err = base + ": unsupported layout (TOC must follow the header and precede the content)";
        return a;
    }
    uint8_t th[16];
    if (!read_exact(io, base, a.toc_offset, th, 16) || memcmp(th, "TOC ", 4) != 0) { a.err = base + ": no TOC chunk"; return a; }
    a.utf_abs = a.toc_offset + 16;
    a.utf_len = le64(th + 8);
    if (a.utf_len < 0x20 || a.utf_len > (64u << 20) || 16 + a.utf_len > a.toc_size || a.utf_abs + a.utf_len > a.file_size) {
        a.err = base + ": bad TOC chunk size";
        return a;
    }
    Bytes tb(a.utf_len);
    if (!read_exact(io, base, a.utf_abs, tb.data(), tb.size())) { a.err = base + ": short read"; return a; }
    if (!parse_utf(tb.data(), tb.size(), a.toc, e)) { a.err = base + ": TOC " + e; return a; }
    const UtfColumn* c_dir = a.toc.col("DirName");
    const UtfColumn* c_name = a.toc.col("FileName");
    const UtfColumn* c_fs = a.toc.col("FileSize");
    const UtfColumn* c_es = a.toc.col("ExtractSize");
    const UtfColumn* c_fo = a.toc.col("FileOffset");
    if (!c_name || !c_fs || !c_es || !c_fo) { a.err = base + ": TOC lacks FileName/FileSize/ExtractSize/FileOffset"; return a; }
    for (uint32_t r = 0; r < a.toc.nrows; ++r) {
        TocRow row;
        std::string dir, name;
        if (c_dir && a.toc.at(*c_dir, r) && !a.toc.str_at(a.toc.at(*c_dir, r), dir)) { a.err = base + ": bad DirName"; return a; }
        if (!a.toc.str_at(a.toc.at(*c_name, r), name)) { a.err = base + ": bad FileName"; return a; }
        row.path = dir.empty() ? name : dir + "/" + name;
        if (!a.toc.uint_at(*c_fs, r, row.size) || !a.toc.uint_at(*c_es, r, row.extract) || !a.toc.uint_at(*c_fo, r, row.offset)) {
            a.err = base + ": bad size or offset in row " + std::to_string(r);
            return a;
        }
        if (c_fs->storage == 5 && c_es->storage == 5 && c_fo->storage == 5) {
            row.pos_size = a.toc.at(*c_fs, r);
            row.pos_extract = a.toc.at(*c_es, r);
            row.pos_offset = a.toc.at(*c_fo, r);
        }
        a.index.emplace(row.path, a.rows.size());
        a.rows.push_back(std::move(row));
    }
    // Every column the rewrite touches is a fixed-width per-row integer of the expected width.
    a.ok = c_fs->storage == 5 && c_es->storage == 5 && c_fo->storage == 5 && (c_fs->type == 4 || c_fs->type == 5) &&
           (c_es->type == 4 || c_es->type == 5) && (c_fo->type == 6 || c_fo->type == 7);
    if (!a.ok) { a.err = base + ": TOC sizes/offsets are not per-row u32/u32/u64 (cannot rewrite)"; return a; }
    jpsm::Sha256 s;
    Bytes buf(1 << 16);
    for (uint64_t o = 0, end = a.utf_abs + a.utf_len; o < end;) {
        const size_t k = size_t(std::min<uint64_t>(buf.size(), end - o));
        if (!read_exact(io, base, o, buf.data(), k)) { a.ok = false; a.err = base + ": short read"; return a; }
        s.update(buf.data(), k);
        o += k;
    }
    a.prefix = s.finish();
    return a;
}

// ---- the virtual file of one archive ---------------------------------------------------------------------------------------------
struct Segment {
    enum Kind { Original, Memory, ModFile, Zero } kind;
    uint64_t virt, len;
    uint64_t src;      // Original: offset in the archive; ModFile: offset in the file; Memory: offset in Plan::toc
    std::string rel;   // ModFile: path relative to the mods folder
};
struct PlannedOverride {
    std::string entry, mod_id, source;   // source: "files/..." or the alias donor entry
    bool alias = false;
    uint64_t orig_len = 0, len = 0, virt = 0;
};
struct Plan {
    std::string archive;
    uint64_t orig_size = 0, virt_size = 0, toc_offset = 0, utf_abs = 0;
    Digest prefix{};
    Bytes toc;   // the rewritten TOC @UTF table, masked as the original was
    std::vector<Segment> segs;
    std::vector<PlannedOverride> overrides;

    // Copy up to n bytes of the virtual file at pos; returns the count (short at the virtual end or on a source's short read).
    size_t read(Io& io, uint64_t pos, void* dst, size_t n) const
    {
        uint8_t* d = (uint8_t*)dst;
        size_t done = 0;
        auto it = std::upper_bound(segs.begin(), segs.end(), pos, [](uint64_t p, const Segment& s) { return p < s.virt; });
        if (it == segs.begin()) return 0;
        --it;
        while (done < n && it != segs.end()) {
            const uint64_t p = pos + done;
            if (p < it->virt || p >= it->virt + it->len) { ++it; continue; }
            const uint64_t in = p - it->virt;
            const size_t k = size_t(std::min<uint64_t>(n - done, it->len - in));
            size_t got = 0;
            switch (it->kind) {
            case Segment::Original: got = io.archive_read(archive, it->src + in, d + done, k); break;
            case Segment::ModFile: got = io.mod_read(it->rel, it->src + in, d + done, k); break;
            case Segment::Memory: memcpy(d + done, toc.data() + it->src + in, k); got = k; break;
            case Segment::Zero: memset(d + done, 0, k); got = k; break;
            }
            done += got;
            if (got < k) break;
            ++it;
        }
        return done;
    }
};

// ---- the mod set ------------------------------------------------------------------------------------------------------------------
enum class DigestKind { None, Value, Refused };
// Fixed value for "gameplay mods were listed but not all applied": SHA-256("jsp-modset 1\nrefused\n"). Online code must refuse on
// DigestKind::Refused rather than compare it (two refusing peers would otherwise match).
inline Digest refused_digest()
{
    static const char k[] = "jsp-modset 1\nrefused\n";
    return jpsm::sha256((const uint8_t*)k, sizeof k - 1);
}

struct ResolvedOverride {
    OverrideSpec spec;
    std::string archive;
    bool cosmetic = false;
    uint64_t orig_len = 0, len = 0;
    uint64_t src = 0;      // alias: offset of the donor's bytes in the archive
    Digest content{};      // SHA-256 of the bytes served (for an alias, the donor's)
    bool served = false;   // after conflicts
};
struct ModState {
    std::string id;
    int line = 0;
    bool manifest_ok = false;   // mod.json parsed and checked in full
    bool loaded = false;   // manifest valid and every override resolved
    bool applied = false;  // at least one override served
    bool gameplay = true;  // derived class (refused mods that are not provably cosmetic stay gameplay)
    std::string reason;    // why it was refused
    Manifest m;
    std::vector<ResolvedOverride> ovr;
};
struct Options {
    std::string list_name = "mods.txt";   // for the log line only
    std::string title_id;                 // e.g. "BLUS31519"; empty = do not check game.title_ids
    std::string eboot_id;                 // region and version as the port detects it (digest input)
    std::vector<Interface> supported = supported_interfaces();
};
struct ModSet {
    std::vector<ModState> mods;
    std::map<std::string, Plan> plans;   // by archive basename
    DigestKind digest_kind = DigestKind::None;
    Digest digest{};
    std::vector<std::string> log;
};

inline bool hash_source(Io& io, bool archive, const std::string& name, uint64_t off, uint64_t len, Digest& out)
{
    jpsm::Sha256 s;
    Bytes buf(1 << 16);
    for (uint64_t o = 0; o < len;) {
        const size_t k = size_t(std::min<uint64_t>(buf.size(), len - o));
        const size_t got = archive ? io.archive_read(name, off + o, buf.data(), k) : io.mod_read(name, off + o, buf.data(), k);
        if (got != k) return false;
        s.update(buf.data(), k);
        o += k;
    }
    out = s.finish();
    return true;
}

// The slot of an entry: its stored size rounded up to the archive's 2048-byte alignment. A zero-size row has no slot.
inline uint64_t slot_of(const TocRow& row) { return align_up(row.size ? row.size : row.extract); }
// True when `len` bytes can be written over the entry's own slot: it fits, the slot lies inside the content area (at or after
// ContentOffset, before the ETOC, or the end of the file when there is none) and no other row's slot overlaps it (rows that share an offset).
inline bool fits_in_place(const Archive& a, const TocRow& row, uint64_t len)
{
    const uint64_t slot = slot_of(row);
    if (slot == 0 || len > slot) return false;
    const uint64_t at = a.toc_offset + row.offset;
    const uint64_t limit = a.etoc_offset && a.etoc_offset <= a.file_size ? a.etoc_offset : a.file_size;
    if (at < a.content_offset || at + slot > limit) return false;
    for (const TocRow& o : a.rows)
        if (&o != &row && a.toc_offset + o.offset < at + slot && a.toc_offset + o.offset + slot_of(o) > at) return false;
    return true;
}

// Resolve one mod's overrides against the archives (rows, donors, files, hashes). false + reason = refuse the mod.
inline bool resolve_mod(Io& io, ModState& st, std::map<std::string, Archive>& archives, std::string& err)
{
    for (const OverrideSpec& s : st.m.overrides) {
        ResolvedOverride r;
        r.spec = s;
        r.archive = archive_of(s.entry);
        r.cosmetic = is_cosmetic_path(s.entry);
        auto ai = archives.find(r.archive);
        if (ai == archives.end()) ai = archives.emplace(r.archive, load_archive(io, r.archive)).first;
        const Archive& a = ai->second;
        if (!a.ok) { err = a.err; return false; }
        const auto row = a.index.find(s.entry);
        if (row == a.index.end()) {
            err = "entry " + s.entry + " is not in " + r.archive;
            for (const auto& kv : a.index)
                if (lower(kv.first) == lower(s.entry)) { err += " (did you mean " + kv.first + "?)"; break; }
            return false;
        }
        r.orig_len = a.rows[row->second].size;
        if (!s.alias.empty()) {
            if (archive_of(s.alias) != r.archive) { err = "entry " + s.entry + ": alias " + s.alias + " is in another archive (not supported)"; return false; }
            const auto dr = a.index.find(s.alias);
            if (dr == a.index.end()) {
                err = "entry " + s.entry + ": alias " + s.alias + " is not in " + r.archive;
                for (const auto& kv : a.index)
                    if (lower(kv.first) == lower(s.alias)) { err += " (did you mean " + kv.first + "?)"; break; }
                return false;
            }
            const TocRow& d = a.rows[dr->second];
            if (d.size != d.extract) { err = "entry " + s.entry + ": alias " + s.alias + " is CRI-compressed (not supported)"; return false; }
            r.len = d.size;
            r.src = a.toc_offset + d.offset;
            if (r.src + r.len > a.file_size) { err = "entry " + s.entry + ": alias " + s.alias + " runs past its archive"; return false; }
            if (!hash_source(io, true, r.archive, r.src, r.len, r.content)) { err = "entry " + s.entry + ": cannot read alias " + s.alias; return false; }
        } else {
            const std::string rel = st.id + "/" + s.file;
            if (!io.mod_size(rel, r.len)) {
                err = "entry " + s.entry + ": file " + s.file + " is missing";
                const std::string w = io.mod_why(rel);
                if (!w.empty()) err += " (" + w + ")";
                return false;
            }
            if (!hash_source(io, false, rel, 0, r.len, r.content)) { err = "entry " + s.entry + ": cannot read " + s.file; return false; }
            if (hex(r.content) != s.sha256) { err = "entry " + s.entry + ": " + s.file + " does not match its sha256"; return false; }
        }
        if (r.len == 0) { err = "entry " + s.entry + ": the replacement is empty"; return false; }
        if (r.len > 0xFFFFFFFFull) { err = "entry " + s.entry + ": the replacement is 4 GiB or more"; return false; }
        if (!fits_in_place(a, a.rows[row->second], r.len)) {
            const uint64_t slot = slot_of(a.rows[row->second]);
            err = "entry " + s.entry + ": the replacement is " + std::to_string(r.len) + " B, but its slot in " + r.archive + " is " + std::to_string(slot) +
                  " B (the entry's size rounded up to 2048; a zero-size row has no slot, and rows that share an offset have none either): " +
                  "a larger replacement is not supported yet";
            return false;
        }
        st.ovr.push_back(std::move(r));
    }
    return true;
}

// Put `ov` over [ov.virt, ov.virt + ov.len) of a segment list that covers the virtual file without gaps (splits what it overlaps).
inline void overlay_segment(std::vector<Segment>& segs, const Segment& ov)
{
    std::vector<Segment> out;
    const uint64_t a = ov.virt, b = ov.virt + ov.len;
    bool put = false;
    for (const Segment& g : segs) {
        const uint64_t ga = g.virt, gb = g.virt + g.len;
        if (gb <= a || ga >= b) {
            if (ga >= b && !put) { out.push_back(ov); put = true; }
            out.push_back(g);
            continue;
        }
        if (ga < a) { Segment l = g; l.len = a - ga; out.push_back(l); }
        if (!put) { out.push_back(ov); put = true; }
        if (gb > b) {
            Segment r = g;
            r.virt = b;
            r.len = gb - b;
            if (g.kind != Segment::Zero) r.src = g.src + (b - ga);
            out.push_back(r);
        }
    }
    if (!put) out.push_back(ov);
    segs.swap(out);
}

// Every served override is in place (resolve_mod refused the ones that do not fit): the TOC rows get the new FileSize and ExtractSize, the
// data goes over the entry's slot, the rest of the slot reads as zeros, and the archive keeps its size.
inline Plan build_plan(const Archive& a, const std::vector<std::pair<const ModState*, const ResolvedOverride*>>& served)
{
    Plan p;
    p.archive = a.base;
    p.orig_size = a.file_size;
    p.virt_size = a.file_size;
    p.toc_offset = a.toc_offset;
    p.utf_abs = a.utf_abs;
    p.prefix = a.prefix;
    p.toc = a.toc.b;   // unmasked copy
    std::vector<std::pair<const ModState*, const ResolvedOverride*>> order = served;
    std::sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.second->spec.entry < y.second->spec.entry; });
    std::vector<Segment> over;
    for (const auto& [ms, r] : order) {
        const TocRow& row = a.rows[a.index.at(r->spec.entry)];
        const uint64_t virt = a.toc_offset + row.offset, slot = slot_of(row);
        over.push_back(r->spec.alias.empty() ? Segment{ Segment::ModFile, virt, r->len, 0, ms->id + "/" + r->spec.file }
                                             : Segment{ Segment::Original, virt, r->len, r->src, "" });
        if (r->len < slot) over.push_back({ Segment::Zero, virt + r->len, slot - r->len, 0, "" });
        put_be32(&p.toc[row.pos_size], uint32_t(r->len));
        put_be32(&p.toc[row.pos_extract], uint32_t(r->len));
        PlannedOverride po;
        po.entry = r->spec.entry;
        po.mod_id = ms->id;
        po.alias = !r->spec.alias.empty();
        po.source = po.alias ? r->spec.alias : r->spec.file;
        po.orig_len = r->orig_len;
        po.len = r->len;
        po.virt = virt;
        p.overrides.push_back(po);
    }
    if (a.toc.masked) mask(p.toc.data(), p.toc.size());
    p.segs.push_back({ Segment::Original, 0, a.utf_abs, 0, "" });
    p.segs.push_back({ Segment::Memory, a.utf_abs, a.utf_len, 0, "" });
    if (a.file_size > a.utf_abs + a.utf_len)
        p.segs.push_back({ Segment::Original, a.utf_abs + a.utf_len, a.file_size - (a.utf_abs + a.utf_len), a.utf_abs + a.utf_len, "" });
    for (const Segment& g : over) overlay_segment(p.segs, g);
    return p;
}

// Read every listed mod, resolve, merge in load order, plan and digest. `list_text` is mods.txt's content.
inline ModSet load_mod_set(Io& io, const std::string& list_text, const Options& opt = Options())
{
    ModSet set;
    auto& log = set.log;
    const std::vector<ListEntry> list = parse_mods_list(list_text, log);
    {
        std::string ids;
        for (const auto& e : list) ids += (ids.empty() ? "" : ", ") + e.id;
        log.push_back(fmt("[mods] list %s: %llu enabled%s%s%s", opt.list_name.c_str(), (unsigned long long)list.size(), ids.empty() ? "" : " (",
                          ids.c_str(), ids.empty() ? "" : ")"));
    }
    std::map<std::string, Archive> archives;
    for (const ListEntry& e : list) {
        ModState st;
        st.id = e.id;
        st.line = e.line;
        std::string err;
        if (!e.valid) err = "not a mod id";
        else {
            uint64_t sz = 0;
            const std::string rel = e.id + "/mod.json";
            std::string text;
            if (!io.mod_size(rel, sz)) { err = "no mod.json"; const std::string w = io.mod_why(rel); if (!w.empty()) err += " (" + w + ")"; }
            else if (sz > (1u << 20)) err = "mod.json is larger than 1 MiB";
            else {
                text.resize(size_t(sz));
                if (io.mod_read(rel, 0, text.data(), text.size()) != text.size()) err = "cannot read mod.json";
                else if ((st.manifest_ok = parse_manifest(text, e.id, opt.supported, opt.title_id, st.m, err)) &&
                         resolve_mod(io, st, archives, err))
                    st.loaded = true;
            }
        }
        // Class: gameplay unless the whole manifest parsed and every part of it is provably cosmetic.
        bool cosmetic = st.manifest_ok && !st.m.gameplay;
        for (const auto& i : st.m.interfaces) if (i.name != "cpk-entry") cosmetic = false;
        for (const auto& o : st.m.overrides) if (!is_cosmetic_path(o.entry)) cosmetic = false;
        st.gameplay = !cosmetic;
        if (!st.loaded) {
            st.reason = err;
            log.push_back(fmt("[mods] %s: refused: %s", st.id.c_str(), err.c_str()));
        } else {
            bool any_alias = false, any_file = false;
            for (const auto& r : st.ovr) (r.spec.alias.empty() ? any_file : any_alias) = true;
            std::string ifs;
            for (const auto& i : st.m.interfaces) ifs += fmt("%s%s %d", ifs.empty() ? "" : ", ", i.name.c_str(), i.version);
            log.push_back(fmt("[mods] %s %s: %s, %llu override%s%s, interfaces %s", st.id.c_str(), st.m.version.c_str(),
                              st.gameplay ? "gameplay" : "cosmetic", (unsigned long long)st.ovr.size(), st.ovr.size() == 1 ? "" : "s",
                              any_alias && any_file ? " (alias, file)" : any_alias ? " (alias)" : any_file ? " (file)" : "",
                              ifs.empty() ? "none" : ifs.c_str()));
        }
        set.mods.push_back(std::move(st));
    }

    // Conflicts: the later mod wins an entry; a group that loses an entry is dropped whole.
    std::map<std::string, std::pair<size_t, size_t>> owner;   // entry -> (mod index, override index)
    for (size_t mi = 0; mi < set.mods.size(); ++mi) {
        ModState& st = set.mods[mi];
        if (!st.loaded) continue;
        for (size_t oi = 0; oi < st.ovr.size(); ++oi) {
            const std::string& entry = st.ovr[oi].spec.entry;
            const auto it = owner.find(entry);
            if (it != owner.end()) {
                const ModState& prev = set.mods[it->second.first];
                const bool ok = std::find(st.m.conflicts_ok.begin(), st.m.conflicts_ok.end(), prev.id) != st.m.conflicts_ok.end();
                if (!ok) log.push_back(fmt("[mods] conflict %s: %s overridden by %s", entry.c_str(), prev.id.c_str(), st.id.c_str()));
            }
            owner[entry] = { mi, oi };
        }
    }
    for (const auto& kv : owner) set.mods[kv.second.first].ovr[kv.second.second].served = true;
    for (ModState& st : set.mods) {
        if (!st.loaded) continue;
        for (const auto& g : st.m.groups) {
            bool lost = false;
            for (const auto& e : g)
                for (const auto& r : st.ovr) if (r.spec.entry == e && !r.served) lost = true;
            if (!lost) continue;
            for (const auto& e : g)
                for (auto& r : st.ovr)
                    if (r.spec.entry == e && r.served) {
                        r.served = false;
                        owner.erase(e);   // back to stock: the later mod did not override it
                    }
            log.push_back(fmt("[mods] %s: group of %llu dropped, a later mod overrides part of it (%s ...)", st.id.c_str(), (unsigned long long)g.size(),
                              g.front().c_str()));
        }
        for (const auto& r : st.ovr) if (r.served) st.applied = true;
    }

    // Plans, one per archive with served overrides.
    std::map<std::string, std::vector<std::pair<const ModState*, const ResolvedOverride*>>> per;
    for (const ModState& st : set.mods)
        for (const auto& r : st.ovr) if (r.served) per[r.archive].push_back({ &st, &r });
    for (const auto& kv : per) {
        Plan p = build_plan(archives.at(kv.first), kv.second);
        for (const auto& o : p.overrides)
            log.push_back(fmt("[mods] override %s %s <- %s %s (%llu -> %llu B, virtual 0x%llX)", p.archive.c_str(), o.entry.c_str(),
                              o.alias ? "alias" : o.mod_id.c_str(), o.source.c_str(), (unsigned long long)o.orig_len,
                              (unsigned long long)o.len, (unsigned long long)o.virt));
        set.plans.emplace(kv.first, std::move(p));
    }

    // Digest.
    bool refused_gameplay = false, any_gameplay = false;
    jpsm::Sha256 h;
    const std::string head = "jsp-modset 1\neboot " + opt.eboot_id + "\n";
    h.update((const uint8_t*)head.data(), head.size());
    for (const ModState& st : set.mods) {
        if (!st.loaded) { if (st.gameplay) refused_gameplay = true; continue; }
        if (!st.gameplay || !st.applied) continue;
        any_gameplay = true;
        std::string l = st.id + " " + st.m.version + "\n";
        for (const auto& r : st.ovr)
            if (r.served) l += r.spec.entry + " " + hex(r.content) + "\n";
        h.update((const uint8_t*)l.data(), l.size());
    }
    if (refused_gameplay) {
        set.digest_kind = DigestKind::Refused;
        set.digest = refused_digest();
        log.push_back("[mods] gameplay mods requested but not applied on this build: online refused");
    } else if (any_gameplay) {
        set.digest_kind = DigestKind::Value;
        set.digest = h.finish();
        log.push_back(fmt("[mods] set: gameplay digest %s", hex(set.digest).substr(0, 16).c_str()));
    } else {
        log.push_back(fmt("[mods] set: %s, no gameplay digest", set.plans.empty() ? "no overrides" : "cosmetic only"));
    }
    return set;
}

}  // namespace jmods

#endif  // JSP_MODS_CORE_H
