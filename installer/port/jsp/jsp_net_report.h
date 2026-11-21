// jsp_net_report.h -- DIAGNOSTIC (rollback, online): the match report. When an online rollback match ends badly (desync, session failure,
// crash) the game writes ONE small plain-text file the tester can send back: build, settings in effect, session facts, end reason, the last
// per-tick commits and rollbacks, free-form notes. Pure C++17 and the C library, no game or runtime headers, so
// installer/tools/net_report_test.cpp runs it on the host. The only platform code is the file open (wide path on Windows) and the local time.
//
// Use (net layer):
//   jsnr::Report rep; rep.set_build(id); rep.add_setting("JSP_ROLLBACK_DELAY", "2"); rep.set_session({...}); rep.add_redaction(peer_name);
//   per committed tick: rep.commit(tick, hash, raw); per rollback: rep.rollback(from_tick, depth); anything else: rep.note("...");
//   at the bad end: rep.set_end_reason("desync at tick 1234"); rep.write(dir, jsnr::now_local());   -> the full path, or "" on failure
//
// Privacy: the file is meant to be posted publicly. Every free-form string (setting values, end reason, notes, link path) goes through
// sanitize(): IPv4 and IPv6 addresses become "<ip>" (loopback and the unspecified address stay: they say nothing about a person), a user's
// home folder ("C:\Users\<name>", "/mnt/c/Users/<name>", "/home/<name>", "/Users/<name>") becomes "%USERPROFILE%", and every word given to
// add_redaction() (login name, machine name, online names: add_machine_redactions() adds the first two) becomes "<user name>".
// Setting keys and the build id are written as given (the caller's own constants). Control characters become spaces (one line per value).
//
// Size: rings of kRingLen (120) commits and rollbacks, at most kMaxSettings settings and kMaxNotes notes of at most kMaxText bytes each;
// render() is cut at a line end below kMaxBytes (64 KB) whatever the input, with a "[truncated]" line.
#pragma once
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace jsnr {

inline constexpr size_t kRingLen = 120;
inline constexpr size_t kMaxSettings = 48;
inline constexpr size_t kMaxNotes = 48;
inline constexpr size_t kMaxText = 400;        // one setting value, note, end reason (after sanitising)
inline constexpr size_t kMaxKey = 64;
inline constexpr size_t kMaxRedactions = 16;
inline constexpr size_t kMaxBytes = 64 * 1024;  // the whole file stays below this
inline constexpr const char* kFormat = "jsp-match-report 1";

// ---- sanitiser -----------------------------------------------------------------------------------------------------------------------

namespace detail {
inline bool is_dig(char c) { return c >= '0' && c <= '9'; }
inline bool is_hex(char c) { return is_dig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline bool is_word(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
inline bool is_sep(char c) { return c == '\\' || c == '/'; }

// IPv4 at s[i]: four 0..255 groups; not glued to a digit or a dotted number on either side. Returns its length (with ":port"), or 0.
inline size_t ipv4_at(const std::string& s, size_t i, bool* keep)
{
    if (i > 0 && (is_dig(s[i - 1]) || s[i - 1] == '.')) return 0;
    size_t p = i;
    int oct[4];
    for (int g = 0; g < 4; g++) {
        if (g) { if (p >= s.size() || s[p] != '.') return 0; p++; }
        size_t q = p; int v = 0;
        while (q < s.size() && is_dig(s[q]) && q - p < 3) v = v * 10 + (s[q++] - '0');
        if (q == p || (q < s.size() && is_dig(s[q])) || v > 255) return 0;
        if (q - p > 1 && s[p] == '0') return 0;   // "01": not an address
        oct[g] = v; p = q;
    }
    if (p + 1 < s.size() && s[p] == '.' && is_dig(s[p + 1])) return 0;   // 1.2.3.4.5: a version, not an address
    *keep = oct[0] == 127 || (oct[0] == 0 && oct[1] == 0 && oct[2] == 0 && oct[3] == 0);
    if (p + 1 < s.size() && s[p] == ':' && is_dig(s[p + 1])) { p++; while (p < s.size() && is_dig(s[p])) p++; }
    return p - i;
}

// IPv6-ish at s[i]: a run of hex digits and ':' (and '.' for an embedded IPv4, '%zone') with "::" or at least six ':' and hex groups of
// at most four digits. "12:34:56" (a time) is not one. Returns its length, or 0.
inline size_t ipv6_at(const std::string& s, size_t i, bool* keep)
{
    if (i > 0 && (is_word(s[i - 1]) || s[i - 1] == ':' || s[i - 1] == '.')) return 0;
    size_t p = i, colons = 0, group = 0;
    bool dbl = false;
    while (p < s.size()) {
        const char c = s[p];
        if (is_hex(c)) { if (++group > 4) return 0; }
        else if (c == ':') { colons++; if (p > i && s[p - 1] == ':') dbl = true; group = 0; }
        else if (c == '.' && colons >= 2 && is_dig(p > i ? s[p - 1] : 'x')) { group = 0; }
        else break;
        p++;
    }
    while (p > i && s[p - 1] == '.') p--;   // a full stop after the address is not part of it
    if (colons < 2 || (!dbl && colons < 6)) return 0;
    if (p < s.size() && (is_word(s[p]) && s[p] != '%')) return 0;
    const std::string tok = s.substr(i, p - i);
    if (p < s.size() && s[p] == '%') { p++; while (p < s.size() && is_word(s[p])) p++; }   // zone id
    *keep = tok == "::1" || tok == "::";
    return p - i;
}

// A home folder at s[i] ("X:\Users\<name>", "\\?\X:\Users\<name>", "/mnt/x/Users/<name>", "/home/<name>", "/Users/<name>", either separator).
// Returns the length through the name (not the separator after it), or 0.
inline size_t home_at(const std::string& s, size_t i)
{
    size_t p = i;
    auto word = [&](const char* w) {
        size_t q = p;
        for (; *w; w++, q++) if (q >= s.size() || lower(s[q]) != *w) return false;
        p = q; return true;
    };
    if (i > 0 && (is_word(s[i - 1]) || s[i - 1] == ':')) return 0;
    if (p + 2 < s.size() && std::isalpha((unsigned char)s[p]) && s[p + 1] == ':' && is_sep(s[p + 2])) p += 3;   // X:\ ...
    else if (!is_sep(s[p])) return 0;
    else if (word("/mnt/") && p + 1 < s.size() && std::isalpha((unsigned char)s[p]) && is_sep(s[p + 1])) p += 2;   // /mnt/x/ ...
    else p = i + 1;                                                                                                 // /home/, /Users/
    const size_t at = p;
    if (!word("users")) { p = at; if (!word("home")) return 0; }
    if (p >= s.size() || !is_sep(s[p])) return 0;
    p++;
    const size_t name = p;
    while (p < s.size() && !is_sep(s[p]) && s[p] != '"' && s[p] != '\'' && s[p] != '\n') p++;   // names may hold spaces
    if (p == name) return 0;
    if (p == s.size() && p - name > 40) {   // no separator after it: a name followed by prose, cut at the first space
        size_t q = name; while (q < p && s[q] != ' ') q++; p = q;
    }
    return p - i;
}

inline bool ieq_at(const std::string& s, size_t i, const std::string& w)
{
    if (i + w.size() > s.size()) return false;
    for (size_t k = 0; k < w.size(); k++) if (lower(s[i + k]) != lower(w[k])) return false;
    return true;
}
}  // namespace detail

// Placeholders for addresses, home folders and the redaction words; control characters become spaces; cut to max_len bytes (at a UTF-8
// character start). Redaction words match case-insensitively as whole words (not inside a longer word); words shorter than 3 are ignored.
inline std::string sanitize(const std::string& in, const std::vector<std::string>& redact = {}, size_t max_len = kMaxText)
{
    std::string s;
    s.reserve(in.size());
    for (char c : in) s.push_back(((unsigned char)c < 0x20 || c == 0x7f) ? ' ' : c);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        bool keep = false;
        size_t n = detail::home_at(s, i);
        if (n) { out += "%USERPROFILE%"; i += n; continue; }
        if ((n = detail::ipv4_at(s, i, &keep)) || (n = detail::ipv6_at(s, i, &keep))) {
            out += keep ? s.substr(i, n) : std::string("<ip>"); i += n; continue;
        }
        bool hit = false;
        if (i == 0 || !detail::is_word(s[i - 1])) {
            for (const std::string& w : redact) {
                if (w.size() < 3 || !detail::ieq_at(s, i, w)) continue;
                if (i + w.size() < s.size() && detail::is_word(s[i + w.size()])) continue;
                out += "<user name>"; i += w.size(); hit = true; break;
            }
        }
        if (hit) continue;
        out.push_back(s[i++]);
    }
    if (out.size() > max_len) {
        size_t cut = max_len;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;
        out.resize(cut);
    }
    return out;
}

// ---- fixed ring ----------------------------------------------------------------------------------------------------------------------

template <typename T, size_t N>
class Ring {
public:
    void push(const T& v) { buf_[(size_t)(total_ % N)] = v; total_++; }
    size_t size() const { return total_ < N ? (size_t)total_ : N; }
    uint64_t total() const { return total_; }                       // ever pushed (total() - size() were dropped)
    const T& at(size_t i) const { return buf_[(size_t)((total_ - size() + i) % N)]; }   // 0 = oldest kept
    void clear() { total_ = 0; }
private:
    std::array<T, N> buf_{};
    uint64_t total_ = 0;
};

// ---- the report ----------------------------------------------------------------------------------------------------------------------

struct CommitLine { uint32_t tick = 0; uint64_t hash = 0; uint64_t raw = 0; };   // raw: the caller's packed input word(s) of the tick
struct RollbackEvent { uint32_t from_tick = 0; uint32_t depth = 0; };             // from_tick: the tick the game was on; depth: ticks re-run

struct SessionFacts {
    int local_slot = -1;        // -1: unknown (no session started)
    int players = 0;
    int input_delay = -1;
    uint32_t base_tick = 0;
    std::string link;           // "direct" or "relay" (sanitised anyway)
};

struct Stamp { int year = 1970, month = 1, day = 1, hour = 0, min = 0, sec = 0; };

inline std::string stamp_text(const Stamp& t)
{
    char b[48]; std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.min, t.sec); return b;
}
// "jsp-match-report-YYYYMMDD-HHMMSS.txt"; n > 1 adds "-n" before ".txt" (a second report in the same second).
inline std::string file_name(const Stamp& t, int n = 1)
{
    char b[80];
    std::snprintf(b, sizeof b, "jsp-match-report-%04d%02d%02d-%02d%02d%02d", t.year, t.month, t.day, t.hour, t.min, t.sec);
    std::string s = b;
    if (n > 1) s += "-" + std::to_string(n);
    return s + ".txt";
}

class Report {
public:
    void set_build(const std::string& id) { build_ = clip(id, kMaxText); }
    void add_setting(const std::string& key, const std::string& value)
    {
        if (settings_.size() >= kMaxSettings) { settings_dropped_++; return; }
        settings_.emplace_back(clip(key, kMaxKey), value);
    }
    void set_session(const SessionFacts& f) { session_ = f; have_session_ = true; }
    void set_end_reason(const std::string& why) { end_ = why; }
    void commit(uint32_t tick, uint64_t hash, uint64_t raw) { commits_.push(CommitLine{tick, hash, raw}); }
    void rollback(uint32_t from_tick, uint32_t depth)
    {
        rollbacks_.push(RollbackEvent{from_tick, depth});
        if (depth > max_depth_) max_depth_ = depth;
        depth_sum_ += depth;
    }
    void note(const std::string& line)
    {
        if (notes_.size() >= kMaxNotes) { notes_dropped_++; return; }
        notes_.push_back(line);
    }
    // A word to replace with "<user name>" wherever it stands as a word (online names, the peer's name). Applied at render time, so a
    // redaction added after the text it covers still applies.
    void add_redaction(const std::string& w)
    {
        std::string t = clip(w, kMaxKey);
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (t.size() >= 3 && redact_.size() < kMaxRedactions) redact_.push_back(t);
    }
    // The login and machine names from the environment (USERNAME / USER, COMPUTERNAME / HOSTNAME).
    void add_machine_redactions()
    {
        for (const char* k : {"USERNAME", "USER", "COMPUTERNAME", "HOSTNAME"}) {
            const char* v = std::getenv(k);
            if (v && *v) add_redaction(v);
        }
    }
    void clear() { *this = Report(); }

    const Ring<CommitLine, kRingLen>& commits() const { return commits_; }
    const Ring<RollbackEvent, kRingLen>& rollbacks() const { return rollbacks_; }

    // The whole file. Always < kMaxBytes.
    std::string render(const Stamp& when) const
    {
        std::string o;
        o.reserve(16 * 1024);
        char b[160];
        auto line = [&](const std::string& s) { o += s; o += '\n'; };
        auto clean = [&](const std::string& s) { return sanitize(s, redact_); };

        line(std::string("# ") + kFormat);
        line("# J-Stars Victory VS+ port: online rollback match report. Safe to share: addresses, home folders and names are replaced.");
        line("written: " + stamp_text(when) + " (local time)");
        line("build: " + (build_.empty() ? std::string("(unknown)") : build_));
        line("");
        line("[end]");
        line("reason: " + (end_.empty() ? std::string("(not given)") : clean(end_)));
        line("");
        line("[session]");
        if (!have_session_) line("(no session started)");
        else {
            std::snprintf(b, sizeof b, "local_slot: %d\nplayers: %d\ninput_delay: %d\nbase_tick: %u", session_.local_slot, session_.players,
                          session_.input_delay, (unsigned)session_.base_tick);
            line(b);
            line("link: " + (session_.link.empty() ? std::string("(unknown)") : sanitize(session_.link, redact_, kMaxKey)));
        }
        line("");
        line("[settings]");
        if (settings_.empty()) line("(none)");
        for (const auto& kv : settings_) line(one_line(kv.first) + "=" + clean(kv.second));
        if (settings_dropped_) line("(" + std::to_string(settings_dropped_) + " more not kept)");
        line("");
        line("[rollbacks]");
        std::snprintf(b, sizeof b, "count: %llu  max_depth: %u  avg_depth: %.2f  kept: %zu (oldest first: from_tick depth)",
                      (unsigned long long)rollbacks_.total(), (unsigned)max_depth_,
                      rollbacks_.total() ? (double)depth_sum_ / (double)rollbacks_.total() : 0.0, rollbacks_.size());
        line(b);
        for (size_t i = 0; i < rollbacks_.size(); i++) {
            const RollbackEvent& e = rollbacks_.at(i);
            std::snprintf(b, sizeof b, "%u %u", (unsigned)e.from_tick, (unsigned)e.depth);
            line(b);
        }
        line("");
        line("[commits]");
        std::snprintf(b, sizeof b, "count: %llu  kept: %zu (oldest first: tick hash raw, hex)", (unsigned long long)commits_.total(),
                      commits_.size());
        line(b);
        for (size_t i = 0; i < commits_.size(); i++) {
            const CommitLine& c = commits_.at(i);
            std::snprintf(b, sizeof b, "%u %016llx %016llx", (unsigned)c.tick, (unsigned long long)c.hash, (unsigned long long)c.raw);
            line(b);
        }
        line("");
        line("[notes]");
        if (notes_.empty()) line("(none)");
        for (const std::string& n : notes_) line(clean(n));
        if (notes_dropped_) line("(" + std::to_string(notes_dropped_) + " more not kept)");
        line("");
        line("# end of report");

        if (o.size() >= kMaxBytes) {   // cannot happen with the limits above; a guard all the same
            const std::string tail = "[truncated]\n";
            size_t cut = kMaxBytes - tail.size() - 1;
            while (cut > 0 && o[cut - 1] != '\n') cut--;
            o.resize(cut);
            o += tail;
        }
        return o;
    }

    // Writes render(when) to dir/file_name(when) ("-2".."-9" when that name exists). Returns the full path, or "" (with *err) on failure.
    // dir: UTF-8, '/' or '\' separators, "" = the current folder.
    std::string write(const std::string& dir, const Stamp& when, std::string* err = nullptr) const
    {
        const std::string body = render(when);
        std::string base = dir;
        if (!base.empty() && !detail::is_sep(base.back())) base += '/';
        for (int n = 1; n <= 9; n++) {
            const std::string path = base + file_name(when, n);
            if (std::FILE* f = open_file(path, "rb")) { std::fclose(f); continue; }
            std::FILE* f = open_file(path, "wb");
            if (!f) { if (err) *err = "cannot create " + path; return ""; }
            const bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
            const bool closed = std::fclose(f) == 0;
            if (!ok || !closed) { if (err) *err = "write failed: " + path; std::remove(path.c_str()); return ""; }
            return path;
        }
        if (err) *err = "nine reports already in this second";
        return "";
    }

private:
    static std::string clip(const std::string& s, size_t n) { return one_line(s.size() > n ? s.substr(0, n) : s); }
    static std::string one_line(std::string s) { for (char& c : s) if ((unsigned char)c < 0x20 || c == 0x7f) c = ' '; return s; }

    static std::FILE* open_file(const std::string& path, const char* mode)
    {
#ifdef _WIN32
        auto wide = [](const std::string& u) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
            std::wstring w(n > 0 ? (size_t)n : 1, L'\0');
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, &w[0], n);
            return w;
        };
        return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
        return std::fopen(path.c_str(), mode);
#endif
    }

    std::string build_;
    std::vector<std::pair<std::string, std::string>> settings_;
    size_t settings_dropped_ = 0;
    SessionFacts session_;
    bool have_session_ = false;
    std::string end_;
    Ring<CommitLine, kRingLen> commits_;
    Ring<RollbackEvent, kRingLen> rollbacks_;
    uint32_t max_depth_ = 0;
    uint64_t depth_sum_ = 0;
    std::vector<std::string> notes_;
    size_t notes_dropped_ = 0;
    std::vector<std::string> redact_;
};

// The local time now (the caller passes it to write(); tests pass a fixed Stamp).
inline Stamp now_local()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return Stamp{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

}  // namespace jsnr
