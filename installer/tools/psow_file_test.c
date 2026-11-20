/* Host test of libs/video/psow_file.h (toolkit patch 2069: pipelines.bin with 64-bit offsets, no repeated records, compaction).
 * No game, no GPU. POSIX only (a sparse file, mkdtemp, a symlink to /dev/full).
 *   cc -O1 -Wall -I<patched tree>/libs/video installer/tools/psow_file_test.c -o psow_file_test && ./psow_file_test
 * The big files are sparse: a file of more than 2^31 bytes whose payload is a hole costs a few KB of disk; the loader seeks over
 * payloads, and compaction copies only the first record of each key. Proves: (a) a file over 2^31 bytes is read, logged and
 * compacted, (b) a key already in the file is never appended, even across runs, (c) compaction writes a temporary file and
 * replaces the old one in one step, keeping the old file on every failure, (d) offsets are 64-bit. */
#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <ftw.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

static char g_log[1 << 16]; static size_t g_loglen;
static void test_log(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    g_loglen += (size_t)vsnprintf(g_log + g_loglen, sizeof g_log - g_loglen - 1, fmt, ap);
    va_end(ap);
    if (g_loglen < sizeof g_log - 2) g_log[g_loglen++] = '\n';
    g_log[g_loglen] = 0;
}
#define PSW_LOG(...) test_log(__VA_ARGS__)
static int g_replace_fail; static long long g_replace_tmp_size, g_replace_dst_size = -1;
static int test_replace(const char* tmp, const char* dst);
#define PSW_REPLACE(tmp, dst) test_replace((tmp), (dst))
#include "psow_file.h"

static long long fsize(const char* p) { struct stat st; return stat(p, &st) ? -1 : (long long)st.st_size; }
static int exists(const char* p) { struct stat st; return lstat(p, &st) == 0; }
static int test_replace(const char* tmp, const char* dst)
{
    g_replace_tmp_size = fsize(tmp); g_replace_dst_size = fsize(dst);   /* what a reader sees at the moment of the swap */
    if (g_replace_fail) { errno = EIO; return -1; }
    return rename(tmp, dst);
}

static int rm_cb(const char* p, const struct stat* st, int fl, struct FTW* w) { (void)st; (void)fl; (void)w; return remove(p); }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define HDR 32u            /* the test's fixed record head: 24 bytes of PswRecHead and 8 of padding */
#define MAXB (1u << 20)
#define STAMP 0x1234abcdULL
#define HEADSZ ((long long)sizeof(PswFileHead))
static char dir[256];
static char* path(const char* n) { static char b[8][400]; static int i; char* r = b[i++ & 7]; snprintf(r, 400, "%s/%s", dir, n); return r; }

/* writers independent of psow_file.h's reader */
static void put_head(FILE* f, uint64_t stamp) { PswFileHead h = { PSW_FILE_MAGIC, PSW_FORMAT, stamp, HDR, 0 }; fwrite(&h, sizeof h, 1, f); }
static void put_rec(FILE* f, uint64_t key, uint32_t vs, uint32_t ps, int sparse)
{
    unsigned char head[HDR]; memset(head, 0, sizeof head);
    PswRecHead h = { PSW_REC_MAGIC, vs, ps, 0, key }; memcpy(head, &h, sizeof h);
    fwrite(head, sizeof head, 1, f);
    if (!sparse) { unsigned char b[64]; for (uint32_t i = 0; i < vs + ps; i += sizeof b) { memset(b, (int)(key & 0xff), sizeof b); fwrite(b, 1, (vs + ps - i) < sizeof b ? (vs + ps - i) : sizeof b, f); } }
    else {   /* a hole: seek over the payload, and write its last byte so the file really extends */
        psw_seek(f, (psw_off)(vs + ps) - 1, SEEK_CUR); fputc(0, f);
    }
}
static FILE* mk(const char* n) { FILE* f = fopen(path(n), "wb"); if (!f) { printf("cannot create %s\n", path(n)); exit(2); } return f; }
/* an independent count of whole records in a file with a head */
static int count_recs(const char* p, uint64_t* keys, int max)
{
    FILE* f = fopen(p, "rb"); if (!f) return -1;
    PswFileHead fh; int n = 0;
    if (fread(&fh, sizeof fh, 1, f) != 1 || fh.magic != PSW_FILE_MAGIC) { fclose(f); return -2; }
    PswRecHead h; unsigned char pad[HDR];
    while (fread(&h, sizeof h, 1, f) == 1 && h.magic == PSW_REC_MAGIC) {
        if (fread(pad, HDR - sizeof h, 1, f) != 1) break;
        if (fseeko(f, (off_t)(h.vs_len + h.ps_len), SEEK_CUR) != 0) break;
        if (n < max) keys[n] = h.key;
        n++;
    }
    fclose(f); return n;
}
static void fresh(PswCache* c, const char* n, uint32_t maxrec, uint64_t stamp) { g_loglen = 0; g_log[0] = 0; CHECK(psw_init(c, path(n), HDR, MAXB, maxrec, stamp)); }
static void rec_head(unsigned char* head, uint64_t key, uint32_t vs, uint32_t ps) { memset(head, 0, HDR); PswRecHead h = { PSW_REC_MAGIC, vs, ps, 0, key }; memcpy(head, &h, sizeof h); }
static void note(PswCache* c, uint64_t key, int expect)
{
    unsigned char head[HDR], body[16]; memset(body, (int)(key & 0xff), sizeof body); rec_head(head, key, 8, 8);
    const int r = psw_note(c, head, body, body);
    if (r != expect) { printf("FAIL note key %llu: got %d want %d\n", (unsigned long long)key, r, expect); fails++; }
}
static size_t occurrences(const char* hay, const char* needle) { size_t n = 0; for (const char* p = hay; (p = strstr(p, needle)); p += strlen(needle)) n++; return n; }
static void freec(PswCache* c) { psw_free(c); }
/* the test's record check: the payload is the key's low byte throughout (what put_rec and note write) */
static int test_verify(const unsigned char* rec, uint32_t hdr, uint32_t vs, uint32_t ps)
{
    PswRecHead h; memcpy(&h, rec, sizeof h);
    for (uint32_t i = 0; i < vs + ps; i++) if (rec[hdr + i] != (unsigned char)(h.key & 0xff)) return 0;
    return 1;
}

int main(void)
{
    const char* tmpd = getenv("TMPDIR"); if (!tmpd || !*tmpd) tmpd = "/tmp";
    snprintf(dir, sizeof dir, "%s/psow_test_XXXXXX", tmpd);
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 2; }
    PswCache c;

    /* (d) offsets */
    CHECK(sizeof(psw_off) == 8);

    /* (a) 1: a file of 1100 x ~2 MiB = 2.2 GB (sparse) that is a clean list of distinct keys: read as it is, no rewrite */
    {
        FILE* f = mk("big_clean.bin"); put_head(f, STAMP);
        for (uint64_t k = 1; k <= 1100; k++) put_rec(f, k, MAXB, MAXB, 1);
        fclose(f);
        const long long sz = fsize(path("big_clean.bin"));
        CHECK(sz > 2147483648LL);
        fresh(&c, "big_clean.bin", 2048, STAMP);
        CHECK(psw_prepare(&c) == 1100);
        CHECK(c.size == sz);
        CHECK(!c.dead);
        char want[64]; snprintf(want, sizeof want, "%lld bytes", sz);
        CHECK(strstr(g_log, want) != NULL);                       /* the size is in the log, correct above 2^31 */
        CHECK(strstr(g_log, "head matches this build") != NULL);
        CHECK(strstr(g_log, "1100 records (1100 distinct, 0 repeated") != NULL);
        CHECK(strstr(g_log, "rewritten") == NULL);
        CHECK(fsize(path("big_clean.bin")) == sz);                // untouched
        freec(&c);
    }
    /* (a) 2: the real failure: a first record, then the same key appended over and over (a run whose load failed re-appends
     * every pipeline), 2.2 GB in all. It is read, logged, compacted to two records, and a later start finds the small file. */
    {
        FILE* f = mk("big_dup.bin");   /* an older build's file: no head */
        put_rec(f, 1, 100, 200, 0);
        for (int i = 0; i < 1100; i++) put_rec(f, 2, MAXB, MAXB, 1);
        fclose(f);
        const long long sz = fsize(path("big_dup.bin"));
        CHECK(sz > 2147483648LL);
        fresh(&c, "big_dup.bin", 2048, STAMP);
        const int n = psw_prepare(&c);
        CHECK(n == 2);
        char want[64]; snprintf(want, sizeof want, "%lld bytes", sz);
        CHECK(strstr(g_log, want) != NULL);
        CHECK(strstr(g_log, "no head (older build)") != NULL);
        CHECK(strstr(g_log, "1101 records (2 distinct, 1099 repeated") != NULL);
        CHECK(strstr(g_log, "rewritten to 2 records") != NULL);
        const long long want_sz = HEADSZ + (HDR + 300) + (HDR + 2 * MAXB);
        CHECK(fsize(path("big_dup.bin")) == want_sz);
        CHECK(c.size == want_sz);
        uint64_t keys[4]; CHECK(count_recs(path("big_dup.bin"), keys, 4) == 2 && keys[0] == 1 && keys[1] == 2);
        CHECK(!exists(path("big_dup.bin.tmp")));
        freec(&c);
        fresh(&c, "big_dup.bin", 2048, STAMP);
        CHECK(psw_prepare(&c) == 2);
        CHECK(strstr(g_log, "rewritten") == NULL);
        freec(&c);
    }
    /* the warm-up reads each record by its offset (no whole-file image) */
    {
        fresh(&c, "big_dup.bin", 2048, STAMP); CHECK(psw_prepare(&c) == 2); CHECK(c.n_offs == 2);
        FILE* f = fopen(path("big_dup.bin"), "rb"); PswRecHead h;
        const size_t cap = HDR + 2u * MAXB; unsigned char* buf = (unsigned char*)malloc(cap);
        CHECK(psw_read_record(&c, f, 0, buf, cap, &h) && h.key == 1 && h.vs_len == 100 && h.ps_len == 200);
        CHECK(psw_read_record(&c, f, 1, buf, cap, &h) && h.key == 2 && h.vs_len == MAXB && h.ps_len == MAXB);
        CHECK(!psw_read_record(&c, f, 2, buf, cap, &h));            /* past the end of the list */
        CHECK(!psw_read_record(&c, f, 1, buf, HDR + 10, &h));       /* a buffer that is too small is refused, not overrun */
        free(buf); fclose(f); freec(&c);
    }

    /* (b) a key already in the file is never appended, within a run and across runs */
    {
        fresh(&c, "w.bin", 8, STAMP);
        CHECK(psw_prepare(&c) == 0); CHECK(strstr(g_log, "no file yet") != NULL);
        for (uint64_t k = 1; k <= 5; k++) note(&c, k, 0);
        for (uint64_t k = 1; k <= 5; k++) note(&c, k, 1);
        fclose(c.out); c.out = NULL;
        const long long one_run = fsize(path("w.bin"));
        CHECK(one_run == HEADSZ + 5 * (HDR + 16));
        freec(&c);
        fresh(&c, "w.bin", 8, STAMP);                              /* the next start */
        CHECK(psw_prepare(&c) == 5);
        for (uint64_t k = 1; k <= 5; k++) note(&c, k, 1);          /* all five again: nothing written */
        note(&c, 6, 0);
        fclose(c.out); c.out = NULL; freec(&c);
        CHECK(fsize(path("w.bin")) == HEADSZ + 6 * (HDR + 16));
        fresh(&c, "w.bin", 8, STAMP);                              /* a third start: capacity 8 */
        CHECK(psw_prepare(&c) == 6);
        note(&c, 7, 0); note(&c, 8, 0); note(&c, 9, 2); note(&c, 10, 2); note(&c, 3, 1);
        CHECK(occurrences(g_log, "full (8 records)") == 1);        /* said once */
        fclose(c.out); c.out = NULL; freec(&c);
        uint64_t keys[16]; CHECK(count_recs(path("w.bin"), keys, 16) == 8);
        /* the bug: a run whose load failed had an empty noted-set and appended everything again; with this code a file that was
         * grown that way is compacted at the next start, whatever made it */
        FILE* f = fopen(path("w.bin"), "ab");
        for (uint64_t k = 1; k <= 8; k++) put_rec(f, k, 8, 8, 0);
        fclose(f);
        fresh(&c, "w.bin", 8, STAMP);
        CHECK(psw_prepare(&c) == 8); CHECK(strstr(g_log, "8 repeated") != NULL); CHECK(count_recs(path("w.bin"), keys, 16) == 8);
        freec(&c);
    }

    /* (c) compaction writes path.tmp and replaces the old file in one step; the old file stays on every failure */
    {
        FILE* f = mk("c.bin"); put_head(f, STAMP);
        for (int i = 0; i < 3; i++) for (uint64_t k = 1; k <= 3; k++) put_rec(f, k, 8, 8, 0);
        fclose(f);
        const long long orig = fsize(path("c.bin"));
        unsigned char* before = (unsigned char*)malloc((size_t)orig); f = fopen(path("c.bin"), "rb"); CHECK(fread(before, 1, (size_t)orig, f) == (size_t)orig); fclose(f);
        unsigned char* after = (unsigned char*)malloc((size_t)orig);
#define UNCHANGED() do { f = fopen(path("c.bin"), "rb"); CHECK(f != NULL && fsize(path("c.bin")) == orig && fread(after, 1, (size_t)orig, f) == (size_t)orig && !memcmp(before, after, (size_t)orig)); if (f) fclose(f); } while (0)

        /* the temporary file cannot be created (a folder has its name) */
        CHECK(mkdir(path("c.bin.tmp"), 0700) == 0);
        fresh(&c, "c.bin", 16, STAMP); CHECK(psw_prepare(&c) == -1); CHECK(c.dead); CHECK(strstr(g_log, "the old file is kept") != NULL); CHECK(strstr(g_log, "warm-up is off") != NULL);
        UNCHANGED(); note(&c, 99, -1); UNCHANGED(); freec(&c);
        rmdir(path("c.bin.tmp"));
        /* a temporary name that cannot be deleted is reported */
        CHECK(mkdir(path("c.bin.tmp"), 0700) == 0); { FILE* k = mk("c.bin.tmp/inner"); fclose(k); }
        fresh(&c, "c.bin", 16, STAMP); CHECK(psw_prepare(&c) == -1); CHECK(strstr(g_log, "could not be removed") != NULL); UNCHANGED(); freec(&c);
        unlink(path("c.bin.tmp/inner")); rmdir(path("c.bin.tmp"));
        /* the temporary file fills up: written to /dev/full */
        CHECK(symlink("/dev/full", path("c.bin.tmp")) == 0);
        fresh(&c, "c.bin", 16, STAMP); CHECK(psw_prepare(&c) == -1); CHECK(strstr(g_log, "write or sync failed") != NULL);
        UNCHANGED(); CHECK(!exists(path("c.bin.tmp"))); freec(&c);
        /* the replacement itself fails */
        g_replace_fail = 1;
        fresh(&c, "c.bin", 16, STAMP); CHECK(psw_prepare(&c) == -1); CHECK(strstr(g_log, "cannot replace the old file") != NULL);
        CHECK(g_replace_tmp_size == HEADSZ + 3 * (HDR + 16));          /* the temporary file was complete at the swap */
        UNCHANGED(); CHECK(!exists(path("c.bin.tmp"))); freec(&c);
        g_replace_fail = 0;
        /* success: at the moment of the swap the old file was still whole and the temporary one complete */
        fresh(&c, "c.bin", 16, STAMP); CHECK(psw_prepare(&c) == 3);
        CHECK(g_replace_dst_size == orig); CHECK(g_replace_tmp_size == HEADSZ + 3 * (HDR + 16));
        CHECK(fsize(path("c.bin")) == HEADSZ + 3 * (HDR + 16)); CHECK(!exists(path("c.bin.tmp")));
        uint64_t keys[8]; CHECK(count_recs(path("c.bin"), keys, 8) == 3 && keys[0] == 1 && keys[1] == 2 && keys[2] == 3);
        freec(&c); free(before); free(after);
    }

    /* a damaged tail is cut (it would hide whatever is appended after it), over-capacity records are dropped */
    {
        FILE* f = mk("t.bin"); put_head(f, STAMP);
        for (uint64_t k = 1; k <= 6; k++) put_rec(f, k, 8, 8, 0);
        put_rec(f, 7, 8, 8, 0); fflush(f); CHECK(ftruncate(fileno(f), (off_t)(HEADSZ + 7 * (HDR + 16) - 5)) == 0);   /* torn last record */
        fclose(f);
        fresh(&c, "t.bin", 4, STAMP);
        CHECK(psw_prepare(&c) == 4);
        CHECK(strstr(g_log, "2 beyond the capacity of 4") != NULL);
        CHECK(strstr(g_log, "bytes after the last whole record") != NULL);
        note(&c, 50, 2);                                           /* full */
        freec(&c);
        uint64_t keys[8]; CHECK(count_recs(path("t.bin"), keys, 8) == 4 && keys[3] == 4);
        FILE* g = mk("t2.bin"); put_head(g, STAMP); for (uint64_t k = 1; k <= 3; k++) put_rec(g, k, 8, 8, 0); put_rec(g, 4, 8, 8, 0);
        fflush(g); CHECK(ftruncate(fileno(g), (off_t)(HEADSZ + 4 * (HDR + 16) - 5)) == 0); fclose(g);
        fresh(&c, "t2.bin", 16, STAMP); CHECK(psw_prepare(&c) == 3); CHECK(strstr(g_log, "rewritten to 3 records") != NULL);
        note(&c, 77, 0); fclose(c.out); c.out = NULL; freec(&c);
        CHECK(count_recs(path("t2.bin"), keys, 8) == 4 && keys[3] == 77);   /* the record after the cut tail is reachable */
    }
    /* a torn record in the MIDDLE of an older file (the old code appended after a crash-torn record): it parses as a record whose body
     * runs into the next records. Its bytes do not hash to its key, so it is not kept (and not built), and the key is free again. */
    {
        FILE* f = mk("m.bin");                                     /* no head: written before patch 2069 */
        put_rec(f, 11, 64, 64, 0);
        { unsigned char head[HDR]; rec_head(head, 22, 64, 64); fwrite(head, HDR, 1, f); for (int i = 0; i < 40; i++) fputc(0x16, f); }   /* torn: 40 of 128 */
        put_rec(f, 33, 64, 64, 0); put_rec(f, 44, 64, 64, 0); put_rec(f, 55, 64, 64, 0);
        fclose(f);
        fresh(&c, "m.bin", 16, STAMP); c.verify = test_verify;
        CHECK(psw_prepare(&c) == 1);
        CHECK(strstr(g_log, "did not hash to their key") != NULL);
        CHECK(c.n_offs == 1);
        note(&c, 22, 0);                                           /* the real pipeline of key 22 can be noted again */
        fclose(c.out); c.out = NULL; freec(&c);
        uint64_t keys[8]; CHECK(count_recs(path("m.bin"), keys, 8) == 2 && keys[0] == 11 && keys[1] == 22);
        fresh(&c, "m.bin", 16, STAMP); c.verify = test_verify; CHECK(psw_prepare(&c) == 2);
        FILE* r = fopen(path("m.bin"), "rb"); PswRecHead h; unsigned char buf[HDR + 256];
        CHECK(psw_read_record(&c, r, 0, buf, sizeof buf, &h) && psw_read_record(&c, r, 1, buf, sizeof buf, &h) && h.key == 22);
        fclose(r);
        /* bit rot in a file that is otherwise whole: the scan does not read bodies, the warm-up's read does and refuses it */
        FILE* w = fopen(path("m.bin"), "r+b"); psw_seek(w, (psw_off)sizeof(PswFileHead) + HDR + 5, SEEK_SET); fputc(0x00, w); fclose(w);
        r = fopen(path("m.bin"), "rb"); CHECK(!psw_read_record(&c, r, 0, buf, sizeof buf, &h)); CHECK(psw_read_record(&c, r, 1, buf, sizeof buf, &h)); fclose(r);
        freec(&c);
        /* an I/O error on a record head is an error, not the end of the list (a write-only handle cannot be read) */
        FILE* wo = fopen(path("m.bin"), "ab"); PswRecHead hh; psw_off end;
        fresh(&c, "m.bin", 16, STAMP);
        CHECK(psw_rec_at(&c, wo, (psw_off)sizeof(PswFileHead), fsize(path("m.bin")), &hh, &end) == -1);
        fclose(wo); freec(&c);
    }
    /* a head from another build empties the file; a file with no head keeps its records and gets one */
    {
        FILE* f = mk("s.bin"); put_head(f, STAMP + 1); put_rec(f, 1, 8, 8, 0); put_rec(f, 2, 8, 8, 0); fclose(f);
        fresh(&c, "s.bin", 16, STAMP); CHECK(psw_prepare(&c) == 0);
        CHECK(strstr(g_log, "head from another build") != NULL); CHECK(strstr(g_log, "emptied") != NULL);
        CHECK(fsize(path("s.bin")) == HEADSZ);
        note(&c, 1, 0);                                            /* a key of the old build is new now */
        fclose(c.out); c.out = NULL; freec(&c);
        uint64_t keys[4]; CHECK(count_recs(path("s.bin"), keys, 4) == 1);
        f = mk("n.bin"); put_rec(f, 1, 8, 8, 0); put_rec(f, 2, 8, 8, 0); fclose(f);
        fresh(&c, "n.bin", 16, STAMP); CHECK(psw_prepare(&c) == 2); CHECK(count_recs(path("n.bin"), keys, 4) == 2); freec(&c);
        f = mk("z.bin"); fclose(f);                                // an empty file: the head is written with the first record
        fresh(&c, "z.bin", 16, STAMP); CHECK(psw_prepare(&c) == 0); note(&c, 5, 0); fclose(c.out); c.out = NULL; freec(&c);
        CHECK(count_recs(path("z.bin"), keys, 4) == 1);
    }

    nftw(dir, rm_cb, 8, FTW_DEPTH | FTW_PHYS);   /* the test folder and its files, symlinks not followed */
    printf(fails ? "psow_file_test: %d FAILED\n" : "psow_file_test: all passed\n", fails);
    return fails ? 1 : 0;
}
