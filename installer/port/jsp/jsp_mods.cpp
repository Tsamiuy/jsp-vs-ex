/* jsp_mods.cpp: ENHANCEMENT: the mods/ folder in the game (GMOD round 1, step R3). It reads the enable list and the mods, plans one virtual
 * file per CPK archive (jsp_mods_core.h), and serves those archives through the toolkit's virtual-file hook (patch 6502,
 * ps3emu/fs_vfile.h). Nothing on disk is written: the archives and their hdd0 copies stay as they are, so a mod that is switched off is gone
 * at the next start.
 *
 * Environment (the launcher sets the two paths, installer/launcher/launcher.c):
 *   JSP_MODS_LIST  mods.txt, the enable list (one mod id per line, load order). Unset: the stock game, and no provider is registered.
 *   JSP_MODS_DIR   the mods folder (<install>\mods), each mod in <id>\ with its mod.json.
 *   JSP_MODS=0     the kill switch: the stock game whatever the list says.
 * main.cpp calls jsp_mods_init() before the PPU table and the rollback install, so the rollback build word (jsp_net_game.cpp) and the
 * game's own online (jsp_online.cpp) see the set.
 *
 * Files: a mod file is opened once, with writers and deleters shut out for the whole session (Windows share mode), hashed through that handle
 * and served through it, so the bytes served are the bytes in the digest. A path whose final location is not exactly <mods>\<id>\<path>
 * (a symbolic link or a junction anywhere below the mods folder) is refused, not followed. The archives are planned from the disc copy
 * (<disc>/PS3_GAME/USRDIR/data/PS3/<archive>, the path the game opens) and each fd serves the original bytes from its own copy (disc or
 * hdd0), once that copy is checked to have the planned size, header, TOC and alias bytes.
 * Logs: "[mods] ..." lines on stderr (play_stderr.txt).
 */
#include "jsp_mods_glue.h"
#include "ps3emu/fs_vfile.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" void jsp_online_refuse_for_mods(void);   /* jsp_online.cpp: the game's own online off for this session, with the reason */

namespace {

using namespace jmods;

void logl(const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); }

int seek64(FILE* f, uint64_t off)
{
#ifdef _WIN32
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}
bool size64(FILE* f, uint64_t& size)
{
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END)) return false;
    const long long n = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END)) return false;
    const long long n = (long long)ftello(f);
#endif
    if (n < 0) return false;
    size = (uint64_t)n;
    return true;
}
size_t file_read(FILE* f, uint64_t off, void* dst, size_t n)
{
    if (seek64(f, off)) return 0;
    return std::fread(dst, 1, n, f);
}

#ifdef _WIN32
std::wstring wide(const std::string& u)
{
    if (u.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), &w[0], n);
    return w;
}
std::string utf8(const wchar_t* w)
{
    if (!w || !*w) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 1 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
// An environment value as UTF-8 (the paths may hold any character the player's folders do); nullptr when unset.
const char* env_utf8(const char* name, std::string& keep)
{
    const wchar_t* v = _wgetenv(wide(name).c_str());
    if (!v) return nullptr;
    keep = utf8(v);
    return keep.c_str();
}
std::wstring final_path(HANDLE h)
{
    wchar_t b[1024];
    const DWORD n = GetFinalPathNameByHandleW(h, b, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    return n > 0 && n < 1024 ? std::wstring(b, n) : std::wstring();
}
FILE* open_utf8(const std::string& path) { return _wfopen(wide(path).c_str(), L"rb"); }
#else
const char* env_utf8(const char* name, std::string& keep)
{
    const char* v = std::getenv(name);
    if (!v) return nullptr;
    keep = v;
    return keep.c_str();
}
FILE* open_utf8(const std::string& path) { return std::fopen(path.c_str(), "rb"); }
#endif

// ---- the mods folder: files opened once, never through a link -----------------------------------------------------------------------
struct ModFile {
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    uint64_t size = 0;
    bool ok = false;
};

class ModsRoot {
public:
    bool open(const std::string& dir, std::string& why)
    {
#ifdef _WIN32
        HANDLE h = CreateFileW(wide(dir).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) { why = fmt("cannot open it (error %lu)", (unsigned long)GetLastError()); return false; }
        BY_HANDLE_FILE_INFORMATION fi;
        const bool dir_ok = GetFileInformationByHandle(h, &fi) && (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        root_ = final_path(h);
        CloseHandle(h);
        if (!dir_ok) { why = "it is not a folder"; return false; }
        if (root_.empty()) { why = "cannot resolve its path"; return false; }
        while (!root_.empty() && root_.back() == L'\\') root_.pop_back();
        return true;
#else
        root_ = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (root_ < 0) { why = std::string("cannot open it: ") + std::strerror(errno); return false; }
        return true;
#endif
    }
    // rel: "<id>/<path>", already checked by valid_rel_path (no '..', no device names, no '\\' or ':').
    bool open_file(const std::string& rel, ModFile& f, std::string& why) const
    {
#ifdef _WIN32
        std::wstring r = wide(rel);
        for (auto& c : r) if (c == L'/') c = L'\\';
        const std::wstring path = root_ + L"\\" + r;
        // FILE_SHARE_READ only: nobody may write, rename or delete the file while the game has it (it is hashed and served through this handle).
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD e = GetLastError();
            why = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? "not found"
                : e == ERROR_SHARING_VIOLATION ? "another program has it open for writing"
                : fmt("cannot open it (error %lu)", (unsigned long)e);
            return false;
        }
        BY_HANDLE_FILE_INFORMATION fi;
        LARGE_INTEGER sz;
        const std::wstring fin = final_path(h);
        if (!GetFileInformationByHandle(h, &fi) || (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(h, &sz)) {
            CloseHandle(h);
            why = "not a plain file";
            return false;
        }
        // The file must be where its name says: a link or junction anywhere below the mods folder leads somewhere else and is not followed.
        if (fin.empty() || CompareStringOrdinal(fin.c_str(), (int)fin.size(), path.c_str(), (int)path.size(), TRUE) != CSTR_EQUAL) {
            CloseHandle(h);
            why = "it is reached through a link or junction (not followed)";
            return false;
        }
        f.h = h;
        f.size = (uint64_t)sz.QuadPart;
        f.ok = true;
        return true;
#else
        int d = dup(root_);
        size_t s = 0;
        for (;;) {
            const size_t e = rel.find('/', s);
            const bool last = e == std::string::npos;
            const std::string part = rel.substr(s, last ? std::string::npos : e - s);
            const int n = openat(d, part.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (last ? 0 : O_DIRECTORY));
            const int err = errno;
            ::close(d);
            if (n < 0) {
                why = err == ENOENT ? "not found" : err == ELOOP || err == ENOTDIR ? "it is reached through a link (not followed)"
                                                                                  : std::string("cannot open it: ") + std::strerror(err);
                return false;
            }
            d = n;
            if (last) break;
            s = e + 1;
        }
        struct stat st;
        if (fstat(d, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(d); why = "not a plain file"; return false; }
        f.fd = d;
        f.size = (uint64_t)st.st_size;
        f.ok = true;
        return true;
#endif
    }
    static size_t read(const ModFile& f, uint64_t off, void* dst, size_t n)
    {
        if (!f.ok) return 0;
        size_t done = 0;
        uint8_t* p = (uint8_t*)dst;
        while (done < n) {
#ifdef _WIN32
            OVERLAPPED ov = {};   // a positional read: the handle's own position is not shared state
            const uint64_t o = off + done;
            ov.Offset = (DWORD)o;
            ov.OffsetHigh = (DWORD)(o >> 32);
            DWORD got = 0;
            const DWORD want = (DWORD)std::min<size_t>(n - done, 1u << 30);
            if (!ReadFile(f.h, p + done, want, &got, &ov) || got == 0) break;
#else
            const ssize_t got = pread(f.fd, p + done, n - done, (off_t)(off + done));
            if (got <= 0) break;
#endif
            done += (size_t)got;
        }
        return done;
    }
    static void close(ModFile& f)
    {
#ifdef _WIN32
        if (f.h != INVALID_HANDLE_VALUE) CloseHandle(f.h);
        f.h = INVALID_HANDLE_VALUE;
#else
        if (f.fd >= 0) ::close(f.fd);
        f.fd = -1;
#endif
        f.ok = false;
    }
private:
#ifdef _WIN32
    std::wstring root_;
#else
    int root_ = -1;
#endif
};

// ---- the session's state: built once by jsp_mods_init, read-only afterwards (except `verified`, under `mu`) ---------------------------
struct Served {
    Plan plan;
    CopyCheck check;
    std::unique_ptr<std::atomic<bool>[]> logged;   // one "first read in override segment" line per override
};
struct State {
    ModsRoot root;
    bool root_ok = false;
    std::map<std::string, ModFile> files;   // "<id>/<path>" -> the open file (a failed open is kept with ok = false)
    ModSet set;
    std::map<std::string, Served> served;   // by archive basename
    std::mutex mu;
    std::map<std::string, std::string> verified;   // host path -> "" (serves) or why it is served stock
};
State* g_st = nullptr;                                // set once by jsp_mods_init, never freed (fds may outlive any teardown)
std::atomic<int> g_kind{ (int)DigestKind::None };   // the set's DigestKind

// Reads for load_mod_set: mod files through ModsRoot, archives from the disc copy.
class LoadIo : public Io {
public:
    LoadIo(State& st, std::string disc) : st_(st), disc_(std::move(disc)) {}
    ~LoadIo() override { for (auto& kv : arch_) if (kv.second) std::fclose(kv.second); }
    bool mod_size(const std::string& rel, uint64_t& size) override
    {
        const ModFile* f = file(rel);
        if (!f) return false;
        size = f->size;
        return true;
    }
    size_t mod_read(const std::string& rel, uint64_t off, void* dst, size_t n) override
    {
        const ModFile* f = file(rel);
        return f ? ModsRoot::read(*f, off, dst, n) : 0;
    }
    bool archive_size(const std::string& base, uint64_t& size) override
    {
        FILE* f = arch(base);
        return f && size64(f, size);
    }
    size_t archive_read(const std::string& base, uint64_t off, void* dst, size_t n) override
    {
        FILE* f = arch(base);
        return f ? file_read(f, off, dst, n) : 0;
    }
private:
    State& st_;
    std::string disc_;
    std::map<std::string, FILE*> arch_;
    const ModFile* file(const std::string& rel)
    {
        auto it = st_.files.find(rel);
        if (it == st_.files.end()) {
            ModFile f;
            std::string why = "the mods folder is not open";
            if (!valid_rel_path(rel)) why = "not a valid mod path";
            else if (st_.root_ok && st_.root.open_file(rel, f, why)) why.clear();
            if (!why.empty() && why != "not found") logl("[mods] " + rel + ": " + why);
            it = st_.files.emplace(rel, f).first;
        }
        return it->second.ok ? &it->second : nullptr;
    }
    FILE* arch(const std::string& base)
    {
        auto it = arch_.find(base);
        if (it == arch_.end()) it = arch_.emplace(base, std::fopen((disc_ + base).c_str(), "rb")).first;   // the path cellFs opens (ppu_fs.cpp, narrow)
        return it->second;
    }
};

// ---- the cellFs provider (ps3emu/fs_vfile.h) ----------------------------------------------------------------------------------------
struct View { const Served* s; std::string guest; };

// "" when the copy at host_path is the planned one. Called once per host path (the result is kept).
std::string check_copy(const Served& s, const char* host_path)
{
    FILE* f = std::fopen(host_path, "rb");   // a second, read-only handle of our own, closed before open() returns
    if (!f) return "cannot open it";
    uint64_t size = 0;
    std::string why = size64(f, size) ? verify_copy(s.check, size, [f](uint64_t off, void* d, size_t n) { return file_read(f, off, d, n); })
                                      : "cannot read its size";
    std::fclose(f);
    return why;
}

ps3_fs_vfile* v_open(const char* guest_path, const char* host_path)
{
    State* st = g_st;
    if (!st || !host_path) return nullptr;
    const auto it = st->served.find(base_of(host_path));
    if (it == st->served.end()) return nullptr;
    std::string why;
    {
        std::lock_guard<std::mutex> lk(st->mu);   // never held while taking another fd's stream lock: only our own FILE is used in here
        auto v = st->verified.find(host_path);
        if (v == st->verified.end()) {
            why = check_copy(it->second, host_path);
            st->verified.emplace(host_path, why);
            if (why.empty()) logl(fmt("[mods] serve %s (%s): checked, %llu override(s), virtual size %llu B", it->first.c_str(), guest_path ? guest_path : "?",
                                      (unsigned long long)it->second.plan.overrides.size(), (unsigned long long)it->second.plan.virt_size));
            else logl(fmt("[mods] serve %s (%s): NOT served, the stock file is read: this copy is not the one the mods were planned on (%s)",
                          it->first.c_str(), guest_path ? guest_path : "?", why.c_str()));
        } else why = v->second;
    }
    if (!why.empty()) return nullptr;
    return reinterpret_cast<ps3_fs_vfile*>(new View{ &it->second, guest_path ? guest_path : "" });
}
uint64_t v_size(ps3_fs_vfile* vf) { return reinterpret_cast<View*>(vf)->s->plan.virt_size; }
size_t v_read(ps3_fs_vfile* vf, FILE* host, uint64_t pos, void* dst, size_t n)
{
    const View* v = reinterpret_cast<View*>(vf);
    const Served& s = *v->s;
    State* st = g_st;
    ServeIo io([host](uint64_t off, void* d, size_t k) { return file_read(host, off, d, k); },   // the caller holds host's stream lock
               [st](const std::string& rel, uint64_t off, void* d, size_t k) -> size_t {
                   const auto f = st->files.find(rel);
                   return f == st->files.end() ? 0 : ModsRoot::read(f->second, off, d, k);
               });
    const size_t got = s.plan.read(io, pos, dst, n);
    for (size_t i = 0; i < s.plan.overrides.size(); ++i) {
        const PlannedOverride& o = s.plan.overrides[i];
        if (pos < o.virt + o.len && o.virt < pos + n && !s.logged[i].exchange(true))
            logl(fmt("[mods] serve %s (%s) %s: first read in override segment (%s)", s.plan.archive.c_str(), v->guest.c_str(), o.entry.c_str(), o.mod_id.c_str()));
    }
    if (got < n) logl(fmt("[mods] serve %s: short read at %llu (%llu of %llu B)", s.plan.archive.c_str(), (unsigned long long)pos,
                          (unsigned long long)got, (unsigned long long)n));
    return got;
}
void v_close(ps3_fs_vfile* vf) { delete reinterpret_cast<View*>(vf); }
const ps3_fs_vfile_ops k_ops = { v_open, v_size, v_read, v_close };

std::string eboot_id(const char* path)
{
    FILE* f = path ? std::fopen(path, "rb") : nullptr;
    if (!f) return "unknown";
    jpsm::Sha256 s;
    Bytes b(1 << 16);
    size_t n;
    while ((n = std::fread(b.data(), 1, b.size(), f)) > 0) s.update(b.data(), n);
    std::fclose(f);
    return "sha256:" + hex(s.finish());
}

}  // namespace

/* vfs_root: the folder holding PS3_GAME (main.cpp's ppu_vfs_root); eboot: the ELF main() loads (its SHA-256 is the digest's EBOOT id);
 * title_id: PARAM.SFO's (manifests name the game versions they are for). Call once, before the guest starts. */
extern "C" void jsp_mods_init(const char* vfs_root, const char* eboot, const char* title_id)
{
    std::string k1, k2, k3;
    const EnvChoice c = choose_env(env_utf8("JSP_MODS", k1), env_utf8("JSP_MODS_DIR", k2), env_utf8("JSP_MODS_LIST", k3));
    if (!c.on) { logl(c.why); return; }   // off means stock: no provider is registered at all
    std::string text;
    {
        FILE* f = open_utf8(c.list);
        uint64_t size = 0;
        bool ok = f && size64(f, size) && size <= (1u << 20);
        if (ok) { text.resize((size_t)size); ok = file_read(f, 0, text.data(), text.size()) == text.size(); }
        if (f) std::fclose(f);
        if (!ok) { logl("[mods] list " + c.list + ": cannot read it (or it is over 1 MiB): stock game"); return; }
    }
    State* st = new State;
    std::string why = "JSP_MODS_DIR is not set";
    if (!c.dir.empty()) st->root_ok = st->root.open(c.dir, why);
    if (!st->root_ok) logl("[mods] mods folder " + (c.dir.empty() ? std::string("(none)") : c.dir) + ": " + why + "; every listed mod is refused");
    Options opt;
    opt.list_name = c.list;
    opt.title_id = title_id ? title_id : "";
    opt.eboot_id = eboot_id(eboot);
    {
        std::string disc = vfs_root && *vfs_root ? vfs_root : ".";
        while (!disc.empty() && (disc.back() == '/' || disc.back() == '\\')) disc.pop_back();
        LoadIo io(*st, disc + "/PS3_GAME/USRDIR/data/PS3/");
        st->set = load_mod_set(io, text, opt);
    }
    for (const std::string& l : st->set.log) logl(l);
    // Keep only the files a plan serves; mod.json and refused mods' files are closed now.
    std::map<std::string, bool> used;
    for (const auto& kv : st->set.plans)
        for (const Segment& sg : kv.second.segs) if (sg.kind == Segment::ModFile) used[sg.rel] = true;
    for (auto& kv : st->files) if (!used.count(kv.first)) ModsRoot::close(kv.second);
    for (const auto& kv : st->set.plans) {
        Served& s = st->served[kv.first];
        s.plan = kv.second;
        s.check = copy_check(st->set, kv.second);
        s.logged.reset(new std::atomic<bool>[kv.second.overrides.size() + 1]());
    }
    g_kind.store((int)st->set.digest_kind);
    g_st = st;
    if (!st->served.empty()) {
        __atomic_store_n(&g_ps3_fs_vfile_ops, &k_ops, __ATOMIC_RELEASE);
        std::string names;
        for (const auto& kv : st->served) names += (names.empty() ? "" : ", ") + kv.first;
        logl(fmt("[mods] serving %llu archive(s): %s (eboot %s)", (unsigned long long)st->served.size(), names.c_str(), opt.eboot_id.c_str()));
    }
    if (!game_online_allowed(st->set.digest_kind)) jsp_online_refuse_for_mods();
}

/* The rollback build word with the mod set folded in (jsp_mods_glue.h net_build). 1 = *out is the word to use; 0 = refused (gameplay mods
 * were listed but not all applied): rollback must not start. */
extern "C" int jsp_mods_net_build(uint32_t stock, uint32_t* out)
{
    const DigestKind k = (DigestKind)g_kind.load();
    const State* st = g_st;
    uint32_t v = stock;
    if (!net_build(stock, k, st ? st->set.digest : Digest{}, v)) {
        logl("[mods] rollback refused: gameplay mods were requested but not applied on this build");
        return 0;
    }
    if (k == DigestKind::Value)
        logl(fmt("[mods] set: gameplay digest %s..., net build 0x%08x (stock 0x%08x)", hex(st->set.digest).substr(0, 16).c_str(), v, stock));
    *out = v;
    return 1;
}
