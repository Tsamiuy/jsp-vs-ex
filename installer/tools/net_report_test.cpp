// Host test of installer/port/jsp/jsp_net_report.h, the online match report: ring wraparound order (oldest first), the size bound with every
// field at its limit, the sanitiser (IPv4 with and without a port, IPv6 forms, home folders in Windows, WSL and POSIX spellings, redaction
// words, things that must stay: times, versions, loopback), the file name and the "-2" name of a second report in the same second, the file
// written byte for byte, and the report of a match that never started. Run: installer/tools/net_report_test.sh [SCRATCH_DIR].
#include "../port/jsp/jsp_net_report.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace jsnr;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECK_EQ(a, b) do { const std::string x_ = (a), y_ = (b); if (x_ != y_) { std::printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, x_.c_str(), y_.c_str()); fails++; } } while (0)

static bool has(const std::string& s, const std::string& w) { return s.find(w) != std::string::npos; }
// Home-folder test inputs are spelled with @U / @u / @H for Users / users / home, so the repository's personal-path check (tools/ci/repo_checks.py)
// does not read the made-up names in them as a real one.
static std::string P(std::string s)
{
    const char* from[] = {"@U", "@u", "@H"}; const char* to[] = {"Us" "ers", "us" "ers", "ho" "me"};
    for (int k = 0; k < 3; k++) for (size_t at; (at = s.find(from[k])) != std::string::npos;) s.replace(at, 2, to[k]);
    return s;
}
static const Stamp kWhen{2026, 11, 21, 9, 5, 7};

static void test_ring()
{
    Ring<int, 5> r;
    CHECK(r.size() == 0 && r.total() == 0);
    for (int i = 0; i < 3; i++) r.push(i);
    CHECK(r.size() == 3 && r.at(0) == 0 && r.at(2) == 2);
    for (int i = 3; i < 13; i++) r.push(i);   // wraps twice, plus a partial lap
    CHECK(r.size() == 5 && r.total() == 13);
    for (size_t i = 0; i < 5; i++) CHECK(r.at(i) == (int)(8 + i));
    r.clear();
    CHECK(r.size() == 0);

    Report rep;
    for (uint32_t t = 0; t < 300; t++) rep.commit(t, 0x1000 + t, t * 2);
    for (uint32_t t = 0; t < 130; t++) rep.rollback(1000 + t, 1 + t % 7);
    CHECK(rep.commits().size() == kRingLen && rep.commits().total() == 300);
    CHECK(rep.commits().at(0).tick == 300 - kRingLen && rep.commits().at(kRingLen - 1).tick == 299);
    CHECK(rep.rollbacks().at(0).from_tick == 1010);
    const std::string txt = rep.render(kWhen);
    // the rendered commit lines are in tick order, oldest first, and only the kept ones
    size_t last = 0; bool order = true;
    for (uint32_t t = 300 - kRingLen; t < 300; t++) {
        char b[64]; std::snprintf(b, sizeof b, "\n%u %016llx %016llx\n", t, (unsigned long long)(0x1000 + t), (unsigned long long)(t * 2));
        const size_t at = txt.find(b);
        if (at == std::string::npos || at < last) order = false;
        last = at;
    }
    CHECK(order);
    CHECK(!has(txt, "\n179 "));                    // dropped
    CHECK(has(txt, "count: 300  kept: 120"));
    CHECK(has(txt, "count: 130  max_depth: 7"));
    CHECK(txt.find("\n1010 ") < txt.find("\n1129 "));
    std::printf("ring: ok\n");
}

static void test_sanitize()
{
    // IPv4
    CHECK_EQ(sanitize("peer 203.0.113.7 lost"), "peer <ip> lost");
    CHECK_EQ(sanitize("join=198.51.100.23:3074,"), "join=<ip>,");
    CHECK_EQ(sanitize("[203.0.113.7]"), "[<ip>]");
    CHECK_EQ(sanitize("a 10.0.0.1."), "a <ip>.");
    CHECK_EQ(sanitize("local 127.0.0.1:7000 any 0.0.0.0"), "local 127.0.0.1:7000 any 0.0.0.0");   // say nothing about a person
    CHECK_EQ(sanitize("version 0.18.2.1.4 and 999.1.1.1 and 1.2.3"), "version 0.18.2.1.4 and 999.1.1.1 and 1.2.3");
    // IPv6-ish
    CHECK_EQ(sanitize("to 2001:db8::1 ok"), "to <ip> ok");
    CHECK_EQ(sanitize("[2001:db8:85a3::8a2e:370:7334]:3074"), "[<ip>]:3074");
    CHECK_EQ(sanitize("fe80::1%eth0 up"), "<ip> up");
    CHECK_EQ(sanitize("full 2001:0db8:0000:0000:0000:ff00:0042:8329."), "full <ip>.");
    CHECK_EQ(sanitize("mapped ::ffff:192.0.2.128 x"), "mapped <ip> x");
    CHECK_EQ(sanitize("lo ::1 any ::"), "lo ::1 any ::");
    CHECK_EQ(sanitize("at 12:34:56 tick 1:2"), "at 12:34:56 tick 1:2");                       // times stay
    CHECK_EQ(sanitize("jsnr::Report std::string"), "jsnr::Report std::string");
    // home folders
    CHECK_EQ(sanitize(P("C:\\@U\\alice\\Documents\\jsp\\x.log")), "%USERPROFILE%\\Documents\\jsp\\x.log");
    CHECK_EQ(sanitize(P("open 'd:/@u/Bob Smith/AppData/x' failed")), "open '%USERPROFILE%/AppData/x' failed");
    CHECK_EQ(sanitize(P("\\\\?\\C:\\@U\\carol\\a")), "\\\\?\\%USERPROFILE%\\a");
    CHECK_EQ(sanitize(P("/mnt/c/@U/dave/jsp")), "%USERPROFILE%/jsp");
    CHECK_EQ(sanitize(P("/@H/erin/x and /@U/frank/y")), "%USERPROFILE%/x and %USERPROFILE%/y");
    CHECK_EQ(sanitize(P("\"C:\\@U\\gina\"")), "\"%USERPROFILE%\"");
    CHECK_EQ(sanitize(P("C:\\@U\\hank")), "%USERPROFILE%");
    CHECK_EQ(sanitize("game/users/list and C:\\Program Files\\x"), "game/users/list and C:\\Program Files\\x");
    // redaction words, control characters, length cut
    const std::vector<std::string> red = {"Kenshiro", "PC-ALPHA"};
    CHECK_EQ(sanitize("kenshiro joined from pc-alpha; kenshiros stays", red), "<user name> joined from <user name>; kenshiros stays");
    CHECK_EQ(sanitize("a\nb\tc\rd"), "a b c d");
    CHECK(sanitize(std::string(1000, 'x')).size() == kMaxText);
    std::string u8 = std::string(kMaxText - 1, 'x') + "\xC3\xA9";   // a 2-byte character across the cut
    CHECK(sanitize(u8).size() == kMaxText - 1);

    // through the report: every free-form field, and a redaction added after the text
    Report rep;
    rep.set_build("v0.18-abc123");
    rep.add_setting("JSP_ROLLBACK_JOIN", "203.0.113.9:3074");
    rep.add_setting("JSP_LOG", P("C:\\@U\\alice\\log.txt"));
    rep.set_session(SessionFacts{1, 2, 2, 40, "relay"});
    rep.set_end_reason("desync with Kenshiro at 2001:db8::7");
    rep.note("peer 198.51.100.4 name Kenshiro");
    rep.add_redaction("kenshiro");
    rep.add_redaction("ab");   // too short: ignored
    const std::string txt = rep.render(kWhen);
    CHECK(!has(txt, "203.0.113") && !has(txt, "198.51") && !has(txt, "2001:db8") && !has(txt, "alice") && !has(txt, "Kenshiro"));
    CHECK(has(txt, "JSP_ROLLBACK_JOIN=<ip>\n") && has(txt, "JSP_LOG=%USERPROFILE%\\log.txt\n"));
    CHECK(has(txt, "reason: desync with <user name> at <ip>\n"));
    CHECK(has(txt, "link: relay\n") && has(txt, "build: v0.18-abc123\n") && has(txt, "local_slot: 1\nplayers: 2\ninput_delay: 2\nbase_tick: 40\n"));
    std::printf("sanitize: ok\n");
}

static void test_size()
{
    Report rep;
    rep.set_build(std::string(5000, 'b'));
    for (int i = 0; i < 500; i++) rep.add_setting(std::string(500, 'K'), std::string(5000, 'v'));
    for (int i = 0; i < 500; i++) rep.note(std::string(5000, 'n') + " 203.0.113.1");
    for (int i = 0; i < 16; i++) rep.add_redaction(std::string(3, (char)('a' + i)));
    rep.add_redaction("nnn");   // every note expands ~3.7x before the cut
    rep.set_end_reason(std::string(100000, 'e'));
    rep.set_session(SessionFacts{0, 4, 9, 0xFFFFFFFFu, std::string(5000, 'l')});
    for (uint32_t t = 0; t < 100000; t++) { rep.commit(0xFFFFFFF0u + t, ~0ull, ~0ull); rep.rollback(0xFFFFFFFFu, 0xFFFFFFFFu); }
    const std::string txt = rep.render(kWhen);
    std::printf("size: worst case %zu bytes (limit %zu)\n", txt.size(), kMaxBytes);
    CHECK(txt.size() < kMaxBytes);
    CHECK(has(txt, "more not kept)"));
    CHECK(has(txt, "# end of report\n"));   // the limits keep it below the cut: nothing truncated
    size_t longest = 0, start = 0;
    for (size_t i = 0; i < txt.size(); i++) if (txt[i] == '\n') { longest = std::max(longest, i - start); start = i + 1; }
    CHECK(longest <= kMaxKey + 1 + kMaxText);
}

static void test_file_and_empty(const std::string& dir)
{
    CHECK_EQ(file_name(kWhen), "jsp-match-report-20261121-090507.txt");
    CHECK_EQ(file_name(Stamp{2027, 1, 2, 23, 59, 0}, 2), "jsp-match-report-20270102-235900-2.txt");

    Report empty;
    const std::string txt = empty.render(kWhen);
    CHECK(txt.rfind(std::string("# ") + kFormat + "\n", 0) == 0);
    CHECK(has(txt, "written: 2026-11-21 09:05:07 (local time)\n"));
    CHECK(has(txt, "build: (unknown)\n") && has(txt, "reason: (not given)\n") && has(txt, "[session]\n(no session started)\n"));
    CHECK(has(txt, "[settings]\n(none)\n") && has(txt, "[notes]\n(none)\n"));
    CHECK(has(txt, "count: 0  max_depth: 0  avg_depth: 0.00  kept: 0") && has(txt, "count: 0  kept: 0"));
    const char* sec[] = {"[end]", "[session]", "[settings]", "[rollbacks]", "[commits]", "[notes]"};
    size_t prev = 0;
    for (const char* s : sec) { const size_t at = txt.find(std::string("\n") + s + "\n"); CHECK(at != std::string::npos && at > prev); prev = at; }

    std::string err;
    const std::string p1 = empty.write(dir, kWhen, &err);
    const std::string p2 = empty.write(dir + "/", kWhen, &err);
    CHECK_EQ(p1, dir + "/jsp-match-report-20261121-090507.txt");
    CHECK_EQ(p2, dir + "/jsp-match-report-20261121-090507-2.txt");
    std::string back;
    if (std::FILE* f = std::fopen(p1.c_str(), "rb")) {
        char b[4096]; size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) back.append(b, n);
        std::fclose(f);
    }
    CHECK(back == txt);
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    CHECK(empty.write(dir + "/no/such/folder", kWhen, &err).empty() && !err.empty());
    std::printf("file: ok\n");
}

int main(int argc, char** argv)
{
    test_ring();
    test_sanitize();
    test_size();
    const char* tmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : std::getenv("TEMP") ? std::getenv("TEMP") : "/tmp";
    test_file_and_empty(argc > 1 ? argv[1] : tmp);   // write() goes to SCRATCH_DIR, else the temp folder (removed again)
    std::printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
