// Host test for installer/port/jsp/jsp_online.cpp: feeds ini files in a temp tree, checks the resulting environment.
// Build/run: installer/tools/online_settings_test.sh. Test hosts are example.invalid and 192.0.2.x only.
#define JSP_ONLINE_TEST 1
#include "../port/jsp/jsp_online.cpp"
#include <sys/stat.h>
#include <unistd.h>
using namespace jsp_online;

static int fails = 0, total = 0;
static std::map<std::string, std::string> E;
static std::vector<std::string> L;
static std::string root;

static void wr(const std::string& rel, const std::string& body)
{
    std::string p = root + "/" + rel;
    for (size_t i = 1; i < p.size(); i++) if (p[i] == '/') mkdir(p.substr(0, i).c_str(), 0700);
    FILE* f = fopen(p.c_str(), "wb"); fwrite(body.data(), 1, body.size(), f); fclose(f);
}
static void reset() { E.clear(); L.clear(); system(("rm -rf " + root + "/*").c_str()); }
static Env mk()
{
    Env e;
    e.get = [](const char* n) -> const char* { auto i = E.find(n); return i == E.end() ? nullptr : i->second.c_str(); };
    e.set = [](const char* k, const char* v) { E[k] = v; };
    e.log = [](const std::string& s) { L.push_back(s); };
    return e;
}
static std::string g(const char* k) { auto i = E.find(k); return i == E.end() ? "<unset>" : i->second; }
static bool logged(const char* sub) { for (auto& s : L) if (s.find(sub) != std::string::npos) return true; return false; }
static void check(const char* what, bool ok) { total++; if (!ok) { fails++; printf("FAIL %s\n", what); for (auto& s : L) printf("   log: %s\n", s.c_str()); } else printf("ok   %s\n", what); }
static int run(const char* psnr = "", const char* user = "") { Env e = mk(); return apply(e, root + "/shared", root + "/game", psnr, user); }

int main()
{
    char t[] = "/tmp/jsp_online_XXXXXX"; root = mkdtemp(t);
    const char* SHR = "[repsn]\nenabled=1\nserver=example.invalid:36200\nname=Tester_1\n";

    reset(); wr("shared/repsn.ini", SHR);
    check("shared only: online", run() == 1 && g("PS3_NET_ONLINE") == "1" && g("PSNR_SERVER") == "example.invalid:36200" && g("PS3_NP_ONLINE_ID") == "Tester_1");

    reset(); wr("shared/repsn.ini", SHR); wr("game/repsn.ini", "[repsn]\ncomm_id=NPWR12345\np2p_port=4000\nreport_rtt=1\n");
    run();
    check("shared plus game", g("PSNR_SERVER") == "example.invalid:36200" && g("PS3_NET_P2P_PORT") == "4000" && g("PSNR_COMM_ID") == "NPWR12345" && g("PSNR_REPORT_RTT") == "1");

    reset(); wr("shared/repsn.ini", SHR); wr("game/repsn.ini", "[repsn]\nserver=192.0.2.7\nname=GameName\n");
    run();
    check("game overrides shared", g("PSNR_SERVER") == "192.0.2.7:36100" && g("PS3_NP_ONLINE_ID") == "GameName");

    reset(); wr("shared/repsn.ini", SHR); wr("shared/community.ini", "[community]\nenabled=1\nactive=a\n[preset a]\naddress=example.invalid\nname=Comm_1\n");
    check("both enabled: refused", run() == 0 && g("PS3_NET_ONLINE") == "<unset>" && g("PSNR_SERVER") == "<unset>" && logged("both enabled"));

    reset(); wr("shared/community.ini", "[community]\nenabled=1\nactive=a\n[preset a]\naddress=192.0.2.9:1234\nname=Comm_1\n");
    check("community enabled: offline, not supported", run() == 0 && g("PSNR_SERVER") == "<unset>" && logged("not supported yet"));

    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\nname=9bad\n");
    run();
    check("bad name rejected (no id set), server still used", g("PS3_NP_ONLINE_ID") == "<unset>" && g("PSNR_SERVER") == "example.invalid:36100" && logged("bad name"));
    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\nname=ab\n"); run();
    check("name too short rejected", g("PS3_NP_ONLINE_ID") == "<unset>");

    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\n[preset x]\naddress=192.0.2.1\n");
    run();
    check("[preset] in repsn.ini rejected", logged("[preset] in repsn.ini") && g("PSNR_SERVER") == "example.invalid:36100");
    reset(); wr("shared/community.ini", "[community]\nenabled=1\nserver=example.invalid\n"); run();
    check("server key in community.ini rejected", logged("repsn key 'server' in community.ini"));

    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\n; " + std::string(70000, 'x') + "\n");
    check("file over 64 KB rejected whole", run() == 0 && logged("larger than 64 KB"));
    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\nname=" + std::string(600, 'a') + "\n"); run();
    check("line over 512 bytes rejected, rest kept", logged("line over 512") && g("PSNR_SERVER") == "example.invalid:36100" && g("PS3_NP_ONLINE_ID") == "<unset>");

    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=example.invalid\nfoo=1\nbar=2\n[zzz]\na=b\n"); int on = run();
    check("unknown keys counted as rejected", on == 1 && logged("unknown key 'foo'") && logged("unknown section [zzz]") && logged("4 setting(s) rejected"));
    reset(); wr("shared/repsn.ini", "[repsn]\nenabled=1\nserver=ex ample;rm -rf\n"); on = run();
    check("bad server value rejected, stays offline", on == 0 && g("PSNR_SERVER") == "<unset>");

    reset(); wr("shared/repsn.ini", SHR); E["PSNR_SERVER"] = "192.0.2.50"; E["PS3_NET_ONLINE"] = "1"; E["PS3_NP_ONLINE_ID"] = "EnvName"; E["PS3_NET_P2P_PORT"] = "5";
    { Env e = mk(); apply(e, root + "/shared", root + "/game", "", ""); }
    check("env wins over files", g("PSNR_SERVER") == "192.0.2.50" && g("PS3_NP_ONLINE_ID") == "EnvName" && g("PS3_NET_P2P_PORT") == "5" && logged("wins over the settings files"));
    reset(); wr("shared/repsn.ini", SHR);
    run("192.0.2.60:1", "FlagName");
    check("--psnr/--username win over files", g("PSNR_SERVER") == "192.0.2.60:1" && g("PS3_NP_ONLINE_ID") == "FlagName");

    reset(); // no shared, no game folders at all
    check("missing folders: offline, nothing created", run() == 0 && g("PSNR_SERVER") == "<unset>" && access((root + "/shared").c_str(), F_OK) != 0 && logged("no settings files"));

    // folder resolution
    reset(); { Env e = mk(); std::string s, gm;
      resolve_dirs(e, "C:/Games/jsp", "C:/Data/Roaming", s, gm);
      check("installed: %APPDATA%\\PS3Recomp + %APPDATA%\\JSP Fan Revival", s == "C:/Data/Roaming/PS3Recomp" && gm == "C:/Data/Roaming/JSP Fan Revival");
      wr("exe/jsp-install.ini", "[install]\r\nportable=1\r\n");
      resolve_dirs(e, root + "/exe", "X", s, gm);
      check("portable: <exe>/shared + <exe>", s == root + "/exe/shared" && gm == root + "/exe");
      wr("inst/jsp-install.ini", "[install]\r\nportable=1\r\n");
      resolve_dirs(e, root + "/inst/game", "X", s, gm);
      check("portable, exe in game\\: <install>/shared + <install>", s == root + "/inst/shared" && gm == root + "/inst");
      E["PS3RECOMP_SHARED_DIR"] = "/o/v";
      resolve_dirs(e, root + "/exe", "X", s, gm);
      check("PS3RECOMP_SHARED_DIR override", s == "/o/v"); }

    reset(); wr("shared/repsn.ini", "\xEF\xBB\xBF[REPSN]\r\nEnabled=1\r\nSERVER=example.invalid\r\nserver=192.0.2.3:99\r\n");
    run();
    check("case-insensitive keys, CRLF, BOM, last duplicate wins", g("PSNR_SERVER") == "192.0.2.3:99");
    reset(); wr("shared/community.ini", "[community]\nenabled=1\nactive=\n[PRESET lbl-1]\naddress=example.invalid\n[preset " + std::string(33, 'a') + "]\naddress=x\n");
    run();
    check("preset label 1-32, 'preset ' case-insensitive", logged("bad preset label") && !logged("unknown section"));
    system(("rm -rf " + root).c_str());
    printf("%s: %d/%d\n", fails ? "FAIL" : "PASS", total - fails, total);
    return fails != 0;
}
