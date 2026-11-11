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

    // ---- the in-game page's writer (edit_load / edit_check / edit_save) ----
    auto rd = [&](const std::string& rel) { std::string out; FILE* f = fopen((root + "/" + rel).c_str(), "rb"); if (!f) return std::string("<none>"); char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n); fclose(f); return out; };
    auto sh = [&]() { return root + "/shared"; };
    {   // write then read back: the reader (apply) sees exactly what was written
        reset();
        EditState w; w.rep_enabled = 1; w.server = "example.invalid:36200"; w.name = "Tester_1";
        check("edit_save: fresh folder written", edit_save(sh(), w).empty());
        check("fresh repsn.ini: CRLF, preamble, keys", rd("shared/repsn.ini") == "; re-psn server settings. Written by the launcher; the game reads it.\r\n[repsn]\r\nenabled=1\r\nserver=example.invalid:36200\r\nname=Tester_1\r\n");
        check("fresh: no community.ini, no temp file left", rd("shared/community.ini") == "<none>" && system(("ls " + root + "/shared | grep -q tmp").c_str()) != 0);
        EditState r; check("edit_load reads it back equal", edit_load(sh(), r).empty() && r.rep_enabled == 1 && !r.com_enabled && r.server == w.server && r.name == w.name);
        L.clear(); E.clear(); int on = run();
        check("reader accepts the file the page wrote: online, nothing rejected", on == 1 && g("PSNR_SERVER") == "example.invalid:36200" && g("PS3_NP_ONLINE_ID") == "Tester_1" && !logged("rejected (") && !logged("setting(s) rejected"));
    }
    {   // invalid values are refused before anything is written
        reset(); wr("shared/repsn.ini", "[repsn]\nenabled=0\nserver=old.invalid\nname=OldName\n");
        const std::string before = rd("shared/repsn.ini");
        EditState w; w.rep_enabled = 1; w.server = "bad host!"; w.name = "Tester_1";
        check("bad server refused, file untouched", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        w.server = "example.invalid:70000"; check("port 70000 refused", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        w.server = "example.invalid"; w.name = "9bad"; check("name starting with a digit refused", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        w.name = "ab"; check("name of 2 refused", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        w.name = std::string(17, 'a'); check("name of 17 refused", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        w.name = "ok name"; check("space in name refused", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == before);
        EditState c; c.com_enabled = 1; c.presets = {"a"}; c.active = "zzz";
        check("community with no valid active preset refused", !edit_save(sh(), c).empty() && rd("shared/community.ini") == "<none>");
    }
    {   // both methods on: refused, nothing written
        reset(); wr("shared/repsn.ini", "[repsn]\nenabled=0\n");
        const std::string before = rd("shared/repsn.ini");
        EditState w; w.rep_enabled = 1; w.com_enabled = 1; w.server = "example.invalid"; w.name = "Tester_1"; w.presets = {"a"}; w.active = "a";
        std::string why = edit_save(sh(), w);
        check("both methods on refused, nothing written", why.find("both") != std::string::npos && rd("shared/repsn.ini") == before && rd("shared/community.ini") == "<none>");
        reset(); wr("shared/repsn.ini", SHR); wr("shared/community.ini", "[community]\nenabled=1\nactive=a\n[preset a]\naddress=example.invalid\nname=Comm_1\n");
        EditState l; edit_load(sh(), l);
        check("both on disk loads as Off with a note", l.rep_enabled == 0 && l.com_enabled == 0 && !l.note.empty());
        l.rep_enabled = 1;   // the player picks re-psn: saving turns community off on disk, so the files never both say 1
        check("choosing one side writes both files consistently", edit_save(sh(), l).empty() && rd("shared/community.ini").find("enabled=0") != std::string::npos && rd("shared/repsn.ini").find("enabled=1") != std::string::npos);
        L.clear(); E.clear(); check("and the reader then goes online (not refused)", run() == 1 && !logged("both enabled"));
    }
    {   // unknown keys, comments, presets, other sections survive; only the owned keys change; order kept
        reset();
        const std::string orig = "# my notes\r\n[repsn]\r\n; keep me\r\nenabled=0\r\nfuture_key=7\r\nserver=old.invalid\r\n\r\n[other]\r\nx=1\r\n";
        wr("shared/repsn.ini", orig);
        const std::string corig = "; top\n[community]\nenabled=0\nactive=a\nnote=1\n[preset a]\naddress=example.invalid\nname=Comm_1\nextra=2\n[preset b]\naddress=192.0.2.5\nname=Comm_2\n";
        wr("shared/community.ini", corig);
        EditState w; edit_load(sh(), w);
        w.rep_enabled = 1; w.server = "192.0.2.7:1234"; w.name = "New-Name"; w.active = "b";
        check("save over existing files", edit_save(sh(), w).empty());
        check("repsn.ini: comments, unknown key and section kept, owned keys changed in place",
              rd("shared/repsn.ini") == "# my notes\r\n[repsn]\r\n; keep me\r\nenabled=1\r\nfuture_key=7\r\nserver=192.0.2.7:1234\r\nname=New-Name\r\n\r\n[other]\r\nx=1\r\n");
        check("community.ini: only enabled/active changed (presets, extra keys kept)",
              rd("shared/community.ini") == "; top\r\n[community]\r\nenabled=0\r\nactive=b\r\nnote=1\r\n[preset a]\r\naddress=example.invalid\r\nname=Comm_1\r\nextra=2\r\n[preset b]\r\naddress=192.0.2.5\r\nname=Comm_2\r\n");
        EditState r; edit_load(sh(), r);
        check("presets listed, active read back", r.presets.size() == 2 && r.active == "b" && r.server == "192.0.2.7:1234");
    }
    {   // files over the limits are neither read nor overwritten
        reset(); std::string big(70 * 1024, 'x'); wr("shared/repsn.ini", big);
        EditState w; w.rep_enabled = 1; w.server = "example.invalid"; w.name = "Tester_1";
        check("64 KB file: refused, not overwritten", !edit_save(sh(), w).empty() && rd("shared/repsn.ini") == big);
        reset(); wr("shared/repsn.ini", "[repsn]\n" + std::string(600, 'y') + "\n");
        check("512-byte line file: refused", !edit_save(sh(), w).empty());
    }
    {   // the in-game run's seed (run_port_menu_edit.sh) ends exactly as check_port_menu_edit.py expects
        reset(); wr("shared/repsn.ini", "; seeded by run_port_menu_edit.sh\r\n[repsn]\r\nenabled=0\r\nkeep_me=1\r\n"); wr("shared/community.ini", "[community]\r\nenabled=0\r\nactive=\r\n");
        EditState w; edit_load(sh(), w); w.rep_enabled = 1; w.server = "ab.io:99"; w.name = "Pad_Test";
        check("run seed: repsn.ini as the checker expects", edit_save(sh(), w).empty() && rd("shared/repsn.ini") == "; seeded by run_port_menu_edit.sh\r\n[repsn]\r\nenabled=1\r\nkeep_me=1\r\nserver=ab.io:99\r\nname=Pad_Test\r\n" && rd("shared/community.ini").find("enabled=0") != std::string::npos);
    }
    {   // the API as the page calls it
        reset(); g_shared = sh(); char b[300];
        wr("shared/community.ini", "[community]\nenabled=0\nactive=\n[preset a]\naddress=example.invalid\nname=Comm_1\n[preset b]\naddress=192.0.2.5\nname=Comm_2\n");
        check("begin ok", jsp_online_edit_begin(b, sizeof b) == 0);
        jsp_online_edit_cycle(0, 1); jsp_online_edit_row(0, b, sizeof b);
        check("method Left/Right cycles Off -> re-psn", std::string(b) == "Method: re-psn");
        jsp_online_edit_cycle(0, 1); jsp_online_edit_cycle(0, 1); jsp_online_edit_row(0, b, sizeof b);
        check("method wraps back to Off", std::string(b) == "Method: Off");
        jsp_online_edit_cycle(0, -1); jsp_online_edit_row(0, b, sizeof b);
        check("method Left from Off wraps to Community", std::string(b) == "Method: Community server");
        jsp_online_edit_cycle(3, 1); jsp_online_edit_row(3, b, sizeof b);
        check("preset cycles through the saved ones", std::string(b) == "Community preset: a");
        jsp_online_edit_cycle(3, 1); jsp_online_edit_row(3, b, sizeof b);
        check("... then b", std::string(b) == "Community preset: b");
        check("server hint for an invalid value", jsp_online_edit_set(1, "no good", b, sizeof b) == 1 && b[0]);
        check("valid server stored", jsp_online_edit_set(1, "example.invalid:1", b, sizeof b) == 0);
        check("save in the API with community on", jsp_online_edit_save(b, sizeof b) == 1 && std::string(b) == "Saved. Applies on the next start." && rd("shared/community.ini").find("enabled=1") != std::string::npos);
        jsp_online_edit_cycle(0, 1);   // community -> Off
        jsp_online_edit_cycle(0, 1);   // -> re-psn with the stored server but no name
        jsp_online_edit_set(2, "", b, sizeof b);
        check("save refused through the API when server or name is missing; message says why", jsp_online_edit_save(b, sizeof b) == 0 && std::string(b).find("re-psn:") != std::string::npos);
    }
    system(("rm -rf " + root).c_str());
    printf("%s: %d/%d\n", fails ? "FAIL" : "PASS", total - fails, total);
    return fails != 0;
}
