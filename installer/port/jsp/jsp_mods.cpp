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
 * Files: a mod file is opened once, with other Windows programs' writers and deleters shut out for the whole session (the Windows share mode;
 * under Proton a native Linux program can still rewrite it), hashed through that handle and served through it, so the bytes served are the
 * bytes in the digest. A path whose final location is not exactly <mods>\<id>\<path>
 * (a symbolic link or a junction anywhere below the mods folder) is refused, not followed. The archives are planned from the disc copy
 * (<disc>/PS3_GAME/USRDIR/data/PS3/<archive>, the path the game opens) and each fd serves the original bytes from its own copy (disc or
 * hdd0), once that copy is checked to have the planned size, header, TOC and alias bytes. Every copy the game can open (the disc copy and
 * <hdd0>/game/<folder>/USRDIR/data/PS3/<archive>) is checked at start: a copy of an archive a gameplay mod changes that differs makes the
 * set Refused (no rollback, no game online), since that copy would run stock under a digest that says modded. A copy first seen later that
 * differs (for an archive a gameplay mod changes) is logged loudly and sets jsp_mods_copy_mismatch(): jsp_net_game.cpp then stops a rollback
 * session that has not sent its settings-handshake Hello (it checks before polling the rendezvous and again before the Hello); a session
 * already past its Hello is not ended. A copy that differed is checked again when its size has changed since (an install being written).
 * On Windows the archive name is matched without regard to case.
 * Logs: "[mods] ..." lines on stderr (play_stderr.txt).
 */
#include "jsp_mods_glue.h"
#include "ps3emu/fs_vfile.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
#include <dirent.h>
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
// A symbolic link, a junction (mount point) or a Linux-style link: the reparse tags that lead somewhere else. Other reparse points (cloud placeholders,
// deduplicated files) are ordinary files for this check and stay accepted.
bool is_link_tag(DWORD tag) { return tag == 0xA000000CUL || tag == 0xA0000003UL || tag == 0xA000001DUL; }
// R6: true when this path (one component) is itself a symbolic link or junction. The final-path comparison alone is not enough: under Wine
// GetFinalPathNameByHandleW resolves a Linux symlink without showing it (the R6 run followed a linked mod folder), so each component is also
// looked at without following it: the reparse tag of a directory listing entry (FindFirstFileW does not follow) and of a handle opened with
// FILE_FLAG_OPEN_REPARSE_POINT.
bool is_link_component(const std::wstring& p)
{
    WIN32_FIND_DATAW d;
    HANDLE f = FindFirstFileW(p.c_str(), &d);
    if (f != INVALID_HANDLE_VALUE) {
        FindClose(f);
        if ((d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && is_link_tag(d.dwReserved0)) return true;
    }
    HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO ti;
    const bool link = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof ti) && (ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                      is_link_tag(ti.ReparseTag);
    CloseHandle(h);
    return link;
}
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
        for (size_t e = 0; e != std::wstring::npos;) {   // R6: every component below the mods folder, none of them a link
            e = r.find(L'\\', e);
            const std::wstring part = root_ + L"\\" + r.substr(0, e);
            if (is_link_component(part)) { why = "it is reached through a link or junction (not followed)"; return false; }
            if (e != std::wstring::npos) ++e;
        }
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
    bool gameplay = false;                         // a gameplay mod serves an override here (a differing copy refuses the set)
    std::unique_ptr<std::atomic<bool>[]> logged;   // one "first read in override segment" line per override
    mutable std::atomic<uint64_t> short_reads{ 0 };   // logged at 1, 10, 100, ... (one line, with the count)
};
struct Verified { std::string why; bool logged = false; uint64_t size = UINT64_MAX; };   // why: "" = serves, else why the stock file is read; size: the copy's size then (UINT64_MAX: unknown)
struct State {
    ModsRoot root;
    bool root_ok = false;
    std::map<std::string, ModFile> files;   // "<id>/<path>" -> the open file (a failed open is kept with ok = false)
    ModSet set;
    std::map<std::string, Served> served;   // by archive_key (the basename; case folded on Windows)
    std::mutex mu;
    std::map<std::string, Verified> verified;   // by path_key of the host path; filled at start for every copy found, later on first open
};
State* g_st = nullptr;                                // set once by jsp_mods_init, never freed (fds may outlive any teardown)
std::atomic<int> g_kind{ (int)DigestKind::None };   // the set's DigestKind (Refused also after a gameplay copy differs)
std::atomic<int> g_copy_mismatch{ 0 };               // a gameplay archive copy first opened after start differs (jsp_mods_copy_mismatch)

#ifdef _WIN32
bool g_fold_case = true;    // the file system ignores case: so does the archive match
#else
bool g_fold_case = false;   // (the host test turns it on to drive the Windows match)
#endif

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
    std::string mod_why(const std::string& rel) override
    {
        const auto it = why_.find(rel);
        return it == why_.end() ? std::string() : it->second;
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
    std::map<std::string, std::string> why_;   // rel -> why it could not be opened (not "not found")
    const ModFile* file(const std::string& rel)
    {
        auto it = st_.files.find(rel);
        if (it == st_.files.end()) {
            ModFile f;
            std::string why = "the mods folder is not open";
            if (!valid_rel_path(rel)) why = "not a valid mod path";
            else if (st_.root_ok && st_.root.open_file(rel, f, why)) why.clear();
            if (!why.empty() && why != "not found") { logl("[mods] " + rel + ": " + why); why_[rel] = why; }
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
std::string check_copy(const Served& s, const char* host_path, uint64_t* size_out = nullptr)
{
    FILE* f = std::fopen(host_path, "rb");   // a second, read-only handle of our own, closed before open() returns
    if (!f) return "cannot open it";
    uint64_t size = 0;
    if (size_out) *size_out = size64(f, size) ? size : UINT64_MAX;
    std::string why = size64(f, size) ? verify_copy(s.check, size, [f](uint64_t off, void* d, size_t n) { return file_read(f, off, d, n); })
                                      : "cannot read its size";
    std::fclose(f);
    return why;
}

ps3_fs_vfile* v_open(const char* guest_path, const char* host_path)
{
    State* st = g_st;
    if (!st || !host_path) return nullptr;
    const auto it = st->served.find(archive_key(host_path, g_fold_case));
    if (it == st->served.end()) return nullptr;
    const Served& s = it->second;
    const char* name = s.plan.archive.c_str();
    std::string why;
    {
        std::lock_guard<std::mutex> lk(st->mu);   // never held while taking another fd's stream lock: only our own FILE is used in here
        const std::string key = path_key(host_path, g_fold_case);
        auto v = st->verified.find(key);
        const bool late = v == st->verified.end();   // a copy the start-up check did not find
        if (late) { Verified r; r.why = check_copy(s, host_path, &r.size); v = st->verified.emplace(key, r).first; }
        else if (!v->second.why.empty()) {   // a copy that differed: checked again when its size changed since (an install still being written)
            FILE* f = std::fopen(host_path, "rb");
            uint64_t now = UINT64_MAX;
            if (f) { if (!size64(f, now)) now = UINT64_MAX; std::fclose(f); }
            if (now != v->second.size) { Verified r; r.why = check_copy(s, host_path, &r.size); v->second = r; }
        }
        why = v->second.why;
        if (!v->second.logged) {
            v->second.logged = true;
            if (why.empty()) logl(fmt("[mods] serve %s (%s): checked, %llu override(s), virtual size %llu B", name, guest_path ? guest_path : "?",
                                      (unsigned long long)s.plan.overrides.size(), (unsigned long long)s.plan.virt_size));
            else logl(fmt("[mods] serve %s (%s): NOT served, the stock file is read: this copy is not the one the mods were planned on (%s)",
                          name, guest_path ? guest_path : "?", why.c_str()));
        }
        if (late && !why.empty() && s.gameplay) {   // one line per such path (the result is kept)
            g_copy_mismatch.store(1);
            // The start-time decision (the build word, the game's online) could not see this copy: this process now runs stock bytes where
            // a gameplay mod was planned. Online peers would run different content; the session layer reads jsp_mods_copy_mismatch().
            g_kind.store((int)DigestKind::Refused);
            logl(fmt("[mods] WARNING: %s (%s) is a copy the start-up check did not find and it differs from the planned copy: this session no "
                     "longer runs the mod set as listed (online content mismatch)", name, host_path));
        }
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
    if (got < n) {   // one line per decade of the count, so a file rewritten under the game cannot flood the log
        const uint64_t c = s.short_reads.fetch_add(1) + 1;
        uint64_t d = 1;
        while (d < c && d <= UINT64_MAX / 10) d *= 10;
        if (d == c) logl(fmt("[mods] serve %s: short read at %llu (%llu of %llu B); %llu short read(s) so far", s.plan.archive.c_str(), (unsigned long long)pos,
                             (unsigned long long)got, (unsigned long long)n, (unsigned long long)c));
    }
    return got;
}
void v_close(ps3_fs_vfile* vf) { delete reinterpret_cast<View*>(vf); }
const ps3_fs_vfile_ops k_ops = { v_open, v_size, v_read, v_close };

// The folders under <hdd0>/game (each may hold an install copy of the archives). Narrow names, as ppu_fs opens them.
std::vector<std::string> game_folders(const std::string& hdd0)
{
    std::vector<std::string> v;
#ifdef _WIN32
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA((hdd0 + "\\game\\*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && std::strcmp(d.cFileName, ".") != 0 && std::strcmp(d.cFileName, "..") != 0) v.push_back(d.cFileName);
    } while (FindNextFileA(h, &d));
    FindClose(h);
#else
    DIR* dir = opendir((hdd0 + "/game").c_str());
    if (!dir) return v;
    while (const dirent* e = readdir(dir))
        if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) v.push_back(e->d_name);
    closedir(dir);
#endif
    std::sort(v.begin(), v.end());
    return v;
}

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

std::vector<RosterFighter> g_roster;   // ENHANCEMENT (roster): the applied mods' added fighters, for jsp_roster.cpp

}  // namespace

/* vfs_root: the folder holding PS3_GAME (main.cpp's ppu_vfs_root); eboot: the ELF main() loads (its SHA-256 is the digest's EBOOT id);
 * title_id: PARAM.SFO's (manifests name the game versions they are for). Call once, before the guest starts. */
extern "C" void jsp_mods_init(const char* vfs_root, const char* eboot, const char* title_id)
{
    g_copy_mismatch.store(0);
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
    std::string disc = vfs_root && *vfs_root ? vfs_root : ".";
    while (!disc.empty() && (disc.back() == '/' || disc.back() == '\\')) disc.pop_back();
    const std::string disc_dir = disc + "/PS3_GAME/USRDIR/data/PS3/";
    {
        LoadIo io(*st, disc_dir);
        st->set = load_mod_set(io, text, opt);
    }
    for (const std::string& l : st->set.log) logl(l);
    // Keep only the files a plan serves; mod.json and refused mods' files are closed now.
    std::map<std::string, bool> used;
    for (const auto& kv : st->set.plans)
        for (const Segment& sg : kv.second.segs) if (sg.kind == Segment::ModFile) used[sg.rel] = true;
    for (auto& kv : st->files) if (!used.count(kv.first)) ModsRoot::close(kv.second);
    for (const auto& kv : st->set.plans) {
        Served& s = st->served[archive_key(kv.first.c_str(), g_fold_case)];
        s.plan = kv.second;
        s.check = copy_check(st->set, kv.second);
        s.gameplay = gameplay_archive(st->set, kv.first);
        s.logged.reset(new std::atomic<bool>[kv.second.overrides.size() + 1]());
    }
    // Every copy the game can open, checked now, before the digest is published: hdd0 is PS3_HDD0_ROOT, else the disc root (ppu_fs host_path).
    bool gameplay_differs = false;
    if (!st->served.empty()) {
        const char* h = std::getenv("PS3_HDD0_ROOT");
        std::string hdd0 = h && *h ? h : disc;
        while (!hdd0.empty() && (hdd0.back() == '/' || hdd0.back() == '\\')) hdd0.pop_back();
        const std::vector<std::string> folders = game_folders(hdd0);
        for (const auto& kv : st->served) {
            const Served& s = kv.second;
            int found = 0, ok = 0;
            for (const std::string& path : copy_paths(disc_dir, hdd0, folders, s.plan.archive)) {
                FILE* f = std::fopen(path.c_str(), "rb");
                if (!f) continue;   // no copy here
                std::fclose(f);
                ++found;
                Verified r;
                r.why = check_copy(s, path.c_str(), &r.size);
                const std::string why = r.why;
                st->verified[path_key(path.c_str(), g_fold_case)] = r;
                if (why.empty()) { ++ok; continue; }
                logl(fmt("[mods] check %s: the copy %s is not the one the mods were planned on (%s): it is read stock%s", s.plan.archive.c_str(), path.c_str(),
                         why.c_str(), s.gameplay ? ", and a gameplay mod changes this archive" : ""));
                if (s.gameplay) gameplay_differs = true;
            }
            logl(fmt("[mods] check %s: %d of %d cop%s the game can open match the plan", s.plan.archive.c_str(), ok, found, found == 1 ? "y" : "ies"));
        }
    }
    const DigestKind kind = after_copy_check(st->set.digest_kind, gameplay_differs);
    if (kind != st->set.digest_kind) {
        logl("[mods] set: refused: a copy of an archive a gameplay mod changes differs from the planned copy (see the check lines); the game's "
             "online and rollback are off, local play reads that copy stock");
        st->set.digest_kind = kind;
    }
    g_kind.store((int)st->set.digest_kind);
    g_roster = st->set.roster;
    g_st = st;
    if (!st->served.empty()) {
        __atomic_store_n(&g_ps3_fs_vfile_ops, &k_ops, __ATOMIC_RELEASE);
        std::string names;
        for (const auto& kv : st->served) names += (names.empty() ? "" : ", ") + kv.first;
        logl(fmt("[mods] serving %llu archive(s): %s (eboot %s)", (unsigned long long)st->served.size(), names.c_str(), opt.eboot_id.c_str()));
    }
    if (!game_online_allowed(st->set.digest_kind)) jsp_online_refuse_for_mods();
}

/* 1 when a copy of an archive a gameplay mod changes was first opened after start and differs from the planned copy: this process reads it
 * stock, so a running online session no longer runs the set the peers agreed on (the session layer may end it as a content mismatch). */
extern "C" int jsp_mods_copy_mismatch(void) { return g_copy_mismatch.load(); }

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

/* ENHANCEMENT (roster, jsp_roster.cpp). The applied roster mods' fighters, load order; returns the count (0 = none), at most cap. */
extern "C" int jsp_mods_roster_fighters(int* id, int* logo_as, int* owned, int cap)
{
    int n = 0;
    for (const RosterFighter& f : g_roster) {
        if (n >= cap) break;
        id[n] = f.id; logo_as[n] = f.logo_as; owned[n] = f.owned ? 1 : 0;
        ++n;
    }
    return n;
}

/* ENHANCEMENT (roster card_as, jsp_roster.cpp). The added ids that borrow a stock card: id[i] shows card_as[i]'s texture. Returns the count. */
extern "C" int jsp_mods_roster_cards(int* id, int* card_as, int cap)
{
    int n = 0;
    for (const RosterFighter& f : g_roster) {
        if (f.card_as < 0) continue;
        if (n >= cap) break;
        id[n] = f.id; card_as[n] = f.card_as;
        ++n;
    }
    return n;
}

/* The final character list (stock ids plus the added ones, no terminator); returns its length, or 0 when it does not fit cap. */
extern "C" int jsp_mods_roster_list(uint32_t* out, int cap)
{
    const std::vector<uint32_t> l = build_roster_list(g_roster);
    if ((int)l.size() > cap) return 0;
    for (size_t i = 0; i < l.size(); ++i) out[i] = l[i];
    return (int)l.size();
}

/* The stock character list (the 52 ids, no terminator), for the guest side's comparison; returns its length, or 0 when it does not fit cap. */
extern "C" int jsp_mods_roster_stock(uint32_t* out, int cap)
{
    const int n = (int)(sizeof kStockRoster / sizeof kStockRoster[0]);
    if (n > cap) return 0;
    for (int i = 0; i < n; ++i) out[i] = kStockRoster[i];
    return n;
}

/* The roster could not be put into the guest (not US 1.00, or the guest list is not the stock one): the set no longer runs as listed, so it
 * is Refused (no rollback, no game online), the same as a gameplay mod that was requested but not applied. */
extern "C" void jsp_mods_roster_failed(const char* why)
{
    g_kind.store((int)DigestKind::Refused);
    logl(std::string("[mods] roster not applied (") + (why ? why : "?") + "): gameplay mods requested but not applied on this build: online refused");
    jsp_online_refuse_for_mods();
}
