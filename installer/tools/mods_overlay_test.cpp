// Host test of the mods/ framework core (installer/port/jsp/jsp_mods_core.h): mods.txt and manifest parsing, interface check, the CPK @UTF
// TOC rewrite, the virtual-file plan (original bytes, rewritten TOC, appended files at 2048 alignment), file and alias sources, conflicts,
// groups, classification and the mod-set digest. No game data: the test writes its own SYNTHETIC CPK archives (a small @UTF writer, masked
// and unmasked tables, odd file sizes, an unaligned end) and mod files in memory.
// Also the pure parts of the game glue (installer/port/jsp/jsp_mods_glue.h): the start decision, the copy check, the served reads and the
// online rules; and, with -DMODS_GLUE_FILES and the toolkit's include/ (mods_overlay_test.sh TOOLKIT), the glue itself
// (installer/port/jsp/jsp_mods.cpp) on files the test writes in a temporary folder: the cellFs provider as ppu_fs.cpp calls it.
// installer/tools/mods_overlay_test.sh builds and runs it (g++ or clang++, C++20)
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "jsp_mods_glue.h"

using jmods::Bytes;

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t rnd_state = 1;
static uint8_t rnd8() { rnd_state = rnd_state * 6364136223846793005ull + 1442695040888963407ull; return uint8_t(rnd_state >> 56); }
static Bytes noise(size_t n, uint64_t seed) { rnd_state = seed; Bytes b(n); for (auto& x : b) x = rnd8(); return b; }

// ---------------------------------------------------------------------------------------------------- in-memory Io
struct MemIo : jmods::Io {
    std::map<std::string, Bytes> mods, archives;
    std::map<std::string, const jmods::Plan*> virt;   // archives read through a plan (to parse the virtual file back)
    MemIo* under = nullptr;                            // the Io the plans read their sources from
    bool mod_size(const std::string& rel, uint64_t& s) override
    {
        auto it = mods.find(rel);
        if (it == mods.end()) return false;
        s = it->second.size();
        return true;
    }
    size_t mod_read(const std::string& rel, uint64_t off, void* dst, size_t n) override
    {
        auto it = mods.find(rel);
        if (it == mods.end() || off >= it->second.size()) return 0;
        const size_t k = std::min<uint64_t>(n, it->second.size() - off);
        memcpy(dst, it->second.data() + off, k);
        return k;
    }
    bool archive_size(const std::string& base, uint64_t& s) override
    {
        if (auto v = virt.find(base); v != virt.end()) { s = v->second->virt_size; return true; }
        auto it = archives.find(base);
        if (it == archives.end()) return false;
        s = it->second.size();
        return true;
    }
    size_t archive_read(const std::string& base, uint64_t off, void* dst, size_t n) override
    {
        if (auto v = virt.find(base); v != virt.end()) return v->second->read(*under, off, dst, n);
        auto it = archives.find(base);
        if (it == archives.end() || off >= it->second.size()) return 0;
        const size_t k = std::min<uint64_t>(n, it->second.size() - off);
        memcpy(dst, it->second.data() + off, k);
        return k;
    }
};

// ---------------------------------------------------------------------------------------------------- a small @UTF writer
struct Col { uint8_t flags; std::string name; };   // flags = storage << 4 | type
struct Val { uint64_t u = 0; std::string s; };
struct UtfOut { Bytes b; std::vector<std::map<std::string, size_t>> pos; };   // pos[row][column] = byte offset in the unmasked table

static void be(Bytes& b, size_t at, uint64_t v, int n) { for (int i = 0; i < n; ++i) b[at + i] = uint8_t(v >> (8 * (n - 1 - i))); }
static int tsize(int t) { static const int k[12] = { 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8 }; return k[t]; }

static UtfOut write_utf(const std::string& table, const std::vector<Col>& cols, const std::map<std::string, Val>& consts,
                        const std::vector<std::map<std::string, Val>>& rows, bool masked)
{
    std::string pool;
    std::map<std::string, uint32_t> at;
    auto str = [&](const std::string& s) -> uint32_t {
        auto it = at.find(s);
        if (it != at.end()) return it->second;
        const uint32_t o = uint32_t(pool.size());
        pool += s;
        pool += '\0';
        at[s] = o;
        return o;
    };
    str("<NULL>");
    const uint32_t tname = str(table);
    for (const auto& c : cols) str(c.name);
    for (const auto& kv : consts) if (!kv.second.s.empty()) str(kv.second.s);
    for (const auto& r : rows) for (const auto& kv : r) if (!kv.second.s.empty()) str(kv.second.s);
    size_t schema = 0, width = 0;
    for (const auto& c : cols) {
        schema += 5;
        if ((c.flags >> 4) == 3) schema += tsize(c.flags & 15);
        if ((c.flags >> 4) == 5) width += tsize(c.flags & 15);
    }
    const size_t rows_off = 0x18 + schema;
    const size_t strings_off = rows_off + width * rows.size();
    const size_t data_off = (strings_off + pool.size() + 7) / 8 * 8;
    UtfOut o;
    o.b.assign(8 + data_off, 0);
    Bytes& b = o.b;
    memcpy(b.data(), "@UTF", 4);
    be(b, 4, data_off, 4);
    be(b, 8, 1, 2);
    be(b, 10, rows_off, 2);
    be(b, 12, strings_off, 4);
    be(b, 16, data_off, 4);
    be(b, 20, tname, 4);
    be(b, 24, cols.size(), 2);
    be(b, 26, width, 2);
    be(b, 28, rows.size(), 4);
    auto put = [&](size_t p, int type, const Val& v) {
        if (type == 10) be(b, p, str(v.s), 4);
        else be(b, p, v.u, tsize(type));
    };
    size_t p = 0x20;
    for (const auto& c : cols) {
        b[p] = c.flags;
        be(b, p + 1, str(c.name), 4);
        p += 5;
        if ((c.flags >> 4) == 3) { put(p, c.flags & 15, consts.at(c.name)); p += tsize(c.flags & 15); }
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        std::map<std::string, size_t> rp;
        size_t q = 8 + rows_off + r * width;
        for (const auto& c : cols) {
            if ((c.flags >> 4) != 5) continue;
            rp[c.name] = q;
            put(q, c.flags & 15, rows[r].at(c.name));
            q += tsize(c.flags & 15);
        }
        o.pos.push_back(rp);
    }
    memcpy(b.data() + 8 + strings_off, pool.data(), pool.size());
    if (masked) jmods::mask(b.data(), b.size());
    return o;
}

// ---------------------------------------------------------------------------------------------------- a synthetic CPK
struct FileSpec { std::string path; size_t size; };
struct SynthCpk {
    Bytes file;
    uint64_t toc_offset = 0x800, utf_abs = 0, utf_len = 0, etoc_off = 0;
    bool masked = true;
    std::map<std::string, uint64_t> offset;   // entry -> absolute offset of its bytes
    std::map<std::string, size_t> row_of;     // entry -> TOC row
    std::vector<std::map<std::string, size_t>> pos;   // TOC field positions (in the TOC table)
    Bytes toc_plain;                          // the TOC table unmasked
    std::map<std::string, Bytes> data;        // entry -> bytes
};
static void chunk(Bytes& f, size_t at, const char* magic, const Bytes& utf)
{
    if (f.size() < at + 16 + utf.size()) f.resize(at + 16 + utf.size());
    memcpy(&f[at], magic, 4);
    f[at + 4] = 0xFF;
    for (int i = 0; i < 8; ++i) f[at + 8 + i] = uint8_t(uint64_t(utf.size()) >> (8 * i));
    memcpy(&f[at + 16], utf.data(), utf.size());
}
static SynthCpk make_cpk(std::vector<FileSpec> files, bool masked, uint64_t seed, size_t tail_odd)
{
    std::sort(files.begin(), files.end(), [](const FileSpec& a, const FileSpec& b) { return a.path < b.path; });
    SynthCpk c;
    c.masked = masked;
    // Content offsets first (they go into the TOC), with sizes chosen so the TOC and ITOC fit before 0x1800.
    const uint64_t content = 0x1800;
    uint64_t cur = content;
    std::vector<uint64_t> offs;
    for (size_t i = 0; i < files.size(); ++i) {
        offs.push_back(cur);
        c.data[files[i].path] = noise(files[i].size, seed + i);
        cur = jmods::align_up(cur + files[i].size);
    }
    const uint64_t content_end = cur;
    const std::vector<Col> tcols = { { 0x5A, "DirName" }, { 0x5A, "FileName" }, { 0x54, "FileSize" }, { 0x54, "ExtractSize" },
                                     { 0x56, "FileOffset" }, { 0x54, "ID" }, { 0x3A, "UserString" } };
    std::vector<std::map<std::string, Val>> rows;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        const size_t s = f.path.rfind('/');
        std::map<std::string, Val> r;
        r["DirName"].s = f.path.substr(0, s);
        r["FileName"].s = f.path.substr(s + 1);
        r["FileSize"].u = f.size;
        r["ExtractSize"].u = f.size;
        r["FileOffset"].u = offs[i] - c.toc_offset;
        r["ID"].u = 0x100 + i;
        rows.push_back(r);
        c.row_of[f.path] = i;
        c.offset[f.path] = offs[i];
    }
    std::map<std::string, Val> tconst;
    tconst["UserString"].s = "<NULL>";
    UtfOut toc = write_utf("CpkTocInfo", tcols, tconst, rows, masked);
    c.pos = toc.pos;
    c.toc_plain = toc.b;
    if (masked) jmods::mask(c.toc_plain.data(), c.toc_plain.size());
    c.utf_abs = c.toc_offset + 16;
    c.utf_len = toc.b.size();
    // ITOC: ID -> TocIndex, never rewritten.
    std::vector<std::map<std::string, Val>> irows;
    for (size_t i = 0; i < files.size(); ++i) { std::map<std::string, Val> r; r["ID"].u = 0x100 + i; r["TocIndex"].u = i; irows.push_back(r); }
    UtfOut itoc = write_utf("CpkExtendId", { { 0x54, "ID" }, { 0x54, "TocIndex" } }, {}, irows, masked);
    const uint64_t itoc_off = jmods::align_up(c.toc_offset + 16 + toc.b.size());
    if (itoc_off + 16 + itoc.b.size() > content) { std::printf("synthetic TOC too big\n"); std::exit(2); }
    // ETOC at the very end, then an odd tail so the file ends unaligned.
    std::vector<std::map<std::string, Val>> erows(1);
    erows[0]["LocalDir"].s = "synthetic";
    UtfOut etoc = write_utf("CpkEtocInfo", { { 0x5A, "LocalDir" } }, {}, erows, masked);
    const uint64_t etoc_off = content_end;
    c.etoc_off = etoc_off;
    // Header (one row; EnableTocCrc as a constant column).
    const std::vector<Col> hcols = { { 0x56, "ContentOffset" }, { 0x56, "ContentSize" }, { 0x56, "TocOffset" }, { 0x56, "TocSize" },
                                     { 0x56, "ItocOffset" }, { 0x56, "ItocSize" }, { 0x56, "EtocOffset" }, { 0x56, "EtocSize" },
                                     { 0x54, "Files" }, { 0x52, "Align" }, { 0x5A, "Tvers" }, { 0x32, "EnableTocCrc" } };
    std::vector<std::map<std::string, Val>> hrow(1);
    auto& h = hrow[0];
    h["ContentOffset"].u = content;
    h["ContentSize"].u = content_end - content;
    h["TocOffset"].u = c.toc_offset;
    h["TocSize"].u = 16 + toc.b.size();
    h["ItocOffset"].u = itoc_off;
    h["ItocSize"].u = 16 + itoc.b.size();
    h["EtocOffset"].u = etoc_off;
    h["EtocSize"].u = 16 + etoc.b.size() + tail_odd;
    h["Files"].u = files.size();
    h["Align"].u = 2048;
    h["Tvers"].s = "synthetic test archive";
    std::map<std::string, Val> hconst;
    hconst["EnableTocCrc"].u = 0;
    UtfOut hdr = write_utf("CpkHeader", hcols, hconst, hrow, masked);
    Bytes& f = c.file;
    f.assign(etoc_off + 16 + etoc.b.size() + tail_odd, 0);
    chunk(f, 0, "CPK ", hdr.b);
    chunk(f, c.toc_offset, "TOC ", toc.b);
    chunk(f, itoc_off, "ITOC", itoc.b);
    for (const auto& fs : files) memcpy(&f[c.offset[fs.path]], c.data[fs.path].data(), fs.size);
    chunk(f, etoc_off, "ETOC", etoc.b);
    for (size_t i = 0; i < tail_odd; ++i) f[f.size() - tail_odd + i] = uint8_t(0xA0 + i);
    return c;
}

// ---------------------------------------------------------------------------------------------------- mods
static std::string sha_hex(const Bytes& b) { return jmods::hex(jpsm::sha256(b.data(), b.size())); }
static void add_mod(MemIo& io, const std::string& id, const std::string& json) { io.mods[id + "/mod.json"] = Bytes(json.begin(), json.end()); }
static std::string file_ovr(const std::string& entry, const std::string& file, const Bytes& b)
{
    return "{ \"entry\": \"" + entry + "\", \"file\": \"" + file + "\", \"sha256\": \"" + sha_hex(b) + "\" }";
}
static std::string manifest(const std::string& id, const std::string& ovr, const std::string& extra = "", const std::string& version = "1.0.0")
{
    return "{\n  \"schema\": 1, \"id\": \"" + id + "\", \"version\": \"" + version + "\", \"title\": \"t\",\n"
           "  \"game\": { \"title_ids\": [\"BLUS31519\", \"BLES02136\"] },\n  \"interfaces\": { \"cpk-entry\": 1 },\n" + extra +
           "  \"overrides\": [ " + ovr + " ]\n}\n";
}
static bool has_log(const jmods::ModSet& s, const std::string& needle)
{
    for (const auto& l : s.log) if (l.find(needle) != std::string::npos) return true;
    return false;
}
static const jmods::ModState* mod(const jmods::ModSet& s, const std::string& id)
{
    for (const auto& m : s.mods) if (m.id == id) return &m;
    return nullptr;
}
static jmods::Options opts()
{
    jmods::Options o;
    o.title_id = "BLUS31519";
    o.eboot_id = "BLUS31519 1.00";
    return o;
}
static Bytes read_all(jmods::Io& io, const jmods::Plan& p, size_t chunk_size, uint64_t start = 0)
{
    Bytes out;
    Bytes buf(chunk_size);
    for (uint64_t pos = start;;) {
        const size_t n = p.read(io, pos, buf.data(), buf.size());
        out.insert(out.end(), buf.begin(), buf.begin() + n);
        pos += n;
        if (n < buf.size()) break;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------- the tests
static const char* GAME = "partition_op_game_ps3.cpk";
static const char* CHARA = "partition_op_character_ps3.cpk";

static void test_helpers()
{
    std::vector<std::string> log;
    const auto l = jmods::parse_mods_list("\xEF\xBB\xBF" "alpha\r\n# comment\n\n  beta  # trailing\nalpha\nBad Id\ngamma", log);
    CHECK(l.size() == 4 && l[0].id == "alpha" && l[1].id == "beta" && l[2].id == "Bad Id" && !l[2].valid && l[3].id == "gamma");
    CHECK(l[0].line == 1 && l[1].line == 4 && l[3].line == 7);
    bool dup = false, bad = false;
    for (const auto& s : log) { dup |= s.find("duplicate alpha") != std::string::npos; bad |= s.find("'Bad Id' is not a mod id") != std::string::npos; }
    CHECK(dup && bad);
    CHECK(jmods::parse_mods_list("", log).empty() && jmods::parse_mods_list("# only\n\n", log).empty());

    CHECK(jmods::is_cosmetic_path("game/logo/logo_US_v.pak") && jmods::is_cosmetic_path("sound/JP/BGM_JP.acb") && jmods::is_cosmetic_path("movie/a.usm"));
    CHECK(!jmods::is_cosmetic_path("sound/bgm/x.acb") && !jmods::is_cosmetic_path("sound/JP/CV_001_00_ST_JP.acb") && !jmods::is_cosmetic_path("sound/BGM_x.acb") &&
          !jmods::is_cosmetic_path("sound/JP/sub/BGM_x.acb") && !jmods::is_cosmetic_path("sound//BGM_x.acb") && !jmods::is_cosmetic_path("sound/JP/SE_001.acb"));   // songs only (D40)
    CHECK(!jmods::is_cosmetic_path("game/resident/game_resident_ALL_m.pak") && !jmods::is_cosmetic_path("character/model/character_model_000_m.pak"));
    CHECK(!jmods::is_cosmetic_path("progress/title_chara/x.pak") && !jmods::is_cosmetic_path("battle/x.pak") && !jmods::is_cosmetic_path("gamelogo/x"));
    CHECK(jmods::archive_of("battle/a/b.pak") == "partition_op_battle_ps3.cpk" && jmods::archive_of("patch/x").empty() && jmods::archive_of("x").empty());
    CHECK(jmods::valid_rel_path("files/a/b.pak") && !jmods::valid_rel_path("../x") && !jmods::valid_rel_path("a/../x") && !jmods::valid_rel_path("/x") &&
          !jmods::valid_rel_path("a\\b") && !jmods::valid_rel_path("a//b") && !jmods::valid_rel_path("c:/x"));

    // strict JSON: duplicate keys, trailing commas and comments are errors
    jmods::Json j;
    std::string err;
    CHECK(!jmods::JsonParser("{\"a\":1,\"a\":2}").parse(j, err));
    CHECK(!jmods::JsonParser("{\"a\":1,}").parse(j, err));
    CHECK(!jmods::JsonParser("{\"a\":1} // c").parse(j, err));
    CHECK(jmods::JsonParser("{\"a\":[1,\"x\\u00e9\",true,null]}").parse(j, err) && j.get("a")->arr[1].s == "x\xC3\xA9");

    // Windows-unsafe names: device names in any part, any case, with or without an extension; trailing '.' or ' '; <>"|?*:
    for (const char* bad : { "files/CON", "files/con", "files/Prn.pak", "files/aux", "files/nul.pak", "files/NUL.tar.gz", "files/nul .txt",
                             "COM1/x.pak", "files/com9", "files/COM0.bin", "files/lpt1", "files/LpT9.pak", "files/LPT0", "files/conin$",
                             "files/CONOUT$.x", "files/COM\xC2\xB9", "files/x.", "files/x ", "x./y", "files/a<b", "files/a>b", "files/a\"b",
                             "files/a|b", "files/a?b", "files/a*b", "files/a:b" }) {
        const bool rejected = !jmods::valid_rel_path(bad);
        if (!rejected) std::printf("FAIL valid_rel_path accepted '%s'\n", bad);
        CHECK(rejected);
    }
    for (const char* ok : { "files/CONSOLE.pak", "files/com10", "files/lpt", "files/nul_x.pak", "files/a.b.c", "files/aux1x", "files/.hidden",
                            "game/logo/logo_US_v.pak", "files/CON_x" }) {
        const bool accepted = jmods::valid_rel_path(ok);
        if (!accepted) std::printf("FAIL valid_rel_path refused '%s'\n", ok);
        CHECK(accepted);
    }

    // RFC 8259 numbers: no leading zeros, no malformed forms; integers stay integers.
    for (const char* bad : { "[01]", "[-01]", "[00]", "[1.2.3]", "[1.]", "[.5]", "[1e]", "[1e+]", "[1.2.3e-]", "[12-3]", "[-]", "[+1]", "[1e5e5]",
                             "[0x10]", "[1 2]" }) {
        const bool rejected = !jmods::JsonParser(bad).parse(j, err);
        if (!rejected) std::printf("FAIL JSON accepted %s\n", bad);
        CHECK(rejected);
    }
    CHECK(jmods::JsonParser("[0, -0, 7, -12, 1.5, -0.25e+3, 2E-2, 123456789012345678]").parse(j, err) && j.arr.size() == 8 &&
          j.arr[0].type == jmods::Json::Int && j.arr[2].i == 7 && j.arr[3].i == -12 && j.arr[4].type == jmods::Json::Num &&
          j.arr[5].type == jmods::Json::Num && j.arr[7].type == jmods::Json::Int);
    // one leading UTF-8 BOM is skipped, a second is an error
    CHECK(jmods::JsonParser("\xEF\xBB\xBF{\"a\": 1}").parse(j, err) && j.get("a") && j.get("a")->i == 1);
    CHECK(!jmods::JsonParser("\xEF\xBB\xBF\xEF\xBB\xBF{}").parse(j, err));
}

static void test_plan_and_reads(MemIo& io, const SynthCpk& game)
{
    // One cosmetic file override that grows the entry, one that shrinks another, with odd sizes.
    const Bytes logo_new = noise(3900, 77), small_new = noise(13, 78);
    io.mods["logo/files/logo_a.pak"] = logo_new;
    io.mods["logo/files/logo_b.pak"] = small_new;
    add_mod(io, "logo", manifest("logo", file_ovr("game/logo/logo_a.pak", "files/logo_a.pak", logo_new) + ", " +
                                             file_ovr("game/logo/logo_b.pak", "files/logo_b.pak", small_new)));
    const jmods::ModSet s = jmods::load_mod_set(io, "logo\n", opts());
    CHECK(mod(s, "logo") && mod(s, "logo")->loaded && mod(s, "logo")->applied && !mod(s, "logo")->gameplay);
    CHECK(s.digest_kind == jmods::DigestKind::None);   // cosmetic only: no digest
    CHECK(has_log(s, "[mods] logo 1.0.0: cosmetic, 2 overrides (file), interfaces cpk-entry 1"));
    CHECK(has_log(s, "cosmetic only, no gameplay digest"));
    CHECK(s.plans.size() == 1 && s.plans.count(GAME));
    if (!s.plans.count(GAME)) return;
    const jmods::Plan& p = s.plans.at(GAME);
    const uint64_t orig = game.file.size();
    CHECK(orig % 2048 != 0);   // the synthetic file ends unaligned
    // In place: logo_a grows inside its 4096 B slot (3001 B stored), logo_b shrinks; the archive keeps its size and every offset.
    CHECK(p.orig_size == orig && p.virt_size == orig);
    CHECK(p.overrides.size() == 2 && p.overrides[0].entry == "game/logo/logo_a.pak" && p.overrides[0].virt == game.offset.at("game/logo/logo_a.pak"));
    CHECK(p.overrides[1].entry == "game/logo/logo_b.pak" && p.overrides[1].virt == game.offset.at("game/logo/logo_b.pak"));
    const uint64_t slot_a = jmods::align_up(game.data.at("game/logo/logo_a.pak").size()), slot_b = jmods::align_up(game.data.at("game/logo/logo_b.pak").size());

    // The reference image: the original, the three TOC fields of both rows rewritten, zero padding, the two files.
    Bytes ref = game.file;
    memset(&ref[p.overrides[0].virt], 0, slot_a);
    memset(&ref[p.overrides[1].virt], 0, slot_b);
    Bytes toc = game.toc_plain;
    std::set<size_t> changed;   // positions in the TOC table the rewrite may touch
    for (const auto& o : p.overrides) {
        const auto& rp = game.pos[game.row_of.at(o.entry)];
        jmods::put_be32(&toc[rp.at("FileSize")], uint32_t(o.len));
        jmods::put_be32(&toc[rp.at("ExtractSize")], uint32_t(o.len));
        for (const char* f : { "FileSize", "ExtractSize" })
            for (int k = 0; k < 4; ++k) changed.insert(rp.at(f) + k);
    }
    if (game.masked) jmods::mask(toc.data(), toc.size());
    memcpy(&ref[game.utf_abs], toc.data(), toc.size());
    memcpy(&ref[p.overrides[0].virt], logo_new.data(), logo_new.size());
    memcpy(&ref[p.overrides[1].virt], small_new.data(), small_new.size());

    // Reads in many chunk sizes (odd ones cross every segment border) equal the reference, and stop at the virtual end.
    bool all_ok = true;
    for (size_t cs : { size_t(1), size_t(3), size_t(7), size_t(509), size_t(2048), size_t(4099), size_t(1 << 20) }) {
        const Bytes v = read_all(io, p, cs);
        if (v != ref) { all_ok = false; std::printf("FAIL chunk %zu: %zu bytes read, want %zu\n", cs, v.size(), ref.size()); }
    }
    CHECK(all_ok);
    // Every original byte outside the rewritten fields is unchanged (unmasked comparison of the TOC, raw elsewhere).
    {
        const Bytes v = read_all(io, p, 65536);
        bool same = v.size() == p.virt_size;
        Bytes vt(v.begin() + game.utf_abs, v.begin() + game.utf_abs + game.utf_len);
        if (game.masked) jmods::mask(vt.data(), vt.size());
        size_t diffs_outside = 0, diffs_inside = 0;
        for (size_t i = 0; i < vt.size(); ++i)
            if (vt[i] != game.toc_plain[i]) (changed.count(i) ? diffs_inside : diffs_outside)++;
        for (uint64_t i = 0; same && i < orig; ++i)
            if ((i < game.utf_abs || i >= game.utf_abs + game.utf_len) && (i < p.overrides[0].virt || i >= p.overrides[0].virt + slot_a) &&
                (i < p.overrides[1].virt || i >= p.overrides[1].virt + slot_b) && v[i] != game.file[i]) same = false;
        CHECK(same && diffs_outside == 0 && diffs_inside > 0);
    }
    // Reads around borders: crossing the original end into the padding, crossing the virtual end, at and past the end.
    {
        uint8_t b[64];
        CHECK(p.read(io, orig - 5, b, 10) == 5 && memcmp(b, &game.file[orig - 5], 5) == 0);   // the virtual end is the original end
        CHECK(p.read(io, p.virt_size - 4, b, 64) == 4);
        CHECK(p.read(io, p.virt_size, b, 64) == 0 && p.read(io, p.virt_size + 100, b, 1) == 0);
        CHECK(p.read(io, game.utf_abs - 3, b, 6) == 6 && memcmp(b, &ref[game.utf_abs - 3], 6) == 0);
        const uint64_t te = game.utf_abs + game.utf_len;
        CHECK(p.read(io, te - 3, b, 6) == 6 && memcmp(b, &ref[te - 3], 6) == 0);
    }
    // The virtual file parses back: rewritten rows give the new size and offset, other rows and every name are unchanged,
    // and the bytes at TocOffset + FileOffset are the override's.
    {
        MemIo vio;
        vio.under = &io;
        vio.virt[GAME] = &p;
        const jmods::Archive orig_a = jmods::load_archive(io, GAME);
        const jmods::Archive va = jmods::load_archive(vio, GAME);
        CHECK(orig_a.ok && va.ok && va.rows.size() == orig_a.rows.size() && va.toc.masked == game.masked);
        bool rows_ok = va.rows.size() == orig_a.rows.size();
        for (size_t i = 0; rows_ok && i < va.rows.size(); ++i) {
            const auto& a = orig_a.rows[i];
            const auto& b = va.rows[i];
            if (a.path != b.path) rows_ok = false;
            const jmods::PlannedOverride* o = nullptr;
            for (const auto& x : p.overrides) if (x.entry == a.path) o = &x;
            if (o) {
                if (b.size != o->len || b.extract != o->len || b.offset != o->virt - game.toc_offset) rows_ok = false;
            } else if (b.size != a.size || b.extract != a.extract || b.offset != a.offset) rows_ok = false;
        }
        CHECK(rows_ok);
        const auto& la = va.rows[va.index.at("game/logo/logo_a.pak")];
        Bytes got(la.size);
        CHECK(vio.archive_read(GAME, va.toc_offset + la.offset, got.data(), got.size()) == got.size() && got == logo_new);
        const auto& lb = va.rows[va.index.at("game/logo/logo_b.pak")];
        got.assign(lb.size, 0);
        CHECK(vio.archive_read(GAME, va.toc_offset + lb.offset, got.data(), got.size()) == got.size() && got == small_new);
        // an untouched row still reads its original bytes
        const auto& lr = va.rows[va.index.at("game/resident/res.pak")];
        got.assign(lr.size, 0);
        CHECK(vio.archive_read(GAME, va.toc_offset + lr.offset, got.data(), got.size()) == got.size() && got == game.data.at("game/resident/res.pak"));
        CHECK(va.prefix != orig_a.prefix && p.prefix == orig_a.prefix);
    }
}

static void test_alias_and_classes(MemIo& io, const SynthCpk& chara)
{
    // Model alias: 000's three entries served from 001's, as one group. The archive is unmasked (both kinds are covered).
    std::string ovr;
    for (const char* k : { "m", "i", "v" }) {
        if (!ovr.empty()) ovr += ", ";
        ovr += std::string("{ \"entry\": \"character/model/character_model_000_") + k + ".pak\", \"alias\": \"character/model/character_model_001_" + k + ".pak\" }";
    }
    const std::string group = "  \"gameplay\": false,\n  \"groups\": [ [ \"character/model/character_model_000_m.pak\", \"character/model/character_model_000_i.pak\", "
                              "\"character/model/character_model_000_v.pak\" ] ],\n";
    add_mod(io, "swap", manifest("swap", ovr, group));
    const jmods::ModSet s = jmods::load_mod_set(io, "swap\n", opts());
    const jmods::ModState* m = mod(s, "swap");
    CHECK(m && m->loaded && m->applied && m->gameplay);   // character/model is not on the allow-list: gameplay
    CHECK(has_log(s, "[mods] swap 1.0.0: gameplay, 3 overrides (alias)"));
    CHECK(has_log(s, "<- alias character/model/character_model_001_m.pak"));
    CHECK(s.digest_kind == jmods::DigestKind::Value && s.plans.count(CHARA));
    if (!s.plans.count(CHARA)) return;
    const jmods::Plan& p = s.plans.at(CHARA);
    for (const auto& o : p.overrides) {
        const std::string donor = "character/model/character_model_001_" + o.entry.substr(o.entry.size() - 5);
        Bytes got(o.len);
        CHECK(o.alias && o.source == donor && p.read(io, o.virt, got.data(), got.size()) == got.size() && got == chara.data.at(donor));
    }
    // the digest input of an alias is the donor's hash
    for (const auto& r : m->ovr) CHECK(jmods::hex(r.content) == sha_hex(chara.data.at(r.spec.alias)));

    // A cosmetic-path mod that sets "gameplay": true is gameplay (raise only).
    const Bytes lb = noise(100, 5);
    io.mods["raise/files/b.pak"] = lb;
    add_mod(io, "raise", manifest("raise", file_ovr("game/logo/logo_b.pak", "files/b.pak", lb), "  \"gameplay\": true,\n"));
    const jmods::ModSet r = jmods::load_mod_set(io, "raise\n", opts());
    CHECK(mod(r, "raise")->gameplay && r.digest_kind == jmods::DigestKind::Value);
    // An unlisted path (game/resident) is gameplay by default.
    const Bytes rb = noise(300, 6);
    io.mods["res/files/r.pak"] = rb;
    add_mod(io, "res", manifest("res", file_ovr("game/resident/res.pak", "files/r.pak", rb)));
    const jmods::ModSet rr = jmods::load_mod_set(io, "res\n", opts());
    CHECK(mod(rr, "res")->loaded && mod(rr, "res")->gameplay && rr.digest_kind == jmods::DigestKind::Value);
}

static void test_refusals(MemIo& io)
{
    const Bytes good = noise(700, 9), other = noise(700, 10);
    io.mods["good/files/a.pak"] = good;
    add_mod(io, "good", manifest("good", file_ovr("game/logo/logo_a.pak", "files/a.pak", good)));
    io.mods["badsha/files/a.pak"] = good;
    add_mod(io, "badsha", manifest("badsha", file_ovr("game/logo/logo_b.pak", "files/a.pak", other)));
    io.mods["norow/files/a.pak"] = good;
    add_mod(io, "norow", manifest("norow", file_ovr("game/logo/LOGO_A.pak", "files/a.pak", good)));
    add_mod(io, "newif", "{ \"schema\": 1, \"id\": \"newif\", \"version\": \"1\", \"interfaces\": { \"cpk-entry\": 2 }, \"overrides\": [] }");
    add_mod(io, "unkif", "{ \"schema\": 1, \"id\": \"unkif\", \"version\": \"1\", \"interfaces\": { \"cpk-entry\": 1, \"hooks\": 1 } }");
    add_mod(io, "balif", "{ \"schema\": 1, \"id\": \"balif\", \"version\": \"1\", \"interfaces\": { \"balance\": 1 } }");
    add_mod(io, "schema2", "{ \"schema\": 2, \"id\": \"schema2\", \"version\": \"1\", \"interfaces\": {} }");
    add_mod(io, "wrongid", "{ \"schema\": 1, \"id\": \"other\", \"version\": \"1\", \"interfaces\": {} }");
    add_mod(io, "nofile", manifest("nofile", file_ovr("game/logo/logo_b.pak", "files/missing.pak", good)));
    add_mod(io, "region", "{ \"schema\": 1, \"id\": \"region\", \"version\": \"1\", \"game\": { \"title_ids\": [\"BLJS10258\"] }, \"interfaces\": {} }");
    add_mod(io, "badgroup", manifest("badgroup", file_ovr("game/logo/logo_a.pak", "files/a.pak", good), "  \"groups\": [ [ \"game/logo/logo_b.pak\" ] ],\n"));
    io.mods["badgroup/files/a.pak"] = good;
    add_mod(io, "patcharc", manifest("patcharc", file_ovr("patch/x.pak", "files/a.pak", good)));
    add_mod(io, "escape", manifest("escape", file_ovr("game/logo/logo_a.pak", "../good/files/a.pak", good)));
    add_mod(io, "alien", "{ \"schema\": 1, \"id\": \"alien\", \"version\": \"1\", \"interfaces\": { \"cpk-entry\": 1 }, \"overrides\": "
                         "[ { \"entry\": \"game/logo/logo_a.pak\", \"alias\": \"character/model/character_model_001_m.pak\" } ] }");

    const std::string list = "badsha\nnorow\nnewif\nunkif\nbalif\nschema2\nwrongid\nnofile\nnotthere\nregion\nbadgroup\npatcharc\nescape\nalien\ngood\n";
    const jmods::ModSet s = jmods::load_mod_set(io, list, opts());
    CHECK(mod(s, "good")->loaded && mod(s, "good")->applied && s.plans.count(GAME));   // the good mod still loads
    struct { const char* id; const char* why; } want[] = {
        { "badsha", "does not match its sha256" }, { "norow", "is not in partition_op_game_ps3.cpk (did you mean game/logo/logo_a.pak?)" },
        { "newif", "\"cpk-entry\" 2 is newer than this build supports (1)" }, { "unkif", "interface \"hooks\" is not supported" },
        { "balif", "interface \"balance\" is not supported" }, { "schema2", "schema 2 is not supported" },
        { "wrongid", "is not the folder name" }, { "nofile", "files/missing.pak is missing" }, { "notthere", "no mod.json" },
        { "region", "made for another game version" }, { "badgroup", "has no override in this mod" },
        { "patcharc", "names no archive" }, { "escape", "bad \"file\" path" }, { "alien", "is in another archive" },
    };
    for (const auto& w : want) {
        const jmods::ModState* m = mod(s, w.id);
        const bool ok = m && !m->loaded && !m->applied && m->reason.find(w.why) != std::string::npos &&
                        has_log(s, std::string("[mods] ") + w.id + ": refused: ");
        if (!ok) std::printf("FAIL refusal %s: got '%s'\n", w.id, m ? m->reason.c_str() : "(no mod)");
        CHECK(ok);
    }
    // A refused mod is never half-applied: nothing of badsha/norow reaches a plan.
    for (const auto& kv : s.plans) for (const auto& o : kv.second.overrides) CHECK(o.mod_id == "good");
    // Refused mods that are not provably cosmetic make the digest refuse online.
    CHECK(s.digest_kind == jmods::DigestKind::Refused && s.digest == jmods::refused_digest());
    CHECK(has_log(s, "gameplay mods requested but not applied on this build: online refused"));
    // A refused mod whose fully parsed manifest is cosmetic does not: only badsha (game/logo) refused, next to good.
    const jmods::ModSet c = jmods::load_mod_set(io, "badsha\ngood\n", opts());
    CHECK(!mod(c, "badsha")->loaded && !mod(c, "badsha")->gameplay && c.digest_kind == jmods::DigestKind::None);
    // An interface refusal stops parsing early, so the class is unknown: gameplay, online refused.
    const jmods::ModSet u = jmods::load_mod_set(io, "unkif\ngood\n", opts());
    CHECK(mod(u, "unkif")->gameplay && u.digest_kind == jmods::DigestKind::Refused);
    // A mod.json saved with a UTF-8 BOM loads (and, being cosmetic, does not refuse online).
    {
        const std::string m = "\xEF\xBB\xBF" + manifest("bom", file_ovr("game/logo/logo_a.pak", "files/a.pak", good));
        add_mod(io, "bom", m);
        io.mods["bom/files/a.pak"] = good;
        const jmods::ModSet b = jmods::load_mod_set(io, "bom\n", opts());
        CHECK(mod(b, "bom")->loaded && mod(b, "bom")->applied && b.digest_kind == jmods::DigestKind::None);
    }
    // A file path naming a Windows device refuses the mod.
    add_mod(io, "device", manifest("device", file_ovr("game/logo/logo_a.pak", "files/nul.pak", good)));
    {
        const jmods::ModSet d = jmods::load_mod_set(io, "device\n", opts());
        CHECK(!mod(d, "device")->loaded && mod(d, "device")->reason.find("bad \"file\" path") != std::string::npos);
    }
    // Empty list: stock.
    const jmods::ModSet e = jmods::load_mod_set(io, "# nothing\n", opts());
    CHECK(e.mods.empty() && e.plans.empty() && e.digest_kind == jmods::DigestKind::None && has_log(e, "0 enabled"));
}

static void test_conflicts_and_digest(MemIo& io, const SynthCpk& game)
{
    const Bytes a1 = noise(900, 21), b1 = noise(1900, 22), r1 = noise(333, 23), r2 = noise(333, 24);
    io.mods["first/files/x.pak"] = a1;
    io.mods["second/files/x.pak"] = b1;
    add_mod(io, "first", manifest("first", file_ovr("game/resident/res.pak", "files/x.pak", a1)));
    add_mod(io, "second", manifest("second", file_ovr("game/resident/res.pak", "files/x.pak", b1)));
    add_mod(io, "quiet", manifest("quiet", file_ovr("game/resident/res.pak", "files/x.pak", b1), "  \"conflicts_ok\": [\"first\"],\n"));
    io.mods["quiet/files/x.pak"] = b1;

    const jmods::ModSet s = jmods::load_mod_set(io, "first\nsecond\n", opts());
    CHECK(has_log(s, "[mods] conflict game/resident/res.pak: first overridden by second"));
    CHECK(!mod(s, "first")->applied && mod(s, "second")->applied);
    if (s.plans.count(GAME)) {
        const auto& p = s.plans.at(GAME);
        Bytes got(b1.size());
        CHECK(p.overrides.size() == 1 && p.overrides[0].mod_id == "second" && p.read(io, p.overrides[0].virt, got.data(), got.size()) == got.size() && got == b1);
    } else CHECK(false);
    const jmods::ModSet q = jmods::load_mod_set(io, "first\nquiet\n", opts());
    CHECK(!has_log(q, "[mods] conflict") && mod(q, "quiet")->applied);

    // Digest: the same set twice -> the same; another order -> different; another byte -> different; another version -> different.
    const jmods::ModSet s2 = jmods::load_mod_set(io, "first\nsecond\n", opts());
    CHECK(s.digest_kind == jmods::DigestKind::Value && s2.digest == s.digest);
    const jmods::ModSet rev = jmods::load_mod_set(io, "second\nfirst\n", opts());
    CHECK(rev.digest_kind == jmods::DigestKind::Value && rev.digest != s.digest);
    io.mods["third/files/r.pak"] = r1;
    add_mod(io, "third", manifest("third", file_ovr("game/resident/res.pak", "files/r.pak", r1)));
    const jmods::ModSet t1 = jmods::load_mod_set(io, "third\n", opts());
    io.mods["third/files/r.pak"] = r2;
    add_mod(io, "third", manifest("third", file_ovr("game/resident/res.pak", "files/r.pak", r2)));
    const jmods::ModSet t2 = jmods::load_mod_set(io, "third\n", opts());
    add_mod(io, "third", manifest("third", file_ovr("game/resident/res.pak", "files/r.pak", r2), "", "1.0.1"));
    const jmods::ModSet t3 = jmods::load_mod_set(io, "third\n", opts());
    CHECK(t1.digest_kind == jmods::DigestKind::Value && t2.digest_kind == jmods::DigestKind::Value && t3.digest_kind == jmods::DigestKind::Value);
    CHECK(t1.digest != t2.digest && t2.digest != t3.digest);
    jmods::Options eu = opts();
    eu.eboot_id = "BLES02136 1.00";
    CHECK(jmods::load_mod_set(io, "third\n", eu).digest != t3.digest);   // the EBOOT id is an input
    // The digest is SHA-256 of the documented text.
    {
        const std::string text = "jsp-modset 1\neboot BLUS31519 1.00\nthird 1.0.1\ngame/resident/res.pak " + sha_hex(r2) + "\n";
        CHECK(t3.digest == jpsm::sha256((const uint8_t*)text.data(), text.size()));
    }
    // Cosmetic alongside gameplay: only the gameplay mod is hashed.
    const Bytes lg = noise(64, 30);
    io.mods["cos/files/l.pak"] = lg;
    add_mod(io, "cos", manifest("cos", file_ovr("game/logo/logo_a.pak", "files/l.pak", lg)));
    CHECK(jmods::load_mod_set(io, "cos\nthird\n", opts()).digest == t3.digest);

    // Groups: a later mod that takes one entry of a group drops the whole group back to stock.
    const Bytes g1 = noise(50, 40), g2 = noise(60, 41), g3 = noise(70, 42);
    io.mods["grp/files/a"] = g1;
    io.mods["grp/files/b"] = g2;
    io.mods["late/files/b"] = g3;
    add_mod(io, "grp", manifest("grp", file_ovr("game/logo/logo_a.pak", "files/a", g1) + ", " + file_ovr("game/logo/logo_b.pak", "files/b", g2),
                                "  \"groups\": [ [\"game/logo/logo_a.pak\", \"game/logo/logo_b.pak\"] ],\n"));
    add_mod(io, "late", manifest("late", file_ovr("game/logo/logo_b.pak", "files/b", g3)));
    const jmods::ModSet g = jmods::load_mod_set(io, "grp\nlate\n", opts());
    CHECK(has_log(g, "[mods] grp: group of 2 dropped"));
    CHECK(!mod(g, "grp")->applied && mod(g, "late")->applied);
    if (g.plans.count(GAME)) {
        const auto& p = g.plans.at(GAME);
        CHECK(p.overrides.size() == 1 && p.overrides[0].entry == "game/logo/logo_b.pak" && p.overrides[0].mod_id == "late");
        // logo_a is stock again: its row and bytes are the original's
        MemIo vio;
        vio.under = &io;
        vio.virt[GAME] = &p;
        const jmods::Archive va = jmods::load_archive(vio, GAME);
        const auto& ra = va.rows[va.index.at("game/logo/logo_a.pak")];
        CHECK(va.toc_offset + ra.offset == game.offset.at("game/logo/logo_a.pak") && ra.size == game.data.at("game/logo/logo_a.pak").size());
    } else CHECK(false);
}

static void test_glue(MemIo& io, const SynthCpk& chara)
{
    using namespace jmods;
    // ---- the start decision
    CHECK(!choose_env("0", "D", "L").on && choose_env("0", "D", "L").why.find("JSP_MODS=0") != std::string::npos);
    CHECK(!choose_env(nullptr, "D", nullptr).on && choose_env(nullptr, "D", nullptr).why.find("JSP_MODS_LIST") != std::string::npos);
    CHECK(!choose_env(nullptr, "D", "").on);
    { const EnvChoice c = choose_env(nullptr, "D", "L"); CHECK(c.on && c.dir == "D" && c.list == "L"); }
    { const EnvChoice c = choose_env("1", nullptr, "L"); CHECK(c.on && c.dir.empty()); }   // no folder: every listed mod is refused (not stock)
    CHECK(choose_env("00", "D", "L").on && choose_env("", "D", "L").on);   // only the exact "0" is the kill switch
    // ---- which archive a path names
    CHECK(base_of("/dev_hdd0/game/BLUS31519-INSTALL/USRDIR/data/PS3/partition_op_character_ps3.cpk") == CHARA);
    CHECK(base_of("C:\\Games\\disc/PS3_GAME\\USRDIR\\data\\PS3\\partition_op_game_ps3.cpk") == GAME);
    CHECK(base_of("plain.cpk") == "plain.cpk" && base_of("dir/") == "" && base_of(nullptr) == "");
    // the keys: case folded only when asked (Windows), '\\' and repeated '/' made one '/'
    CHECK(archive_key("X:\\disc\\PS3_GAME\\USRDIR\\data\\PS3\\Partition_OP_Character_PS3.CPK", true) == CHARA);
    CHECK(archive_key("/d/Partition_OP_Character_PS3.CPK", false) == "Partition_OP_Character_PS3.CPK");
    CHECK(archive_key((std::string("/d/") + CHARA).c_str(), true) == CHARA && archive_key(nullptr, true) == "");
    CHECK(path_key("C:\\Games\\hdd0//game\\BLUS31519-INSTALL/USRDIR", false) == "C:/Games/hdd0/game/BLUS31519-INSTALL/USRDIR");
    CHECK(path_key("C:\\Games\\HDD0//game/X.CPK", true) == path_key("c:/games/hdd0/game\\x.cpk", true));
    CHECK(path_key("/a/B.cpk", false) != path_key("/a/b.cpk", false) && path_key(nullptr, true) == "");
    {
        const std::vector<std::string> cp = copy_paths("D/PS3_GAME/USRDIR/data/PS3/", "H", { "BLUS31519-INSTALL", "NPUB31647" }, CHARA);
        CHECK(cp.size() == 3 && cp[0] == std::string("D/PS3_GAME/USRDIR/data/PS3/") + CHARA
              && cp[1] == std::string("H/game/BLUS31519-INSTALL/USRDIR/data/PS3/") + CHARA && cp[2] == std::string("H/game/NPUB31647/USRDIR/data/PS3/") + CHARA);
        CHECK(copy_paths("D/", "H", {}, GAME).size() == 1);
    }
    CHECK(after_copy_check(DigestKind::Value, true) == DigestKind::Refused && after_copy_check(DigestKind::Value, false) == DigestKind::Value);
    CHECK(after_copy_check(DigestKind::None, true) == DigestKind::None && after_copy_check(DigestKind::Refused, false) == DigestKind::Refused);
    CHECK(rollback_gate(false) == nullptr && rollback_gate(true) == kModsNotAsListed
          && std::string(kModsNotAsListed) == "the mod set does not run as listed (see the [mods] lines)");

    // ---- a gameplay set (alias, character) and a cosmetic one (file, game): served reads and the copy check
    const Bytes lb = noise(1500, 77);
    io.mods["glogo/files/l.pak"] = lb;
    add_mod(io, "glogo", manifest("glogo", file_ovr("game/logo/logo_b.pak", "files/l.pak", lb)));
    const ModSet s = load_mod_set(io, "swap\nglogo\n", opts());   // "swap" from test_alias_and_classes
    CHECK(s.plans.count(GAME) && s.plans.count(CHARA) && s.digest_kind == DigestKind::Value);
    if (!s.plans.count(GAME) || !s.plans.count(CHARA)) return;
    CHECK(gameplay_archive(s, CHARA) && !gameplay_archive(s, GAME));   // swap (gameplay) changes character; glogo (cosmetic) changes game
    CHECK(!gameplay_archive(load_mod_set(io, "glogo\n", opts()), GAME));
    for (const char* base : { GAME, CHARA }) {
        const Plan& p = s.plans.at(base);
        const Bytes& copy = io.archives.at(base);
        const CopyCheck cc = copy_check(s, p);
        CHECK(cc.size == copy.size() && cc.prefix_len == p.utf_abs + p.toc.size() && cc.prefix == p.prefix);
        CHECK(cc.donors.size() == (std::string(base) == CHARA ? 3u : 0u));
        auto reader = [](const Bytes& b) {
            return ReadFn([&b](uint64_t off, void* d, size_t n) -> size_t {
                if (off >= b.size()) return 0;
                const size_t k = size_t(std::min<uint64_t>(n, b.size() - off));
                memcpy(d, b.data() + off, k);
                return k;
            });
        };
        CHECK(verify_copy(cc, copy.size(), reader(copy)).empty());
        CHECK(verify_copy(cc, copy.size() + 1, reader(copy)).find("size") != std::string::npos);
        Bytes bad = copy;
        bad[p.utf_abs + 40] ^= 1;   // a TOC byte
        CHECK(verify_copy(cc, bad.size(), reader(bad)) == "its header or TOC differs");
        bad = copy;
        bad[5] ^= 1;   // a header byte
        CHECK(verify_copy(cc, bad.size(), reader(bad)) == "its header or TOC differs");
        if (std::string(base) == CHARA) {
            bad = copy;
            bad[chara.offset.at("character/model/character_model_001_i.pak") + 7] ^= 1;   // a donor byte: the digest would no longer describe it
            CHECK(verify_copy(cc, bad.size(), reader(bad)).find("alias character/model/character_model_001_i.pak differ") != std::string::npos);
            bad = copy;
            bad[chara.offset.at("character/model/character_model_000_m.pak") + 7] ^= 1;   // an overridden entry's old bytes: never served, accepted
            CHECK(verify_copy(cc, bad.size(), reader(bad)).empty());
        }
        // ServeIo: original bytes from the fd's own copy, mod bytes from the handle map; equals Plan::read over the planning Io.
        ServeIo sio(reader(copy), [&io](const std::string& rel, uint64_t off, void* d, size_t n) { return io.mod_read(rel, off, d, n); });
        CHECK(read_all(sio, p, 4093) == read_all(io, p, 65536) && read_all(sio, p, 4093).size() == p.virt_size);
        // override_at: the first override a read touches
        const PlannedOverride& o = p.overrides.front();
        CHECK(override_at(p, 0, 16) == -1 && override_at(p, o.virt, 1) == 0 && override_at(p, o.virt - 4, 8) == 0);
        CHECK(override_at(p, o.virt + o.len, 1) == (p.overrides.size() > 1 && p.overrides[1].virt == o.virt + o.len ? 1 : -1));
    }
    // ---- online rules
    uint32_t out = 0;
    CHECK(net_build(0x12345678u, DigestKind::None, Digest{}, out) && out == 0x12345678u);           // no digest: exactly the stock word
    CHECK(net_build(0x12345678u, DigestKind::None, s.digest, out) && out == 0x12345678u);           // the digest bytes are ignored then
    out = 7;
    CHECK(!net_build(0x12345678u, DigestKind::Refused, refused_digest(), out) && out == 7);          // refused: no word at all
    uint32_t a = 0, b = 0, c = 0, d = 0;
    CHECK(net_build(0x12345678u, DigestKind::Value, s.digest, a) && net_build(0x12345678u, DigestKind::Value, s.digest, b) && a == b);
    {   // the formula, recomputed: first 32 bits (big-endian) of SHA-256(stock LE || digest)
        uint8_t in[36] = { 0x78, 0x56, 0x34, 0x12 };
        memcpy(in + 4, s.digest.data(), 32);
        const Digest h = jpsm::sha256(in, sizeof in);
        CHECK(a == (uint32_t(h[0]) << 24 | uint32_t(h[1]) << 16 | uint32_t(h[2]) << 8 | h[3]) && a != 0x12345678u);
    }
    Digest other = s.digest;
    other[31] ^= 1;
    CHECK(net_build(0x12345678u, DigestKind::Value, other, c) && c != a);                            // another set: another word
    CHECK(net_build(0x12345679u, DigestKind::Value, s.digest, d) && d != a);                         // another release: another word
    CHECK(!game_online_allowed(DigestKind::Value) && !game_online_allowed(DigestKind::Refused) && game_online_allowed(DigestKind::None));
    // cosmetic only: no digest, the game's own online stays allowed
    const ModSet cos = load_mod_set(io, "glogo\n", opts());
    CHECK(cos.digest_kind == DigestKind::None && !cos.plans.empty() && game_online_allowed(cos.digest_kind));
}

static std::string loose_mod(const std::string& id, const std::string& loose, const std::string& ifs = "\"loose-file\": 1", const std::string& extra = "")
{
    return "{ \"schema\": 1, \"id\": \"" + id + "\", \"version\": \"1.0.0\", \"title\": \"t\", \"game\": { \"title_ids\": [\"BLUS31519\"] },\n"
           "  \"interfaces\": { " + ifs + " }, " + extra + "\"loose\": [ " + loose + " ] }\n";
}
static std::string lf(const std::string& path, const std::string& file, const Bytes& b, const std::string& more = "")
{
    return "{ \"path\": \"" + path + "\", \"file\": \"" + file + "\", \"sha256\": \"" + sha_hex(b) + "\"" + more + " }";
}
static std::string la(const std::string& path, const std::string& alias) { return "{ \"path\": \"" + path + "\", \"alias\": \"" + alias + "\" }"; }
#ifdef MODS_GLUE_FILES
// ---------------------------------------------------------------------------------------------------- the glue on real files (POSIX branch)
#include "jsp_mods.cpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" { const ps3_fs_vfile_ops* volatile g_ps3_fs_vfile_ops = nullptr; }
extern "C" { const ps3_fs_path_ops* volatile g_ps3_fs_path_ops = nullptr; }
static int g_online_refusals = 0;
extern "C" void jsp_online_refuse_for_mods(void) { ++g_online_refusals; }

static void put_file(const std::string& path, const Bytes& b)
{
    for (size_t i = 1; i < path.size(); ++i) if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (b.size()) std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
}
static void put_text(const std::string& path, const std::string& t) { put_file(path, Bytes(t.begin(), t.end())); }
// Runs fn with stderr captured; returns what it wrote.
template <class F> static std::string capture(F fn)
{
    std::fflush(stderr);
    char tmpl[] = "/tmp/jsp_mods_err.XXXXXX";
    const int fd = mkstemp(tmpl);
    const int saved = dup(2);
    dup2(fd, 2);
    fn();
    std::fflush(stderr);
    dup2(saved, 2);
    ::close(saved);
    std::string out;
    lseek(fd, 0, SEEK_SET);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) out.append(buf, size_t(n));
    ::close(fd);
    unlink(tmpl);
    return out;
}
static bool has(const std::string& log, const std::string& needle) { return log.find(needle) != std::string::npos; }

static void test_glue_files(MemIo& io, const SynthCpk& game, const SynthCpk& chara)
{
    const char* tmp = std::getenv("TMPDIR");
    std::string root = std::string(tmp && *tmp ? tmp : "/tmp") + "/jsp_mods_files.XXXXXX";
    if (!mkdtemp(root.data())) { CHECK(false); return; }
    const std::string disc = root + "/disc", arch = disc + "/PS3_GAME/USRDIR/data/PS3/", mods = root + "/mods";
    const std::string hdd = root + "/hdd0/game/BLUS31519-INSTALL/USRDIR/data/PS3/";
    put_file(arch + GAME, game.file);
    put_file(arch + CHARA, chara.file);
    put_file(hdd + CHARA, chara.file);                     // the install copy: identical
    Bytes badcopy = chara.file;
    badcopy[chara.utf_abs + 33] ^= 0x10;
    put_file(root + "/other/" + CHARA, badcopy);          // a copy whose TOC differs
    put_text(root + "/eboot.elf", "not an elf, only its hash matters");
    // the mods: an alias swap (gameplay, character) and a logo file (cosmetic, game), as in the in-memory tests
    for (const auto& kv : io.mods)
        if (kv.first.rfind("swap/", 0) == 0 || kv.first.rfind("glogo/", 0) == 0) put_file(mods + "/" + kv.first, kv.second);
    // a mod whose file is a symbolic link to a file outside the mods folder, and a mod folder that is itself a link
    const Bytes outside = noise(1500, 77);   // the same bytes as glogo's file: only the link makes it fail
    put_file(root + "/outside/l.pak", outside);
    put_text(mods + "/linked/mod.json", manifest("linked", file_ovr("game/logo/logo_a.pak", "files/l.pak", outside)));
    mkdir((mods + "/linked/files").c_str(), 0700);
    CHECK(symlink((root + "/outside/l.pak").c_str(), (mods + "/linked/files/l.pak").c_str()) == 0);
    put_text(root + "/elsewhere/mod.json", manifest("dirlink", file_ovr("game/logo/logo_a.pak", "files/l.pak", outside)));
    put_file(root + "/elsewhere/files/l.pak", outside);
    CHECK(symlink((root + "/elsewhere").c_str(), (mods + "/dirlink").c_str()) == 0);

    const jmods::ModSet want = jmods::load_mod_set(io, "swap\nglogo\n", opts());   // the plans the glue must serve
    put_text(root + "/mods.txt", "swap\nglogo\n");
    put_text(root + "/links.txt", "linked\ndirlink\n");
    put_text(root + "/ghost.txt", "ghost\n");

    // 1. off: the kill switch, and no list. No provider either way.
    setenv("JSP_MODS_DIR", mods.c_str(), 1);
    setenv("JSP_MODS_LIST", (root + "/mods.txt").c_str(), 1);
    setenv("JSP_MODS", "0", 1);
    std::string log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] off (JSP_MODS=0): stock game") && g_ps3_fs_vfile_ops == nullptr && g_online_refusals == 0);
    unsetenv("JSP_MODS");
    unsetenv("JSP_MODS_LIST");
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] no mods list") && g_ps3_fs_vfile_ops == nullptr && g_online_refusals == 0);
    uint32_t nb = 0;
    CHECK(jsp_mods_net_build(0xB001, &nb) == 1 && nb == 0xB001);   // nothing loaded: the stock word

    // 2. links are not followed: both mods refused. linked's manifest was read (game/logo only: cosmetic), dirlink's could not be, so its
    //    class is unknown: gameplay, and the set refuses online.
    setenv("JSP_MODS_LIST", (root + "/links.txt").c_str(), 1);
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] linked/files/l.pak: it is reached through a link (not followed)"));
    CHECK(has(log, "[mods] linked: refused:") && has(log, "[mods] dirlink: refused:") && has(log, "[mods] dirlink/mod.json: it is reached through a link"));
    CHECK(g_ps3_fs_vfile_ops == nullptr && g_online_refusals == 1 && has(log, "online refused"));
    CHECK(has(log, "[mods] linked: refused: entry game/logo/logo_a.pak: file files/l.pak is missing"));

    // 3. a listed mod that does not exist: refused, the set refuses online and rollback
    setenv("JSP_MODS_LIST", (root + "/ghost.txt").c_str(), 1);
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] ghost: refused: no mod.json") && has(log, "online refused") && g_ps3_fs_vfile_ops == nullptr && g_online_refusals == 2);
    log = capture([&] { nb = 0; CHECK(jsp_mods_net_build(0xB001, &nb) == 0 && nb == 0); });
    CHECK(has(log, "[mods] rollback refused"));

    // 4. on: two archives served, the game's own online refused (swap is gameplay), the build word folded. Every copy found (the disc and
    //    the hdd0 install folder) is checked at start.
    setenv("JSP_MODS_LIST", (root + "/mods.txt").c_str(), 1);
    setenv("PS3_HDD0_ROOT", (root + "/hdd0").c_str(), 1);
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] check partition_op_character_ps3.cpk: 2 of 2 copies the game can open match the plan"));
    CHECK(has(log, "[mods] check partition_op_game_ps3.cpk: 1 of 1 copy the game can open match the plan") && !has(log, "set: refused"));
    CHECK(has(log, "[mods] list " + root + "/mods.txt: 2 enabled (swap, glogo)"));
    CHECK(has(log, "[mods] swap 1.0.0: gameplay, 3 overrides (alias), interfaces cpk-entry 1"));
    CHECK(has(log, "[mods] override partition_op_character_ps3.cpk character/model/character_model_000_m.pak <- alias character/model/character_model_001_m.pak"));
    CHECK(has(log, "[mods] serving 2 archive(s): partition_op_character_ps3.cpk, partition_op_game_ps3.cpk (eboot sha256:"));
    CHECK(g_ps3_fs_vfile_ops == &k_ops && g_online_refusals == 3);
    log = capture([&] { CHECK(jsp_mods_net_build(0xB001, &nb) == 1 && nb != 0xB001); });
    CHECK(has(log, "[mods] set: gameplay digest ") && has(log, "net build 0x") && has(log, "(stock 0x0000b001)"));
    {   // the same word as the pure fold over the same set (the eboot id differs, so only the shape is compared: a value, not the stock word)
        uint32_t pure = 0;
        CHECK(jmods::net_build(0xB001, want.digest_kind, want.digest, pure) && pure != 0xB001);
    }
    const ps3_fs_vfile_ops* ops = g_ps3_fs_vfile_ops;
    // an archive with no plan, and a planned one at a copy that differs: not claimed (the stock file is read)
    CHECK(ops->open("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/partition_op_map_ps3.cpk", (arch + "partition_op_map_ps3.cpk").c_str()) == nullptr);
    CHECK(jsp_mods_copy_mismatch() == 0);
    log = capture([&] { CHECK(ops->open("/dev_x/other.cpk", (root + "/other/" + CHARA).c_str()) == nullptr); });
    CHECK(has(log, "NOT served") && has(log, "its header or TOC differs"));
    // a gameplay archive's copy the start-up check did not see, that differs: loud, the flag is set, rollback is refused from now on
    CHECK(has(log, "[mods] WARNING: partition_op_character_ps3.cpk") && has(log, "online content mismatch") && jsp_mods_copy_mismatch() == 1);
    CHECK(jmods::rollback_gate(jsp_mods_copy_mismatch() != 0) == jmods::kModsNotAsListed);   // the rollback pump stops before the rendezvous / Hello
    log = capture([&] { CHECK(ops->open("/dev_x/other.cpk", (root + "/other/" + CHARA).c_str()) == nullptr); });
    CHECK(log.empty());   // one line per path
    log = capture([&] { nb = 0; CHECK(jsp_mods_net_build(0xB001, &nb) == 0); });
    CHECK(has(log, "[mods] rollback refused"));
    // the disc and the hdd0 copies are claimed; reads equal the planned virtual file; the override line comes once
    for (const std::string& host : { arch + GAME, arch + CHARA, hdd + CHARA }) {
        const std::string base = jmods::base_of(host.c_str());
        const jmods::Plan& p = want.plans.at(base);
        ps3_fs_vfile* vf = nullptr;
        log = capture([&] { vf = ops->open(("/dev_x/" + base).c_str(), host.c_str()); });
        CHECK(vf && has(log, "[mods] serve " + base + " (/dev_x/" + base + "): checked"));
        if (!vf) continue;
        CHECK(ops->size(vf) == p.virt_size);
        FILE* h = std::fopen(host.c_str(), "rb");
        Bytes got;
        Bytes buf(3001);
        std::string rlog = capture([&] {
            for (uint64_t pos = 0; pos < p.virt_size;) {
                const size_t n = size_t(std::min<uint64_t>(buf.size(), p.virt_size - pos));
                const size_t k = ops->read(vf, h, pos, buf.data(), n);
                got.insert(got.end(), buf.begin(), buf.begin() + k);
                if (k != n) break;
                pos += k;
            }
        });
        std::fclose(h);
        CHECK(got == read_all(io, p, 65536));
        const bool first = host != hdd + CHARA;   // the hdd0 copy shares the archive's override lines with the disc copy
        CHECK(has(rlog, "first read in override segment") == first);
        if (base == CHARA && first) CHECK(has(rlog, "[mods] serve partition_op_character_ps3.cpk (/dev_x/partition_op_character_ps3.cpk) character/model/character_model_000_i.pak: first read in override segment (swap)"));
        ops->close(vf);
    }

    // 5. a second hdd0 folder whose copies differ. A differing copy of the cosmetic-only archive (game): read stock, the set still runs as
    //    listed. A differing copy of the gameplay archive (character): the set is Refused at start (no rollback, no game online).
    const std::string patch = root + "/hdd0/game/BLUS31519-PATCH/USRDIR/data/PS3/";
    Bytes badgame = game.file;
    badgame[game.utf_abs + 33] ^= 0x10;
    put_file(patch + GAME, badgame);
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] check partition_op_game_ps3.cpk: the copy " + patch + GAME + " is not the one the mods were planned on (its header or TOC differs): it is read stock\n"));
    CHECK(has(log, "[mods] check partition_op_game_ps3.cpk: 1 of 2 copies") && !has(log, "set: refused") && jsp_mods_copy_mismatch() == 0);
    CHECK(g_online_refusals == 4);   // swap is gameplay: refused as before, not because of the copy
    log = capture([&] { CHECK(jsp_mods_net_build(0xB001, &nb) == 1 && nb != 0xB001); });
    {   // the bad copy is not claimed, without a second check
        log = capture([&] { CHECK(g_ps3_fs_vfile_ops->open("/dev_hdd0/x", (patch + GAME).c_str()) == nullptr); });
        CHECK(has(log, "NOT served") && !has(log, "WARNING"));
    }
    put_file(patch + CHARA, badcopy);
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] check partition_op_character_ps3.cpk: the copy " + patch + CHARA + " is not the one the mods were planned on (its header or TOC differs): it is read stock, and a gameplay mod changes this archive"));
    CHECK(has(log, "[mods] check partition_op_character_ps3.cpk: 2 of 3 copies") && has(log, "[mods] set: refused: a copy of an archive a gameplay mod changes differs"));
    CHECK(g_online_refusals == 5 && g_ps3_fs_vfile_ops == &k_ops);   // local play still serves the copies that match
    log = capture([&] { nb = 0; CHECK(jsp_mods_net_build(0xB001, &nb) == 0 && nb == 0); });
    CHECK(has(log, "[mods] rollback refused"));
    unsetenv("PS3_HDD0_ROOT");

    // 6. the Windows match (case folded): a path that names the archive in another case is claimed, and serves the planned bytes
    g_fold_case = true;
    log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] serving 2 archive(s): partition_op_character_ps3.cpk, partition_op_game_ps3.cpk"));
    {
        const std::string upper = root + "/Case/PARTITION_OP_CHARACTER_PS3.CPK";
        put_file(upper, chara.file);
        ps3_fs_vfile* vf = nullptr;
        log = capture([&] { vf = g_ps3_fs_vfile_ops->open("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/PARTITION_OP_CHARACTER_PS3.CPK", upper.c_str()); });
        CHECK(vf && has(log, "[mods] serve partition_op_character_ps3.cpk (/dev_bdvd/PS3_GAME/USRDIR/data/PS3/PARTITION_OP_CHARACTER_PS3.CPK): checked"));
        if (vf) {
            const jmods::Plan& p = want.plans.at(CHARA);
            CHECK(g_ps3_fs_vfile_ops->size(vf) == p.virt_size);
            FILE* h = std::fopen(upper.c_str(), "rb");
            Bytes got(size_t(p.virt_size));
            size_t k = 0;
            log = capture([&] { k = g_ps3_fs_vfile_ops->read(vf, h, 0, got.data(), got.size()); });
            CHECK(k == got.size() && got == read_all(io, p, 65536) && has(log, "first read in override segment (swap)"));
            std::fclose(h);
            g_ps3_fs_vfile_ops->close(vf);
        }
        // the start-up check's key and cellFs's host path agree whatever the case and slashes
        log = capture([&] { vf = g_ps3_fs_vfile_ops->open("/dev_x", (arch + "PARTITION_op_character_ps3.cpk").c_str()); });
        CHECK(vf == nullptr || has(log, "checked"));   // a POSIX file system has no such file: only the key is tested, below
        if (vf) g_ps3_fs_vfile_ops->close(vf);
        CHECK(g_st->verified.count(jmods::path_key((arch + CHARA).c_str(), true)) && g_st->verified.count(jmods::path_key((disc + "\\PS3_GAME//USRDIR\\data\\PS3\\" + CHARA).c_str(), true)));
    }
    g_fold_case = false;

    // 7. a copy still being written (first-boot install): read stock while it differs, checked again once its size changed, then served
    {
        log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
        const std::string late = root + "/late/" + CHARA;
        put_file(late, Bytes(chara.file.begin(), chara.file.begin() + chara.file.size() / 2));
        log = capture([&] { CHECK(g_ps3_fs_vfile_ops->open("/dev_x", late.c_str()) == nullptr); });
        CHECK(has(log, "NOT served") && has(log, "its size is") && jsp_mods_copy_mismatch() == 1);
        log = capture([&] { CHECK(g_ps3_fs_vfile_ops->open("/dev_x", late.c_str()) == nullptr); });
        CHECK(log.empty());   // same size: the kept result, no second check
        put_file(late, chara.file);
        ps3_fs_vfile* vf = nullptr;
        log = capture([&] { vf = g_ps3_fs_vfile_ops->open("/dev_x", late.c_str()); });
        CHECK(vf && has(log, "[mods] serve partition_op_character_ps3.cpk (/dev_x): checked"));
        if (vf) g_ps3_fs_vfile_ops->close(vf);
        CHECK(jsp_mods_copy_mismatch() == 1);   // the refusal already made stays
    }
    std::string rm = "rm -rf '" + root + "'";
    CHECK(std::system(rm.c_str()) == 0);
}

// ENHANCEMENT (loose-file): jsp_mods.cpp's resolver and views on real files: the mod's file and an alias window, the guest path forms, a game-folder copy.
static void test_glue_loose()
{
    const char* tmp = std::getenv("TMPDIR");
    std::string root = std::string(tmp && *tmp ? tmp : "/tmp") + "/jsp_mods_loose.XXXXXX";
    if (!mkdtemp(root.data())) { CHECK(false); return; }
    const std::string disc = root + "/disc", arch = disc + "/PS3_GAME/USRDIR/data/PS3/", mods = root + "/mods";
    const SynthCpk snd = make_cpk({ { "sound/JP/CV_018_00_ST_JP.acb", 3000 }, { "sound/JP/CV_018_00_ST_JP.awb", 5000 } }, false, 3000, 5);
    const SynthCpk prog = make_cpk({ { "progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak", 2500 } }, true, 3100, 9);
    put_file(arch + "partition_op_sound_ps3.cpk", snd.file);
    put_file(arch + "partition_op_progress_ps3.cpk", prog.file);
    const Bytes cva = noise(1500, 71), cvb = noise(2600, 72);
    put_file(mods + "/vo/f/CV_019_00_ST_JP.acb", cva);
    put_file(mods + "/vo/f/CV_019_00_ST_JP.awb", cvb);
    const std::string TIPS = "progress/game/loading_game/tips/", tip = TIPS + "loading_game_tips_chara_019_000_ALL_m.pak", tipv = TIPS + "loading_game_tips_chara_019_000_ALL_v.pak", donor = TIPS + "loading_game_tips_chara_018_000_ALL_m.pak";
    put_text(mods + "/vo/mod.json", loose_mod("vo", lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva) + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb) + ", " + la(tip, donor) + ", " + la(tipv, donor)));
    put_text(root + "/mods.txt", "vo\n");
    put_text(root + "/eboot.elf", "x");
    setenv("JSP_MODS_DIR", mods.c_str(), 1);
    setenv("JSP_MODS_LIST", (root + "/mods.txt").c_str(), 1);
    unsetenv("JSP_MODS");
    const int refusals0 = g_online_refusals;
    std::string log = capture([&] { jsp_mods_init(disc.c_str(), (root + "/eboot.elf").c_str(), "BLUS31519"); });
    CHECK(has(log, "[mods] serving 4 loose file(s) through the path resolver") && has(log, "[mods] vo 1.0.0: gameplay") && g_ps3_fs_path_ops == &k_path_ops && g_ps3_fs_vfile_ops == &k_ops);
    CHECK(g_online_refusals == refusals0 + 1);   // a gameplay set: the game's own online is off (as for any gameplay mod)
    const ps3_fs_path_ops* po = g_ps3_fs_path_ops;
    const ps3_fs_vfile_ops* vo = g_ps3_fs_vfile_ops;
    const auto resolve = [&](const std::string& guest, std::string& host, uint64_t* vs) { char b[1100]; std::snprintf(b, sizeof b, "%s", host.c_str()); const int r = po->resolve(guest.c_str(), b, sizeof b, vs); host = b; return r; };
    // the mod's own file: three guest path forms, a stat size, the pinned bytes through a view
    for (const std::string& g : { std::string("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.acb"), std::string("/dev_hdd0/game/BLUS31519-INSTALL/USRDIR/data/PS3/sound/jp/cv_019_00_st_jp.acb"),
                                  std::string("sound/JP/CV_019_00_ST_JP.acb") }) {
        std::string host = arch + "sound/JP/CV_019_00_ST_JP.acb";
        uint64_t vs = UINT64_MAX;
        CHECK(resolve(g, host, &vs) == 1 && host == mods + "/vo/f/CV_019_00_ST_JP.acb" && vs == cva.size());
    }
    {
        std::string host = arch + "sound/JP/CV_019_00_ST_JP.awb";
        CHECK(resolve("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.awb", host, nullptr) == 1);
        FILE* f = std::fopen(host.c_str(), "rb");
        ps3_fs_vfile* vf = vo->open("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.awb", host.c_str());
        CHECK(f && vf && vo->size(vf) == cvb.size());
        Bytes got(cvb.size());
        CHECK(vf && vo->read(vf, f, 0, got.data(), got.size()) == cvb.size() && got == cvb);
        Bytes mid(100);
        CHECK(vf && vo->read(vf, f, 1000, mid.data(), mid.size()) == 100 && std::equal(mid.begin(), mid.end(), cvb.begin() + 1000));
        CHECK(vf && vo->read(vf, f, cvb.size() - 10, got.data(), 50) == 10);   // never past the end
        if (vf) vo->close(vf);
        if (f) std::fclose(f);
    }
    // the alias: the disc copy of the archive is the host, the view is the donor row's window
    {
        std::string host = arch + tip;
        uint64_t vs = UINT64_MAX;
        const Bytes& want = prog.data.at(donor);
        CHECK(resolve("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/" + tip, host, &vs) == 1 && host == arch + "partition_op_progress_ps3.cpk" && vs == want.size());
        FILE* f = std::fopen(host.c_str(), "rb");
        ps3_fs_vfile* vf = vo->open(("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/" + tip).c_str(), host.c_str());
        CHECK(f && vf && vo->size(vf) == want.size());
        Bytes got(want.size());
        CHECK(vf && vo->read(vf, f, 0, got.data(), got.size()) == want.size() && got == want);
        if (vf) vo->close(vf);
        if (f) std::fclose(f);
        // opened from some other copy of the archive: not served (the fd reads its own FILE)
        put_file(root + "/other.cpk", prog.file);
        log = capture([&] { CHECK(vo->open(("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/" + tip).c_str(), (root + "/other.cpk").c_str()) == nullptr); });
        CHECK(has(log, "not the planned copy: not served"));
    }
    {   // an alias window that differs from the plan is not served, once per path, and the set stops running as listed
        const std::string g = "/dev_bdvd/PS3_GAME/USRDIR/data/PS3/" + tipv;
        std::string host = arch + tipv;
        CHECK(resolve(g, host, nullptr) == 1);
        Bytes bad = prog.file;
        bad[prog.offset.at(donor) + 5] ^= 0x55;
        put_file(arch + "partition_op_progress_ps3.cpk", bad);
        const int before = g_kind.load();
        log = capture([&] { CHECK(vo->open(g.c_str(), host.c_str()) == nullptr); });
        CHECK(has(log, "the archive window differs from the planned bytes") && g_kind.load() == (int)DigestKind::Refused && jsp_mods_copy_mismatch() == 1);
        CHECK(vo->open(g.c_str(), host.c_str()) == nullptr);   // the result is kept
        put_file(arch + "partition_op_progress_ps3.cpk", prog.file);
        g_kind.store(before); g_copy_mismatch.store(0);
    }
    // paths that are not loose files are left alone: another name, a CPK itself, a non-game path
    for (const char* g : { "/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_018_00_ST_JP.acb", "/dev_bdvd/PS3_GAME/USRDIR/data/PS3/partition_op_sound_ps3.cpk",
                           "/dev_hdd0/game/BLUS31519-INSTALL/USRDIR/x.dat", "", "/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_020_00_ST_JP.acb" }) {
        std::string host = "/keep/me";
        uint64_t vs = UINT64_MAX;
        CHECK(resolve(g, host, &vs) == 0 && host == "/keep/me" && vs == UINT64_MAX);
        CHECK(vo->open(g, "/keep/me") == nullptr);
    }
    // a copy in the game folder: the mod's copy wins, with one warning
    put_file(arch + "sound/JP/PV_019_00_ST_JP.acb", noise(10, 1));   // not a mod path: no warning
    put_file(arch + "sound/JP/CV_019_00_ST_JP.acb", noise(77, 2));   // old installer leftovers
    {
        std::string host = arch + "sound/JP/CV_019_00_ST_JP.acb";
        log = capture([&] { CHECK(resolve("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.acb", host, nullptr) == 1); });
        CHECK(host == mods + "/vo/f/CV_019_00_ST_JP.acb" && has(log, "loose sound/JP/CV_019_00_ST_JP.acb: a game-folder copy exists (not from the mods folder), the mod's copy is served"));
        host = arch + "sound/JP/CV_019_00_ST_JP.acb";
        log = capture([&] { CHECK(resolve("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.acb", host, nullptr) == 1); });
        CHECK(!has(log, "game-folder copy"));   // once
    }
    {   // a host buffer too small for the answer: left alone
        char tiny[8] = "/x";
        const int before = g_kind.load();
        log = capture([&] { CHECK(po->resolve("sound/JP/CV_019_00_ST_JP.acb", tiny, sizeof tiny, nullptr) == 0 && std::strcmp(tiny, "/x") == 0); });
        CHECK(has(log, "the set no longer runs as listed") && g_kind.load() == (int)DigestKind::Refused);   // not just a log line
        g_kind.store(before); g_copy_mismatch.store(0);
    }
    std::string rm = "rm -rf '" + root + "'";
    CHECK(std::system(rm.c_str()) == 0);
    unsetenv("JSP_MODS_DIR");
    unsetenv("JSP_MODS_LIST");
}
#endif

// R6: the in-place layout, edge cases (after the GMOD R6 refutation review). The only in-game proof is the same-size case.
static const jmods::Plan* plan1(MemIo& io, const std::string& id, const std::vector<std::pair<std::string, Bytes>>& ovs, jmods::ModSet& s)
{
    std::string o;
    int k = 0;
    for (auto& [entry, b] : ovs) {
        const std::string f = "files/f" + std::to_string(k++) + ".bin";
        io.mods[id + "/" + f] = b;
        o += (o.empty() ? "" : ", ") + file_ovr(entry, f, b);
    }
    add_mod(io, id, manifest(id, o));
    s = jmods::load_mod_set(io, id + "\n", opts());
    return s.plans.count(GAME) ? &s.plans.at(GAME) : nullptr;
}
static void test_inplace_edges()
{
    MemIo io;
    // logo_b 384 B (slot 2048), res 2048 (slot exactly 2048), last 777 is the last entry before the ETOC.
    const SynthCpk game = make_cpk({ { "game/logo/logo_a.pak", 3001 }, { "game/logo/logo_b.pak", 384 }, { "game/resident/res.pak", 2048 },
                                     { "game/zz/last.pak", 777 } }, true, 1000, 441);
    io.archives[GAME] = game.file;
    const uint64_t orig = game.file.size();
    auto all = [&](const jmods::Plan& p) { return read_all(io, p, 4093); };
    // 1. identical bytes: the virtual file IS the original file, byte for byte (the in-game proof of R6).
    {
        jmods::ModSet s;
        const auto* p = plan1(io, "ident", { { "game/logo/logo_a.pak", game.data.at("game/logo/logo_a.pak") } }, s);
        CHECK(p && p->virt_size == orig && p->overrides[0].virt == game.offset.at("game/logo/logo_a.pak"));
        if (p) CHECK(all(*p) == game.file);
    }
    // 2. exactly the slot size (384 -> 2048) goes in place and the virtual archive parses with the new row size.
    {
        jmods::ModSet s;
        const Bytes b = noise(2048, 5);
        const auto* p = plan1(io, "exact", { { "game/logo/logo_b.pak", b } }, s);
        CHECK(p && p->virt_size == orig);
        if (p) {
            const Bytes v = all(*p);
            const uint64_t at = game.offset.at("game/logo/logo_b.pak");
            CHECK(v.size() == orig && memcmp(&v[at], b.data(), 2048) == 0 && memcmp(&v[at + 2048], &game.file[at + 2048], orig - at - 2048) == 0);
            MemIo vio;
            vio.under = &io;
            vio.virt[GAME] = p;
            vio.archives = io.archives;
            const jmods::Archive va = jmods::load_archive(vio, GAME);
            const auto& row = va.rows[va.index.at("game/logo/logo_b.pak")];
            CHECK(va.ok && row.size == 2048 && row.offset == at - game.toc_offset);
        }
    }
    // 3. one byte over the slot is refused, naming the entry and both sizes; a zero-length replacement is refused too.
    {
        jmods::ModSet s;
        const auto* p = plan1(io, "slot1", { { "game/logo/logo_b.pak", noise(2049, 6) } }, s);
        CHECK(!p && mod(s, "slot1") && !mod(s, "slot1")->loaded);
        CHECK(has_log(s, "entry game/logo/logo_b.pak: the replacement is 2049 B, but its slot in partition_op_game_ps3.cpk is 2048 B"));
        CHECK(has_log(s, "a zero-size row has no slot"));
        jmods::ModSet s2;
        const auto* p2 = plan1(io, "zero", { { "game/logo/logo_b.pak", Bytes() } }, s2);
        CHECK(!p2 && mod(s2, "zero") && !mod(s2, "zero")->loaded && has_log(s2, "the replacement is empty"));
    }
    // 4. the last entry before the ETOC (777 -> 2048): in place, the ETOC and the odd tail untouched.
    {
        jmods::ModSet s;
        const Bytes b = noise(2048, 7);
        const auto* p = plan1(io, "last", { { "game/zz/last.pak", b } }, s);
        CHECK(p && p->virt_size == orig);
        if (p) {
            const Bytes v = all(*p);
            CHECK(v.size() == orig && memcmp(&v[game.etoc_off], &game.file[game.etoc_off], orig - game.etoc_off) == 0);
            CHECK(memcmp(&v[game.offset.at("game/zz/last.pak")], b.data(), 2048) == 0);
        }
    }
    // 5. three in-place overrides in one archive: slot tails read as zeros, nothing else changes outside the TOC.
    {
        jmods::ModSet s;
        const Bytes a = noise(10, 8), r = noise(2048, 9), l = noise(1, 10);
        const auto* p = plan1(io, "multi", { { "game/logo/logo_a.pak", a }, { "game/resident/res.pak", r }, { "game/zz/last.pak", l } }, s);
        CHECK(p && p->virt_size == orig && p->overrides.size() == 3);
        if (p) {
            const Bytes v = all(*p);
            Bytes ref = game.file;
            auto put = [&](const char* e, const Bytes& b) {
                const uint64_t at = game.offset.at(e), slot = jmods::align_up(game.data.at(e).size());
                memset(&ref[at], 0, slot);
                memcpy(&ref[at], b.data(), b.size());
            };
            put("game/logo/logo_a.pak", a);
            put("game/resident/res.pak", r);
            put("game/zz/last.pak", l);
            bool same = v.size() == ref.size();
            for (uint64_t i = 0; same && i < ref.size(); ++i)
                if ((i < game.utf_abs || i >= game.utf_abs + game.utf_len) && v[i] != ref[i]) same = false;
            CHECK(same);
        }
    }
    // 6. a zero-size row sharing its offset with the next entry: the next entry still goes in place, the zero row itself is refused.
    {
        MemIo io2;
        const SynthCpk z = make_cpk({ { "game/a/empty.pak", 0 }, { "game/a/full.pak", 3000 }, { "game/b/x.pak", 100 } }, false, 3000, 5);
        io2.archives[GAME] = z.file;
        CHECK(z.offset.at("game/a/empty.pak") == z.offset.at("game/a/full.pak"));
        jmods::ModSet s;
        const auto* p = plan1(io2, "zf", { { "game/a/full.pak", noise(2000, 11) } }, s);
        CHECK(p && p->virt_size == z.file.size());
        jmods::ModSet s2;
        const auto* p2 = plan1(io2, "ze", { { "game/a/empty.pak", noise(10, 12) } }, s2);
        CHECK(!p2 && has_log(s2, "a zero-size row has no slot"));
    }
    // 7. an alias in place: logo_b <- alias res.pak (2048 B fits the 2048 B slot).
    {
        add_mod(io, "al", manifest("al", "{ \"entry\": \"game/logo/logo_b.pak\", \"alias\": \"game/resident/res.pak\" }"));
        const jmods::ModSet s = jmods::load_mod_set(io, "al\n", opts());
        const auto* p = s.plans.count(GAME) ? &s.plans.at(GAME) : nullptr;
        CHECK(p && p->virt_size == orig && p->overrides[0].virt == game.offset.at("game/logo/logo_b.pak"));
        if (p) CHECK(memcmp(&all(*p)[game.offset.at("game/logo/logo_b.pak")], game.data.at("game/resident/res.pak").data(), 2048) == 0);
    }
}

// ---------------------------------------------------------------------------------------------------- roster (ENHANCEMENT)
static std::string roster_mod(const std::string& id, const std::string& fighters, const std::string& ifs = "\"roster\": 1", const std::string& version = "1.0.0")
{
    return "{ \"schema\": 1, \"id\": \"" + id + "\", \"version\": \"" + version + "\", \"game\": { \"title_ids\": [\"BLUS31519\"] },\n"
           "  \"interfaces\": { " + ifs + " }, \"roster\": { \"fighters\": [ " + fighters + " ] } }\n";
}
static std::string ids_of(const std::vector<uint32_t>& l, size_t from, size_t n)
{
    std::string r;
    for (size_t i = from; i < from + n && i < l.size(); ++i) r += (r.empty() ? "" : " ") + std::to_string(l[i]);
    return r;
}
static void test_roster(MemIo& io)
{
    // one mod, one fighter after Joseph (40, index 8): list has 53 ids, 19 at index 9, the rest in stock order
    add_mod(io, "jo", roster_mod("jo", "{ \"id\": 19, \"after\": 40, \"logo_as\": 39, \"owned\": true }"));
    jmods::ModSet s = jmods::load_mod_set(io, "jo\n", opts());
    CHECK(mod(s, "jo") && mod(s, "jo")->loaded && mod(s, "jo")->applied && mod(s, "jo")->gameplay);
    CHECK(s.roster.size() == 1 && s.roster[0].id == 19 && s.roster[0].logo_as == 39 && s.roster[0].owned);
    CHECK(s.digest_kind == jmods::DigestKind::Value);   // a roster mod is gameplay: it has a digest
    CHECK(has_log(s, "[mods] jo: roster adds 1 fighter(s): 19(after 40, logo as 39, owned)"));
    std::vector<uint32_t> l = jmods::build_roster_list(s.roster);
    CHECK(l.size() == 53 && ids_of(l, 7, 4) == "39 40 19 24" && l[0] == 32 && l[52] == 201);
    std::vector<uint32_t> stock = jmods::build_roster_list({});
    CHECK(stock.size() == 52 && stock[8] == 40);
    {   // removing 19 gives the stock list back: nobody else moved
        std::vector<uint32_t> w = l; w.erase(w.begin() + 9);
        CHECK(w == stock);
    }
    const jpsm::Digest d1 = s.digest;
    // digest follows the set: other fighter data, other digest; same set, same digest
    add_mod(io, "jo2", roster_mod("jo2", "{ \"id\": 19, \"after\": 40, \"owned\": true }"));
    jmods::ModSet s2 = jmods::load_mod_set(io, "jo2\n", opts());
    CHECK(s2.digest_kind == jmods::DigestKind::Value && s2.digest != d1);
    jmods::ModSet s3 = jmods::load_mod_set(io, "jo\n", opts());
    CHECK(s3.digest == d1);
    // no roster mod: no digest and no fighters
    jmods::ModSet s0 = jmods::load_mod_set(io, "", opts());
    CHECK(s0.roster.empty() && s0.digest_kind == jmods::DigestKind::None);

    // several mods: order = load order; "after" may name an earlier mod's fighter; no anchor = before the first support
    add_mod(io, "ma", roster_mod("ma", "{ \"id\": 19, \"after\": 40 }"));
    add_mod(io, "mb", roster_mod("mb", "{ \"id\": 21, \"after\": 19 }"));
    jmods::ModSet m = jmods::load_mod_set(io, "ma\nmb\n", opts());
    CHECK(m.roster.size() == 2);
    l = jmods::build_roster_list(m.roster);
    CHECK(l.size() == 54 && ids_of(l, 8, 4) == "40 19 21 24");
    add_mod(io, "mc", roster_mod("mc", "{ \"id\": 20, \"after\": 40 }"));
    add_mod(io, "md", roster_mod("md", "{ \"id\": 21 }"));
    {   // same anchor chains behind the earlier fighter
        jmods::ModSet c = jmods::load_mod_set(io, "ma\nmc\n", opts());
        std::vector<uint32_t> cl = jmods::build_roster_list(c.roster);
        CHECK(cl.size() == 54 && ids_of(cl, 8, 4) == "40 19 20 24");
        jmods::ModSet d = jmods::load_mod_set(io, "md\n", opts());
        std::vector<uint32_t> dl = jmods::build_roster_list(d.roster);
        size_t k = 0; while (dl[k] != 206) ++k;
        CHECK(dl[k - 1] == 21);   // no anchor: right before the first support
    }
    jmods::ModSet mrev = jmods::load_mod_set(io, "mb\nma\n", opts());   // mb first: its "after 19" names nothing yet, so mb is refused whole
    CHECK(mod(mrev, "mb") && !mod(mrev, "mb")->loaded && has_log(mrev, "[mods] mb: refused: roster: fighter 21 goes after id 19"));
    CHECK(mrev.digest_kind == jmods::DigestKind::Refused);
    // the same set twice gives the same list and digest (deterministic)
    jmods::ModSet m2 = jmods::load_mod_set(io, "ma\nmb\n", opts());
    CHECK(m2.digest == m.digest && jmods::build_roster_list(m2.roster) == l);

    // refusals (each refuses the whole mod, with a reason)
    struct Bad { const char* fighters; const char* ifs; const char* why; };
    const Bad bad[] = {
        { "{ \"id\": 40, \"after\": 40 }", "\"roster\": 1", "must be 19, 20 or 21" },     // a stock id is not an allowed id at all
        { "{ \"id\": 19 }, { \"id\": 19 }", "\"roster\": 1", "fighter id 19 is already in the character list" },     // twice in one mod
        { "{ \"id\": 64 }", "\"roster\": 1", "must be 19, 20 or 21" },                                          // owned field / grid limit
        { "{ \"id\": 200 }", "\"roster\": 1", "must be 19, 20 or 21" },
        { "{ \"id\": 42 }", "\"roster\": 1", "must be 19, 20 or 21" },
        { "{ \"id\": 22 }", "\"roster\": 1", "must be 19, 20 or 21" },
        { "{ \"id\": -1 }", "\"roster\": 1", "must be 19, 20 or 21" },
        { "{ \"id\": 19, \"after\": 99 }", "\"roster\": 1", "which is not in the list" },                                   // unknown anchor
        { "{ \"id\": 19, \"after\": 19 }", "\"roster\": 1", "which is not in the list" },                                   // itself
        { "{ \"id\": 19, \"logo_as\": 300 }", "\"roster\": 1", "\"logo_as\" must be" },
        { "{ \"id\": 19, \"logo_as\": 20 }", "\"roster\": 1", "\"logo_as\" must be" },       // an added id is not a stock fighter
        { "{ \"id\": 19, \"logo_as\": 206 }", "\"roster\": 1", "\"logo_as\" must be" },      // a support
        { "{ \"id\": 19, \"logo_as\": 42 }", "\"roster\": 1", "\"logo_as\" must be" },       // not on the stock list
        { "{ \"id\": 19, \"logo_as\": -1 }", "\"roster\": 1", "\"logo_as\" must be" },
        { "{ \"id\": 19, \"owned\": 1 }", "\"roster\": 1", "\"owned\" must be true or false" },
        { "{ \"id\": 19, \"afer\": 40 }", "\"roster\": 1", "unknown key" },
        { "{ \"id\": 19 }", "\"cpk-entry\": 1", "needs interface \"roster\"" },
        { "{ \"id\": 19 }", "\"roster\": 3", "newer than this build supports" },
    };
    for (const Bad& b : bad) {
        add_mod(io, "bad", roster_mod("bad", b.fighters, b.ifs));
        jmods::ModSet r = jmods::load_mod_set(io, "bad\n", opts());
        CHECK(mod(r, "bad") && !mod(r, "bad")->loaded && r.roster.empty() && has_log(r, b.why));
        if (!(mod(r, "bad") && !mod(r, "bad")->loaded && has_log(r, b.why))) std::printf("  roster refusal case: %s\n", b.fighters);
        CHECK(r.digest_kind == jmods::DigestKind::Refused);   // requested, not applied: online refused
    }
    // a second mod with an id an earlier mod added is refused whole; the first stays
    add_mod(io, "dup1", roster_mod("dup1", "{ \"id\": 19 }"));
    add_mod(io, "dup2", roster_mod("dup2", "{ \"id\": 20 }, { \"id\": 19 }"));
    jmods::ModSet dd = jmods::load_mod_set(io, "dup1\ndup2\n", opts());
    CHECK(mod(dd, "dup1")->loaded && !mod(dd, "dup2")->loaded && dd.roster.size() == 1 && dd.digest_kind == jmods::DigestKind::Refused);
    // capacity: the list holds 56 ids; the three allowed ids make 55, so all three fit, alone or spread over three mods
    add_mod(io, "f3", roster_mod("f3", "{ \"id\": 19 }, { \"id\": 20 }, { \"id\": 21 }"));
    jmods::ModSet c3 = jmods::load_mod_set(io, "f3\n", opts());
    CHECK(mod(c3, "f3")->loaded && jmods::build_roster_list(c3.roster).size() == 55);
    {
        add_mod(io, "ta", roster_mod("ta", "{ \"id\": 19 }"));
        add_mod(io, "tb", roster_mod("tb", "{ \"id\": 20 }"));
        add_mod(io, "tc", roster_mod("tc", "{ \"id\": 21 }"));
        jmods::ModSet x = jmods::load_mod_set(io, "ta\ntb\ntc\n", opts());
        CHECK(mod(x, "ta")->loaded && mod(x, "tb")->loaded && mod(x, "tc")->loaded && x.roster.size() == 3);
        CHECK(jmods::build_roster_list(x.roster).size() == 55 && x.digest_kind == jmods::DigestKind::Value);
        jmods::ModSet y = jmods::load_mod_set(io, "ta\ntb\ntc\n", opts());   // the same set again: the same digest
        CHECK(y.digest == x.digest);
        jmods::ModSet z = jmods::load_mod_set(io, "ta\nf3\n", opts());   // f3 repeats 19: refused whole, ta stays
        CHECK(mod(z, "ta")->loaded && !mod(z, "f3")->loaded && z.roster.size() == 1);
    }
    {   // the cap itself (the id rule keeps real manifests under it): check_roster refuses a list that is one over a given cap
        jmods::Manifest m;
        m.roster.push_back({ 19, -1, -1, true });
        m.roster.push_back({ 20, -1, -1, true });
        std::set<int> ids = jmods::stock_roster_ids();
        std::string err;
        CHECK(!jmods::check_roster(m, ids, err, 53) && err.find("would not fit the select lists (room for 53)") != std::string::npos);
        CHECK(ids.size() == 52);   // nothing was added by the refused mod
        CHECK(jmods::check_roster(m, ids, err, 54) && ids.size() == 54);
        CHECK(jmods::kRosterGrid == 56 && jmods::stock_roster_ids().size() + 3 <= jmods::kRosterGrid);
    }
    // the title rule: a roster mod runs only on BLUS31519, and must name it
    add_mod(io, "eu", "{ \"schema\": 1, \"id\": \"eu\", \"version\": \"1\", \"game\": { \"title_ids\": [\"BLES02136\", \"BLUS31519\"] },\n"
                      "  \"interfaces\": { \"roster\": 1 }, \"roster\": { \"fighters\": [ { \"id\": 19 } ] } }\n");
    add_mod(io, "notitle", "{ \"schema\": 1, \"id\": \"notitle\", \"version\": \"1\",\n"
                           "  \"interfaces\": { \"roster\": 1 }, \"roster\": { \"fighters\": [ { \"id\": 19 } ] } }\n");
    add_mod(io, "wrong", "{ \"schema\": 1, \"id\": \"wrong\", \"version\": \"1\", \"game\": { \"title_ids\": [\"BLES02136\"] },\n"
                         "  \"interfaces\": { \"roster\": 1 }, \"roster\": { \"fighters\": [ { \"id\": 19 } ] } }\n");
    {
        jmods::Options eu = opts(); eu.title_id = "BLES02136";
        jmods::Options none = opts(); none.title_id = "";
        jmods::ModSet a = jmods::load_mod_set(io, "eu\n", eu), b = jmods::load_mod_set(io, "eu\n", none);
        CHECK(!mod(a, "eu")->loaded && has_log(a, "runs only on that build (this one is BLES02136)") && a.roster.empty());
        CHECK(!mod(b, "eu")->loaded && has_log(b, "(this one is unknown)") && b.roster.empty());
        jmods::ModSet c = jmods::load_mod_set(io, "notitle\n", opts()), d = jmods::load_mod_set(io, "wrong\n", opts());
        CHECK(!mod(c, "notitle")->loaded && !mod(d, "wrong")->loaded && c.roster.empty() && d.roster.empty());
        CHECK(a.digest_kind == jmods::DigestKind::Refused && c.digest_kind == jmods::DigestKind::Refused);
        jmods::ModSet ok = jmods::load_mod_set(io, "eu\n", opts());
        CHECK(mod(ok, "eu")->loaded && ok.roster.size() == 1);
    }
    // a roster mod beside a cosmetic mod: still gameplay, the cosmetic one does not change the digest
    add_mod(io, "cos", manifest("cos", "", ""));
    jmods::ModSet mix = jmods::load_mod_set(io, "jo\ncos\n", opts());
    CHECK(mix.digest_kind == jmods::DigestKind::Value && mix.digest == d1);
}

// ---------------------------------------------------------------------------------------------------- roster card_as (ENHANCEMENT)
static void test_card_as(MemIo& io)
{
    {   // card_as needs interface roster 2; roster 1 mods still load, without it
        add_mod(io, "v1", roster_mod("v1", "{ \"id\": 19, \"after\": 40 }"));
        CHECK(mod(jmods::load_mod_set(io, "v1\n", opts()), "v1")->loaded);
        add_mod(io, "v1", roster_mod("v1", "{ \"id\": 19, \"card_as\": 18 }"));
        jmods::ModSet r1 = jmods::load_mod_set(io, "v1\n", opts());
        CHECK(!mod(r1, "v1")->loaded && has_log(r1, "unknown key \"card_as\""));
        add_mod(io, "v1", roster_mod("v1", "{ \"id\": 19, \"card_as\": 18 }", "\"roster\": 2"));
        CHECK(mod(jmods::load_mod_set(io, "v1\n", opts()), "v1")->loaded);
        // the EU disc: a card_as mod is refused with the reason (the roster interface is US only for now, jsp_region.h)
        jmods::Options eu = opts(); eu.title_id = "BLES02136";
        add_mod(io, "v1", "{ \"schema\": 1, \"id\": \"v1\", \"version\": \"1\", \"game\": { \"title_ids\": [\"BLES02136\", \"BLUS31519\"] },\n"
                          "  \"interfaces\": { \"roster\": 2 }, \"roster\": { \"fighters\": [ { \"id\": 19, \"card_as\": 18 } ] } }\n");
        jmods::ModSet r2 = jmods::load_mod_set(io, "v1\n", eu);
        CHECK(!mod(r2, "v1")->loaded && r2.roster.empty() && has_log(r2, "the roster interface is not available on this disc yet"));
    }
    // parse: card_as is kept, logged, and does not change the digest (the card is cosmetic)
    const auto card_set = [&](const char* fighters) { add_mod(io, "ca", roster_mod("ca", fighters, "\"roster\": 2")); return jmods::load_mod_set(io, "ca\n", opts()); };   // one id: the digest names it
    jmods::ModSet a = card_set("{ \"id\": 19, \"after\": 40, \"logo_as\": 39, \"card_as\": 18 }");
    jmods::ModSet b = card_set("{ \"id\": 19, \"after\": 40, \"logo_as\": 39, \"card_as\": 7 }");
    jmods::ModSet n = card_set("{ \"id\": 19, \"after\": 40, \"logo_as\": 39 }");
    CHECK(mod(a, "ca") && mod(a, "ca")->loaded && a.roster.size() == 1 && a.roster[0].card_as == 18);
    CHECK(n.roster.size() == 1 && n.roster[0].card_as == -1);
    CHECK(has_log(a, "[mods] ca: roster adds 1 fighter(s): 19(after 40, logo as 39, owned, card as 18)"));
    CHECK(a.digest_kind == jmods::DigestKind::Value && a.digest == n.digest && a.digest == b.digest);   // not in the digest
    CHECK(jmods::build_roster_list(a.roster) == jmods::build_roster_list(n.roster));
    CHECK(jmods::card_donor_for(a.roster, 19) == 18 && jmods::card_donor_for(a.roster, 20) == -1 && jmods::card_donor_for(n.roster, 19) == -1);
    {   // a donor that is a support on the stock list is accepted; a changed gameplay field still changes the digest
        jmods::ModSet c = card_set("{ \"id\": 19, \"after\": 40, \"logo_as\": 39, \"card_as\": 211 }");
        CHECK(mod(c, "ca")->loaded && c.roster[0].card_as == 211 && c.digest == n.digest);
        CHECK(card_set("{ \"id\": 19, \"after\": 40, \"logo_as\": 38, \"card_as\": 18 }").digest != a.digest);
    }
    // refusals: the donor must be a stock-list id (a full card), never the added ids, a stub, an unknown or a non-number
    static const struct { const char* fighters; const char* why; } bad[] = {
        { "{ \"id\": 19, \"card_as\": 19 }", "\"card_as\" must be a stock fighter id" },    // itself: no card of its own
        { "{ \"id\": 19, \"card_as\": 20 }", "\"card_as\" must be a stock fighter id" },    // another added id
        { "{ \"id\": 19, \"card_as\": 21 }", "\"card_as\" must be a stock fighter id" },
        { "{ \"id\": 19, \"card_as\": 208 }", "\"card_as\" must be a stock fighter id" },   // a 64x55 stub in the card set
        { "{ \"id\": 19, \"card_as\": 42 }", "\"card_as\" must be a stock fighter id" },    // not on the stock list
        { "{ \"id\": 19, \"card_as\": 300 }", "\"card_as\" must be a stock fighter id" },
        { "{ \"id\": 19, \"card_as\": -1 }", "\"card_as\" must be a stock fighter id" },
        { "{ \"id\": 19, \"card_as\": \"18\" }", "\"card_as\" must be a stock fighter id" },
        { "{ \"id\": 19, \"card_as\": true }", "\"card_as\" must be a stock fighter id" },
    };
    for (const auto& c : bad) {
        add_mod(io, "badc", roster_mod("badc", c.fighters, "\"roster\": 2"));
        jmods::ModSet r = jmods::load_mod_set(io, "badc\n", opts());
        const bool ok = mod(r, "badc") && !mod(r, "badc")->loaded && r.roster.empty() && has_log(r, c.why);
        CHECK(ok);
        if (!ok) std::printf("  card_as refusal case: %s\n", c.fighters);
    }
    {   // check_roster itself (a Manifest built by hand, not parsed) refuses a bad donor too
        jmods::Manifest m;
        jmods::RosterFighter f; f.id = 19; f.card_as = 20; m.roster.push_back(f);
        std::set<int> ids = jmods::stock_roster_ids();
        std::string err;
        CHECK(!jmods::check_roster(m, ids, err) && err.find("card_as 20 is not a stock-list id") != std::string::npos && ids == jmods::stock_roster_ids());
        m.roster[0].card_as = 18;
        CHECK(jmods::check_roster(m, ids, err) && ids.count(19));
    }
    // the name the game looks up: only the exact card name parses, three digits
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_019.png") == 19 && jmods::card_name_id("_rgb8_256_chara_ico_000.png") == 0);
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_211.png") == 211);
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_19.png") == -1 && jmods::card_name_id("_rgb8_256_chara_ico_0019.png") == -1);
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_019.pn") == -1 && jmods::card_name_id("_rgb8_256_chara_ico_019.png ") == -1);
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_01a.png") == -1 && jmods::card_name_id("_rgb8_256_chara_ic0_019.png") == -1);
    CHECK(jmods::card_name_id("_rgb8_256_chara_ico_019_ALL.png") == -1 && jmods::card_name_id("") == -1);
    CHECK(jmods::card_name_for(18) == "_rgb8_256_chara_ico_018.png" && jmods::card_name_for(211) == "_rgb8_256_chara_ico_211.png");
    CHECK(jmods::card_name_id(jmods::card_name_for(7)) == 7);
    {   // the guest-side rewrite on a fake memory: only the exact added id's name changes, and only for the length of the lookup
        struct Mem {
            std::map<uint32_t, uint8_t> b; bool oob = false;   // oob: a read outside what was written
            uint8_t rd(uint32_t a) { auto it = b.find(a); if (it == b.end()) { oob = true; return 0; } return it->second; }
            void wr(uint32_t a, uint8_t v) { if (!b.count(a)) oob = true; b[a] = v; }
            void put(uint32_t a, const std::string& t) { b.clear(); for (size_t i = 0; i <= t.size(); ++i) b[a + (uint32_t)i] = i < t.size() ? (uint8_t)t[i] : 0; }
            std::string get(uint32_t a) { std::string t; while (b.count(a + (uint32_t)t.size()) && b[a + (uint32_t)t.size()]) t += (char)b[a + (uint32_t)t.size()]; return t; }
        } mem;
        const int ids[2] = { 19, 21 }, donors[2] = { 18, 211 };
        const auto rd = [&](uint32_t a) { return mem.rd(a); };
        const auto wr = [&](uint32_t a, uint8_t v) { mem.wr(a, v); };
        jmods::CardRedirect st;
        const uint32_t p = 0xD0000070u;
        mem.put(p, "_rgb8_256_chara_ico_019.png");
        CHECK(jmods::card_redirect_enter(rd, wr, p, ids, donors, 2, st) && mem.get(p) == "_rgb8_256_chara_ico_018.png" && st.at == p);
        jmods::card_redirect_leave(wr, st);
        CHECK(mem.get(p) == "_rgb8_256_chara_ico_019.png" && st.at == 0 && !mem.oob);
        mem.put(p, "_rgb8_256_chara_ico_021.png");
        CHECK(jmods::card_redirect_enter(rd, wr, p, ids, donors, 2, st) && mem.get(p) == "_rgb8_256_chara_ico_211.png");
        jmods::card_redirect_leave(wr, st);
        CHECK(mem.get(p) == "_rgb8_256_chara_ico_021.png" && !mem.oob);
        for (const char* other : { "_rgb8_256_chara_ico_020.png", "_rgb8_256_chara_ico_018.png", "_rgb8_256_chara_ico_300.png", "_rgb8_256_chara_ico_019.png.x",
                                   "_rgb8_256_chara_ico_19.png", "_rgb8_256_chara_ico_01", "x", "", "_rgb8_256_chara_ico_019.srd", "_rgb8_256_chara_ic_019.png" }) {
            mem.put(p, other);
            const std::string before = mem.get(p);
            CHECK(!jmods::card_redirect_enter(rd, wr, p, ids, donors, 2, st) && mem.get(p) == before && st.at == 0);
            jmods::card_redirect_leave(wr, st);   // nothing to put back
            CHECK(mem.get(p) == before && !mem.oob);   // never read past the string's own end
        }
        mem.put(p, "_rgb8_256_chara_ico_019.png");
        CHECK(!jmods::card_redirect_enter(rd, wr, p, ids, donors, 0, st) && mem.get(p) == "_rgb8_256_chara_ico_019.png");   // no mod: untouched
    }
}

// ---------------------------------------------------------------------------------------------------- loose files (ENHANCEMENT)
static void test_loose()
{
    // names the game builds for the added ids, and only those
    for (const char* ok : { "sound/JP/CV_019_00_ST_JP.acb", "sound/JP/PV_021_01_ST_JP.awb", "sound/jp/cv_020_00_st_jp.acb", "sound/EN/CV_019_00_ST_EN.acb",
                            "progress/game/loading_game/tips/loading_game_tips_chara_019_000_ALL_m.pak", "progress/game/loading_game/tips/loading_game_tips_chara_021_000_ALL_v.pak",
                            "progress/game/loading_game/tips/US/loading_game_tips_title_chara_019_000_US_m.pak", "progress/game/loading_game/tips/NS/loading_game_tips_mes_chara_020_000_NS_v.pak" })
        CHECK(jmods::loose_name_ok(ok));
    for (const char* bad : { "sound/JP/CV_018_00_ST_JP.acb", "sound/JP/CV_022_00_ST_JP.acb", "sound/JP/CV_019_00_ST_US.acb", "sound/JP/CV_019_0_ST_JP.acb", "sound/JP/BGM_JP.acb",
                             "sound/JP/CV_019_00_ST_JP.hca", "sound/JP/CV_019_00_ST_JP.acb.x", "sound/CV_019_00_ST_JP.acb", "sound/J/CV_019_00_ST_J.acb", "sound/JP/x/CV_019_00_ST_JP.acb",
                             "progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak", "progress/game/loading_game/tips/loading_game_tips_chara_019_001_ALL_m.pak",
                             "progress/game/loading_game/tips/loading_game_tips_chara_019_000_US_m.pak", "progress/game/loading_game/tips/US/loading_game_tips_title_chara_019_000_FR_m.pak",
                             "progress/game/loading_game/tips/US/loading_game_tips_chara_019_000_US_m.pak", "progress/game/loading_game/tips/US/loading_game_tips_title_chara_019_000_US_x.pak",
                             "progress/game/loading_game/loading_game_tips_chara_019_000_ALL_m.pak", "game/logo/x.pak", "", "sound" })
        CHECK(!jmods::loose_name_ok(bad));
    // the guest path forms
    CHECK(jmods::loose_rel_from_guest("/dev_bdvd/PS3_GAME/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.acb") == "sound/JP/CV_019_00_ST_JP.acb");
    CHECK(jmods::loose_rel_from_guest("/dev_hdd0/game/BLUS31519-INSTALL/USRDIR/data/PS3/sound/JP/CV_019_00_ST_JP.awb") == "sound/JP/CV_019_00_ST_JP.awb");
    CHECK(jmods::loose_rel_from_guest("/app_home/PS3_GAME/USRDIR/DATA/PS3/progress/game/x.pak") == "progress/game/x.pak");
    CHECK(jmods::loose_rel_from_guest("sound/JP/CV_019_00_ST_JP.acb") == "sound/JP/CV_019_00_ST_JP.acb");
    CHECK(jmods::loose_rel_from_guest("/sound/JP/a") == "sound/JP/a" && jmods::loose_rel_from_guest("data\\PS3\\sound\\JP\\a") == "sound/JP/a");
    CHECK(jmods::loose_rel_from_guest("game/logo/logo_US_v.pak") == "game/logo/logo_US_v.pak");   // an archive folder: looked up, then no loose entry
    CHECK(jmods::loose_rel_from_guest("/dev_hdd0/game/BLUS31519-INSTALL/USRDIR/other.dat").empty() && jmods::loose_rel_from_guest("").empty());
    CHECK(jmods::loose_rel_from_guest("/dev_bdvd/PS3_GAME/PARAM.SFO").empty());

    // synthetic archives with rows of the two kinds (a voice row at an allowed name, and a donor tip)
    MemIo io;
    const SynthCpk snd = make_cpk({ { "sound/JP/CV_018_00_ST_JP.acb", 3000 }, { "sound/JP/CV_018_00_ST_JP.awb", 5000 }, { "sound/JP/CV_021_00_ST_JP.acb", 2100 } }, false, 3000, 5);
    const SynthCpk prog = make_cpk({ { "progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak", 2500 },
                                     { "progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_v.pak", 4100 } }, true, 3100, 9);
    io.archives["partition_op_sound_ps3.cpk"] = snd.file;
    io.archives["partition_op_progress_ps3.cpk"] = prog.file;
    const Bytes cva = noise(1500, 71), cvb = noise(2600, 72), pva = noise(900, 73), pvb = noise(1100, 74);
    io.mods["vo/f/CV_019_00_ST_JP.acb"] = cva; io.mods["vo/f/CV_019_00_ST_JP.awb"] = cvb;
    io.mods["vo/f/PV_019_00_ST_JP.acb"] = pva; io.mods["vo/f/PV_019_00_ST_JP.awb"] = pvb;
    const std::string TIPS = "progress/game/loading_game/tips/";
    const std::string full = lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva) + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb) + ", " +
                             lf("sound/JP/PV_019_00_ST_JP.acb", "f/PV_019_00_ST_JP.acb", pva) + ", " + lf("sound/JP/PV_019_00_ST_JP.awb", "f/PV_019_00_ST_JP.awb", pvb) + ", " +
                             la(TIPS + "loading_game_tips_chara_019_000_ALL_m.pak", TIPS + "loading_game_tips_chara_018_000_ALL_m.pak") + ", " +
                             la(TIPS + "loading_game_tips_chara_019_000_ALL_v.pak", TIPS + "loading_game_tips_chara_018_000_ALL_v.pak");
    add_mod(io, "vo", loose_mod("vo", full));
    jmods::ModSet s = jmods::load_mod_set(io, "vo\n", opts());
    CHECK(mod(s, "vo") && mod(s, "vo")->loaded && mod(s, "vo")->applied && mod(s, "vo")->gameplay && s.loose.size() == 6);
    CHECK(s.digest_kind == jmods::DigestKind::Value);   // voices and tips are gameplay
    {
        const auto v = s.loose.find("sound/jp/cv_019_00_st_jp.acb");
        CHECK(v != s.loose.end() && v->second.file == "vo/f/CV_019_00_ST_JP.acb" && v->second.len == cva.size() && v->second.archive.empty() && v->second.content == jpsm::sha256(cva.data(), cva.size()));
        const auto t = s.loose.find(jmods::lower(TIPS + "loading_game_tips_chara_019_000_ALL_m.pak"));
        CHECK(t != s.loose.end() && t->second.file.empty() && t->second.archive == "partition_op_progress_ps3.cpk" && t->second.len == prog.data.at("progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak").size());
        CHECK(t != s.loose.end() && t->second.src == prog.offset.at("progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak"));
    }
    CHECK(has_log(s, "[mods] loose sound/JP/CV_019_00_ST_JP.acb <- vo f/CV_019_00_ST_JP.acb (1500 B)") && has_log(s, "<- alias progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak"));
    CHECK(s.plans.empty());   // no CPK is changed
    const jpsm::Digest d1 = s.digest;
    CHECK(jmods::load_mod_set(io, "vo\n", opts()).digest == d1);
    {   // the digest follows the bytes served and the path; "derived" is bookkeeping and stays out of it
        Bytes cva2 = cva; cva2[0] ^= 1;
        io.mods["vo/f/CV_019_00_ST_JP.acb"] = cva2;
        add_mod(io, "vo", loose_mod("vo", full));
        CHECK(mod(jmods::load_mod_set(io, "vo\n", opts()), "vo")->reason.find("does not match its sha256") != std::string::npos);
        add_mod(io, "vo", loose_mod("vo", lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva2) + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb)));
        const jmods::ModSet c = jmods::load_mod_set(io, "vo\n", opts());
        CHECK(c.digest_kind == jmods::DigestKind::Value && c.digest != d1);
        io.mods["vo/f/CV_019_00_ST_JP.acb"] = cva;
        const std::string pair = lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva, ", \"derived\": true") + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb);
        add_mod(io, "vo", loose_mod("vo", pair));
        const jmods::ModSet e = jmods::load_mod_set(io, "vo\n", opts());
        add_mod(io, "vo", loose_mod("vo", lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva) + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb)));
        CHECK(jmods::load_mod_set(io, "vo\n", opts()).digest == e.digest && mod(e, "vo")->m.loose[0].derived && !mod(e, "vo")->m.loose[1].derived);
    }
    add_mod(io, "vo", loose_mod("vo", full));

    {   // derived on a cpk-entry override and on a loose file: parsed, logged, out of the digest, an alias takes none
        const Bytes ob = noise(500, 91);
        io.mods["dv/f/o.pak"] = ob;
        const auto dv = [&](const std::string& more) {
            add_mod(io, "dv", "{ \"schema\": 1, \"id\": \"dv\", \"version\": \"1\", \"title\": \"t\", \"game\": { \"title_ids\": [\"BLUS31519\"] }, \"interfaces\": { \"cpk-entry\": 1 },\n"
                    "\"overrides\": [ { \"entry\": \"progress/game/loading_game/tips/loading_game_tips_chara_018_000_ALL_m.pak\", \"file\": \"f/o.pak\", \"sha256\": \"" + sha_hex(ob) + "\"" + more + " } ] }");
            return jmods::load_mod_set(io, "dv\n", opts());
        };
        const jmods::ModSet plain = dv(""), der = dv(", \"derived\": true");
        CHECK(mod(der, "dv")->loaded && jmods::manifest_derived(mod(der, "dv")->m) && !jmods::manifest_derived(mod(plain, "dv")->m));
        CHECK(has_log(der, "[mods] dv: marked derived") && !has_log(plain, "marked derived") && der.digest == plain.digest);
        CHECK(!mod(dv(", \"derived\": 1"), "dv")->loaded && has_log(dv(", \"derived\": 1"), "\"derived\" must be true or false"));
        CHECK(jmods::manifest_derived(mod(jmods::load_mod_set(io, "vo\n", opts()), "vo")->m) == false);
    }
    // refusals
    const std::string acb = lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva), awb = lf("sound/JP/CV_019_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb);
    const std::string tip = TIPS + "loading_game_tips_chara_019_000_ALL_m.pak", donor = TIPS + "loading_game_tips_chara_018_000_ALL_m.pak";
    static const std::string e19 = "\"sound/JP/CV_019_00_ST_JP.acb\"";
    struct Bad { std::string loose, ifs, why; };
    const std::vector<Bad> bad = {
        { acb, "\"loose-file\": 1", "its pair sound/JP/CV_019_00_ST_JP.awb is not in the same mod" },
        { awb, "\"loose-file\": 1", "its pair sound/JP/CV_019_00_ST_JP.acb is not in the same mod" },
        { acb + ", " + awb, "\"cpk-entry\": 1", "\"loose\" needs interface \"loose-file\"" },
        { acb + ", " + awb, "\"loose-file\": 2", "newer than this build supports" },
        { lf("sound/JP/CV_021_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva) + ", " + lf("sound/JP/CV_021_00_ST_JP.awb", "f/CV_019_00_ST_JP.awb", cvb), "\"loose-file\": 1", "has a row in partition_op_sound_ps3.cpk (sound/JP/CV_021_00_ST_JP.acb): use overrides (cpk-entry)" },
        { lf("sound/jp/cv_021_00_st_jp.acb", "f/CV_019_00_ST_JP.acb", cva) + ", " + lf("sound/jp/cv_021_00_st_jp.awb", "f/CV_019_00_ST_JP.awb", cvb), "\"loose-file\": 1", "has a row in partition_op_sound_ps3.cpk" },   // case does not hide a row
        { lf("sound/JP/CV_018_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "not a name the game builds for the added fighters" },
        { lf("sound/JP/BGM_JP.acb", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "not a name the game builds" },
        { lf("movie/x.usm", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "names no archive folder" },
        { lf("data/x.acb", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "names no archive folder" },
        { lf("../sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "no valid \"path\"" },
        { lf("sound\\\\JP\\\\CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "no valid \"path\"" },
        { lf("sound/JP/CV_019_00_ST_JP.acb", "../f/CV_019_00_ST_JP.acb", cva), "\"loose-file\": 1", "bad \"file\" path" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\", \"file\": \"f/CV_019_00_ST_JP.acb\" }", "\"loose-file\": 1", "needs a 64-digit \"sha256\"" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\" }", "\"loose-file\": 1", "give exactly one of" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\", \"alias\": \"a/b\", \"file\": \"f\" }", "\"loose-file\": 1", "give exactly one of" },
        { la("sound/JP/CV_019_00_ST_JP.acb", "sound/JP/CV_018_00_ST_JP.acb") + ", " + la("sound/jp/cv_019_00_st_jp.acb", "sound/JP/CV_018_00_ST_JP.acb"), "\"loose-file\": 1", "is listed twice" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\", \"alias\": \"sound/JP/CV_018_00_ST_JP.acb\", \"sha256\": \"" + sha_hex(cva) + "\" }", "\"loose-file\": 1", "takes no \"sha256\"" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\", \"alias\": \"sound/JP/CV_018_00_ST_JP.acb\", \"derived\": true }", "\"loose-file\": 1", "takes no \"derived\"" },
        { la("sound/JP/CV_019_00_ST_JP.acb", "sound/JP/CV_099_00_ST_JP.acb") + ", " + la("sound/JP/CV_019_00_ST_JP.awb", "sound/JP/CV_018_00_ST_JP.awb"), "\"loose-file\": 1", "alias sound/JP/CV_099_00_ST_JP.acb is not in partition_op_sound_ps3.cpk" },
        { la("sound/JP/CV_019_00_ST_JP.acb", "sound/jp/cv_018_00_st_jp.acb") + ", " + la("sound/JP/CV_019_00_ST_JP.awb", "sound/JP/CV_018_00_ST_JP.awb"), "\"loose-file\": 1", "(did you mean sound/JP/CV_018_00_ST_JP.acb?)" },
        { la("sound/JP/CV_019_00_ST_JP.acb", "data/x") + ", " + la("sound/JP/CV_019_00_ST_JP.awb", "sound/JP/CV_018_00_ST_JP.awb"), "\"loose-file\": 1", "bad \"alias\"" },
        { "{ \"path\": \"sound/JP/CV_019_00_ST_JP.acb\", \"file\": \"f/CV_019_00_ST_JP.acb\", \"sha256\": \"" + sha_hex(cva) + "\", \"x\": 1 }", "\"loose-file\": 1", "unknown key \"x\"" },
        { lf("sound/JP/CV_019_00_ST_JP.acb", "f/missing.acb", cva) + ", " + awb, "\"loose-file\": 1", "file f/missing.acb is missing" },
        { lf("sound/JP/CV_019_00_ST_JP.acb", "f/CV_019_00_ST_JP.awb", cva) + ", " + awb, "\"loose-file\": 1", "does not match its sha256" },
    };
    io.mods["bad/f/CV_019_00_ST_JP.acb"] = cva; io.mods["bad/f/CV_019_00_ST_JP.awb"] = cvb;
    for (const Bad& b : bad) {
        add_mod(io, "bad", loose_mod("bad", b.loose, b.ifs));
        const jmods::ModSet r = jmods::load_mod_set(io, "bad\n", opts());
        const bool ok = mod(r, "bad") && !mod(r, "bad")->loaded && r.loose.empty() && has_log(r, b.why);
        CHECK(ok);
        if (!ok) std::printf("  loose refusal case: %s | %s\n", b.loose.substr(0, 80).c_str(), b.why.c_str());
    }
    {   // another build: the mod runs only on BLUS31519
        add_mod(io, "vo", loose_mod("vo", full));
        jmods::Options o = opts(); o.title_id = "BLES02136";
        const jmods::ModSet r = jmods::load_mod_set(io, "vo\n", o);
        CHECK(!mod(r, "vo")->loaded && r.loose.empty());
        add_mod(io, "vo3", "{ \"schema\": 1, \"id\": \"vo3\", \"version\": \"1\", \"title\": \"t\", \"game\": { \"title_ids\": [\"BLES02136\", \"BLUS31519\"] }, \"interfaces\": { \"loose-file\": 1 }, \"loose\": [ " + acb + ", " + awb + " ] }");
        const jmods::ModSet r3 = jmods::load_mod_set(io, "vo3\n", o);   // the EU disc: refused with the reason
        CHECK(!mod(r3, "vo3")->loaded && r3.loose.empty() && has_log(r3, "the loose-file interface is not available on this disc yet"));
        add_mod(io, "vo2", "{ \"schema\": 1, \"id\": \"vo2\", \"version\": \"1\", \"title\": \"t\", \"game\": { \"title_ids\": [\"BLES02136\"] }, \"interfaces\": { \"loose-file\": 1 }, \"loose\": [ " + acb + ", " + awb + " ] }");
        CHECK(!mod(jmods::load_mod_set(io, "vo2\n", opts()), "vo2")->loaded);
    }
    {   // a refused loose mod makes the set refuse online (it is gameplay)
        add_mod(io, "bad", loose_mod("bad", acb));
        const jmods::ModSet r = jmods::load_mod_set(io, "bad\n", opts());
        CHECK(r.digest_kind == jmods::DigestKind::Refused);
    }

    // conflicts: the later mod wins a path; a group (or an .acb/.awb pair) that loses a member is dropped whole
    const Bytes alt = noise(777, 75), alt2 = noise(555, 76);
    io.mods["b/x.acb"] = alt; io.mods["b/x.awb"] = alt2;
    const std::string bpair = lf("sound/JP/CV_019_00_ST_JP.acb", "x.acb", alt) + ", " + lf("sound/JP/CV_019_00_ST_JP.awb", "x.awb", alt2);
    add_mod(io, "vo", loose_mod("vo", full));
    add_mod(io, "b", loose_mod("b", bpair));
    const jmods::ModSet c = jmods::load_mod_set(io, "vo\nb\n", opts());
    CHECK(has_log(c, "[mods] conflict loose sound/JP/CV_019_00_ST_JP.acb: vo overridden by b"));
    CHECK(c.loose.at("sound/jp/cv_019_00_st_jp.acb").mod_id == "b" && c.loose.at("sound/jp/cv_019_00_st_jp.awb").mod_id == "b" && c.loose.at("sound/jp/pv_019_00_st_jp.acb").mod_id == "vo" && c.loose.size() == 6);
    const jmods::ModSet c2 = jmods::load_mod_set(io, "b\nvo\n", opts());
    CHECK(c2.loose.at("sound/jp/cv_019_00_st_jp.acb").mod_id == "vo" && c2.digest != c.digest);   // load order is in the set
    {   // an explicit loose group: the later mod serves one member, so the whole group of the earlier mod is dropped
        io.mods["g/t1"] = noise(300, 81);
        const std::string gm = TIPS + "loading_game_tips_chara_019_000_ALL_m.pak", gv = TIPS + "loading_game_tips_chara_019_000_ALL_v.pak";
        add_mod(io, "ga", loose_mod("ga", lf(gm, "t1", io.mods["g/t1"]) + ", " + lf(gv, "t1", io.mods["g/t1"]), "\"loose-file\": 1", "\"groups\": [[\"" + gm + "\", \"" + gv + "\"]], "));
        io.mods["ga/t1"] = io.mods["g/t1"];
        const Bytes one = noise(310, 82);
        io.mods["gb/t"] = one;
        add_mod(io, "gb", loose_mod("gb", lf(gv, "t", one)));
        const jmods::ModSet g = jmods::load_mod_set(io, "ga\ngb\n", opts());
        CHECK(has_log(g, "[mods] ga: loose group of 2 dropped") && g.loose.size() == 1 && g.loose.begin()->second.mod_id == "gb");
        CHECK(has_log(g, "[mods] conflict loose " + gv + ": ga overridden by gb"));
        add_mod(io, "gb", loose_mod("gb", lf(gv, "t", one), "\"loose-file\": 1", "\"conflicts_ok\": [\"ga\"], "));
        CHECK(!has_log(jmods::load_mod_set(io, "ga\ngb\n", opts()), "[mods] conflict loose"));
        // a group of a cpk entry and a loose file is refused
        add_mod(io, "gx", loose_mod("gx", lf(gm, "t1", io.mods["g/t1"]), "\"loose-file\": 1, \"cpk-entry\": 1", "\"groups\": [[\"" + gm + "\", \"sound/JP/x\"]], "));
        io.mods["gx/t1"] = io.mods["g/t1"];
        CHECK(has_log(jmods::load_mod_set(io, "gx\n", opts()), "group entry sound/JP/x has no override in this mod"));
    }
}

int main()
{
    MemIo io;
    const SynthCpk game = make_cpk({ { "game/logo/logo_a.pak", 3001 }, { "game/logo/logo_b.pak", 384 }, { "game/resident/res.pak", 2048 },
                                     { "game/zz/last.pak", 777 } }, true, 1000, 441);
    const SynthCpk chara = make_cpk({ { "character/model/character_model_000_m.pak", 4100 }, { "character/model/character_model_000_i.pak", 2500 },
                                      { "character/model/character_model_000_v.pak", 6001 }, { "character/model/character_model_001_m.pak", 3900 },
                                      { "character/model/character_model_001_i.pak", 2047 }, { "character/model/character_model_001_v.pak", 5999 } },
                                    false, 2000, 3);
    io.archives[GAME] = game.file;
    io.archives[CHARA] = chara.file;

    test_helpers();
    test_plan_and_reads(io, game);
    test_inplace_edges();
    test_alias_and_classes(io, chara);
    test_refusals(io);
    test_conflicts_and_digest(io, game);
    test_glue(io, chara);
    test_roster(io);
    test_card_as(io);
    test_loose();
#ifdef MODS_GLUE_FILES
    test_glue_files(io, game, chara);
    test_glue_loose();
#endif

    std::printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
