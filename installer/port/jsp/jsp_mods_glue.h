// ENHANCEMENT: the pure parts of the mods/ glue (GMOD round 1, step R3/R5): the start decision from the environment, the archive a
// cellFs path names, the check that the copy an fd opened is the copy a plan was made from, the reads a virtual fd serves, and the online
// rules of a mod set. No OS or toolkit calls: jsp_mods.cpp does the files and the cellFs provider, and the host tests
// (installer/tools/mods_overlay_test.cpp, session_manifest_test.cpp) drive these on synthetic data.
#ifndef JSP_MODS_GLUE_H
#define JSP_MODS_GLUE_H

#include "jsp_mods_core.h"

#include <functional>

namespace jmods {

// ---- the start decision ------------------------------------------------------------------------------------------------------------
// JSP_MODS=0 is the kill switch. ENHANCEMENT (drop-in mods): mods run when JSP_MODS_DIR (the mods folder, the launcher sets it when the folder
// exists) or JSP_MODS_LIST (mods.txt, optional) is set; with neither, nothing is read. JSP_MODS_DIR unset with a list: every listed mod is
// refused ("no mod.json"), and a listed set that does not run as listed refuses online (DigestKind::Refused). A list without a file is an
// empty list (jsp_mods.cpp).
struct EnvChoice {
    bool on = false;
    std::string dir, list;   // as given
    std::string why;         // the "[mods]" line when off
};
inline EnvChoice choose_env(const char* mods, const char* dir, const char* list)
{
    EnvChoice c;
    if (mods && std::strcmp(mods, "0") == 0) { c.why = "[mods] off (JSP_MODS=0): stock game"; return c; }
    const bool has_list = list && *list, has_dir = dir && *dir;
    if (!has_list && !has_dir) { c.why = "[mods] no mods folder or list (JSP_MODS_DIR and JSP_MODS_LIST are not set): stock game"; return c; }
    c.on = true;
    if (has_list) c.list = list;
    if (dir) c.dir = dir;
    return c;
}

// ---- which archive a cellFs path names -----------------------------------------------------------------------------------------------
// The last part of a host or guest path ('/' or '\\'). The plans are keyed by the archive basename, which every copy shares (disc and hdd0).
inline std::string base_of(const char* path)
{
    if (!path) return "";
    const char* b = path;
    for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}
// The keys the glue looks paths up by. fold: ASCII case is folded (Windows, where the file system ignores case, so the game may open a
// planned archive under another case and still get the same file). A path key also turns '\\' into '/' and collapses repeated '/', so the
// paths the start-up check builds and the host paths cellFs hands the provider (ppu_fs host_path: '/' only) name the same copy.
inline std::string fold_ascii(std::string s)
{
    for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    return s;
}
inline std::string archive_key(const char* path, bool fold) { return fold ? fold_ascii(base_of(path)) : base_of(path); }
inline std::string path_key(const char* path, bool fold)
{
    std::string k;
    for (const char* p = path ? path : ""; *p; ++p) {
        const char ch = *p == '\\' ? '/' : *p;
        if (ch == '/' && !k.empty() && k.back() == '/') continue;
        k += ch;
    }
    return fold ? fold_ascii(k) : k;
}
// The game opens a CPK from the disc (<disc>/PS3_GAME/USRDIR/data/PS3/<archive>) or from an hdd0 install folder
// (<hdd0>/game/<folder>/USRDIR/data/PS3/<archive>). The copies the start-up check verifies, given the folders found under <hdd0>/game.
inline std::vector<std::string> copy_paths(const std::string& disc_dir, const std::string& hdd0_root, const std::vector<std::string>& game_folders,
                                           const std::string& archive)
{
    std::vector<std::string> v{ disc_dir + archive };
    for (const std::string& d : game_folders) v.push_back(hdd0_root + "/game/" + d + "/USRDIR/data/PS3/" + archive);
    return v;
}
// True when a gameplay mod (one the digest covers) serves an override in `archive`. A copy of such an archive that cannot be served (it is
// not the copy the plan was made from) would run stock bytes under a digest that says modded: the set then does not run as listed.
inline bool gameplay_archive(const ModSet& set, const std::string& archive)
{
    for (const ModState& st : set.mods)
        if (st.gameplay)
            for (const ResolvedOverride& r : st.ovr) if (r.served && r.archive == archive) return true;
    return false;
}
// The digest after the start-up copy check: a gameplay archive with a copy that differs makes a gameplay set refuse (online and rollback).
inline DigestKind after_copy_check(DigestKind kind, bool gameplay_copy_differs)
{
    return kind == DigestKind::Value && gameplay_copy_differs ? DigestKind::Refused : kind;
}

// ---- is this copy the copy the plan was made from -------------------------------------------------------------------------------------
// The plan was made from the disc copy; the game also opens the hdd0 install copies (byte-identical on a good install). A virtual fd reads
// the original bytes from its OWN copy, so that copy must have the same size, the same bytes up to the end of the TOC table (Plan::prefix:
// the header and the table the rewrite starts from) and, for each alias, the same donor bytes (their hashes are in the digest).
struct DonorCheck { uint64_t off = 0, len = 0; Digest content{}; std::string entry; };
struct CopyCheck {
    uint64_t size = 0, prefix_len = 0;
    Digest prefix{};
    std::vector<DonorCheck> donors;
};
inline CopyCheck copy_check(const ModSet& set, const Plan& p)
{
    CopyCheck c;
    c.size = p.orig_size;
    c.prefix_len = p.utf_abs + p.toc.size();
    c.prefix = p.prefix;
    for (const ModState& st : set.mods)
        for (const ResolvedOverride& r : st.ovr)
            if (r.served && r.archive == p.archive && !r.spec.alias.empty()) c.donors.push_back({ r.src, r.len, r.content, r.spec.alias });
    return c;
}
// read(off, dst, n) -> bytes read from the copy. "" = the copy matches, else why not.
using ReadFn = std::function<size_t(uint64_t, void*, size_t)>;
inline bool hash_range(const ReadFn& read, uint64_t off, uint64_t len, Digest& out)
{
    jpsm::Sha256 s;
    Bytes buf(1 << 16);
    for (uint64_t o = 0; o < len;) {
        const size_t k = size_t(std::min<uint64_t>(buf.size(), len - o));
        if (read(off + o, buf.data(), k) != k) return false;
        s.update(buf.data(), k);
        o += k;
    }
    out = s.finish();
    return true;
}
inline std::string verify_copy(const CopyCheck& c, uint64_t size, const ReadFn& read)
{
    if (size != c.size) return fmt("its size is %llu B, the planned copy's %llu B", (unsigned long long)size, (unsigned long long)c.size);
    Digest d{};
    if (!hash_range(read, 0, c.prefix_len, d)) return "cannot read its header and TOC";
    if (d != c.prefix) return "its header or TOC differs";
    for (const DonorCheck& k : c.donors) {
        if (!hash_range(read, k.off, k.len, d)) return "cannot read alias " + k.entry;
        if (d != k.content) return "its bytes of alias " + k.entry + " differ";
    }
    return "";
}

// ---- what a virtual fd serves ----------------------------------------------------------------------------------------------------------
// Plan::read with the original bytes from the fd's own copy (`host`) and the mod files from the handles the loader hashed them through.
class ServeIo : public Io {
public:
    ServeIo(ReadFn host, std::function<size_t(const std::string&, uint64_t, void*, size_t)> mod) : host_(std::move(host)), mod_(std::move(mod)) {}
    bool mod_size(const std::string&, uint64_t&) override { return false; }   // the plan knows every length
    size_t mod_read(const std::string& rel, uint64_t off, void* dst, size_t n) override { return mod_(rel, off, dst, n); }
    bool archive_size(const std::string&, uint64_t&) override { return false; }
    size_t archive_read(const std::string&, uint64_t off, void* dst, size_t n) override { return host_(off, dst, n); }
private:
    ReadFn host_;
    std::function<size_t(const std::string&, uint64_t, void*, size_t)> mod_;
};
// The first override of p that [pos, pos + n) touches, or -1 (for the one "first read in override segment" line per override).
inline int override_at(const Plan& p, uint64_t pos, uint64_t n)
{
    for (size_t i = 0; i < p.overrides.size(); ++i) {
        const PlannedOverride& o = p.overrides[i];
        if (pos < o.virt + o.len && o.virt < pos + n) return int(i);
    }
    return -1;
}

// Capture winning overrides at rollback admission. A failed capture never publishes a partial set.
using FrozenOverrides = std::map<std::string, std::vector<Bytes>>;
inline constexpr uint64_t kFrozenLimit = 256ull << 20;
inline const char* freeze_overrides(Io& io, const ModSet& set, FrozenOverrides& result, uint64_t limit = kFrozenLimit)
{
    uint64_t remaining = limit;
    for (const auto& [archive, plan] : set.plans) {
        for (const auto& o : plan.overrides) {
            if (o.len > remaining) return "verified replacement data exceeds the rollback cache limit (256 MiB)";
            remaining -= o.len;
        }
    }
    FrozenOverrides pending;
    for (const auto& [archive, plan] : set.plans) {
        auto& frozen = pending[archive];
        for (const auto& o : plan.overrides) {
            Bytes& bytes = frozen.emplace_back(size_t(o.len));
            size_t got = o.alias ? io.archive_read(archive, o.donor_offset, bytes.data(), bytes.size()) :
                                   io.mod_read(o.mod_id + "/" + o.source, 0, bytes.data(), bytes.size());
            if (got != bytes.size() || jpsm::sha256(bytes.data(), bytes.size()) != o.content)
                return "a mod file or alias donor changed before rollback could verify it";
        }
    }
    result.swap(pending);
    return nullptr;
}

// ---- online ----------------------------------------------------------------------------------------------------------------------------
// Legacy folded build word. Schema 3 checks full session content separately.
inline bool net_build(uint32_t stock, DigestKind kind, const Digest& digest, uint32_t& out)
{
    if (kind == DigestKind::Refused) return false;
    if (kind == DigestKind::None) { out = stock; return true; }
    uint8_t b[4 + 32];
    for (int i = 0; i < 4; ++i) b[i] = uint8_t(stock >> (8 * i));
    memcpy(b + 4, digest.data(), digest.size());
    out = be32(jpsm::sha256(b, sizeof b).data());
    return true;
}
// The game's own online (Ranked and Free over np_psnr) has no build word to compare, so any gameplay set refuses it (D40 P2): only a set with
// no gameplay digest (stock or cosmetic only) may use it.
inline bool game_online_allowed(DigestKind kind) { return kind == DigestKind::None; }
// The rollback refusal text (jsp_net_game.cpp logs it as "[net] rollback refused: <text>: off"). The build word is fixed once at start, so a
// gameplay archive copy that is first opened later and differs (jsp_mods_copy_mismatch) is checked again before the session's rendezvous
// is polled and again before the settings handshake's Hello: nullptr = the session may go on.
inline constexpr const char* kModsNotAsListed = "the mod set does not run as listed (see the [mods] lines)";
inline const char* rollback_gate(bool copy_mismatch) { return copy_mismatch ? kModsNotAsListed : nullptr; }

}  // namespace jmods

#endif  // JSP_MODS_GLUE_H
