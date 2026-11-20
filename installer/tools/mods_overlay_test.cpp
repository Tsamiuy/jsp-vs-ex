// Host test of the mods/ framework core (installer/port/jsp/jsp_mods_core.h): mods.txt and manifest parsing, interface check, the CPK @UTF
// TOC rewrite, the virtual-file plan (original bytes, rewritten TOC, appended files at 2048 alignment), file and alias sources, conflicts,
// groups, classification and the mod-set digest. No game data: the test writes its own SYNTHETIC CPK archives (a small @UTF writer, masked
// and unmasked tables, odd file sizes, an unaligned end) and mod files in memory.
// installer/tools/mods_overlay_test.sh builds and runs it (g++ or clang++, C++20)
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "jsp_mods_core.h"

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
    uint64_t toc_offset = 0x800, utf_abs = 0, utf_len = 0;
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
static Bytes read_all(MemIo& io, const jmods::Plan& p, size_t chunk_size, uint64_t start = 0)
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

    CHECK(jmods::is_cosmetic_path("game/logo/logo_US_v.pak") && jmods::is_cosmetic_path("sound/bgm/x.acb") && jmods::is_cosmetic_path("movie/a.usm"));
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
    const Bytes logo_new = noise(5001, 77), small_new = noise(13, 78);
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
    CHECK(p.orig_size == orig && p.virt_size % 2048 == 0);
    CHECK(p.overrides.size() == 2 && p.overrides[0].entry == "game/logo/logo_a.pak" && p.overrides[0].virt == jmods::align_up(orig));
    CHECK(p.overrides[1].virt == jmods::align_up(p.overrides[0].virt + 5001) && p.virt_size == jmods::align_up(p.overrides[1].virt + 13));

    // The reference image: the original, the three TOC fields of both rows rewritten, zero padding, the two files.
    Bytes ref = game.file;
    ref.resize(p.virt_size, 0);
    Bytes toc = game.toc_plain;
    std::set<size_t> changed;   // positions in the TOC table the rewrite may touch
    for (const auto& o : p.overrides) {
        const auto& rp = game.pos[game.row_of.at(o.entry)];
        jmods::put_be32(&toc[rp.at("FileSize")], uint32_t(o.len));
        jmods::put_be32(&toc[rp.at("ExtractSize")], uint32_t(o.len));
        jmods::put_be64(&toc[rp.at("FileOffset")], o.virt - game.toc_offset);
        for (const char* f : { "FileSize", "ExtractSize", "FileOffset" })
            for (int k = 0; k < (std::string(f) == "FileOffset" ? 8 : 4); ++k) changed.insert(rp.at(f) + k);
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
            if ((i < game.utf_abs || i >= game.utf_abs + game.utf_len) && v[i] != game.file[i]) same = false;
        CHECK(same && diffs_outside == 0 && diffs_inside > 0);
    }
    // Reads around borders: crossing the original end into the padding, crossing the virtual end, at and past the end.
    {
        uint8_t b[64];
        CHECK(p.read(io, orig - 5, b, 10) == 10 && memcmp(b, &game.file[orig - 5], 5) == 0 && b[5] == 0 && b[9] == 0);
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
    test_alias_and_classes(io, chara);
    test_refusals(io);
    test_conflicts_and_digest(io, game);

    std::printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
