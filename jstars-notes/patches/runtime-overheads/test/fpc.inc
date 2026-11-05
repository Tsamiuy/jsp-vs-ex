/* SPU_FP_CACHE (default on; SPU_FP_CACHE=0 = hash every time): the dispatch
 * paths fingerprint the WHOLE job image byte by byte on every dispatch to find
 * its lifted code. J-Stars dispatches ~2,200 SPURS jobs a second, mostly the
 * same few images at the same addresses, and the byte-wise FNV was 20-36% of
 * the SPU job threads' code time (jstars-notes CPU_PROFILE.md §10). Each
 * thread keeps the bytes it last hashed per (address, size); when the image
 * still matches them (memcmp, vectorised) the stored fingerprint is returned.
 * Same bytes give the same FNV, so the answer is always what a full hash would
 * give; any byte that differs takes the full hash again. Images over 256 KB are
 * always hashed. */
#ifdef _WIN32
#define SPU_FP_TLS __declspec(thread)
#else
#define SPU_FP_TLS _Thread_local
#endif
static uint64_t spu_workload_fingerprint_cached(const void* data, size_t n)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("SPU_FP_CACHE"); on = (e && *e == '0') ? 0 : 1; }
    if (!on || n > (256u << 10)) return spu_workload_fingerprint(data, n);
    enum { NE = 16 };
    static SPU_FP_TLS const void* t_ptr[NE];
    static SPU_FP_TLS size_t      t_len[NE];
    static SPU_FP_TLS uint64_t    t_fp[NE];
    static SPU_FP_TLS uint8_t*    t_copy[NE];
    static SPU_FP_TLS size_t      t_cap[NE];
    static SPU_FP_TLS unsigned    t_next;
    int slot = -1;
    for (int i = 0; i < NE; i++)
        if (t_ptr[i] == data && t_len[i] == n) { slot = i; break; }
    if (slot >= 0 && memcmp(t_copy[slot], data, n) == 0) return t_fp[slot];
    const uint64_t fp = spu_workload_fingerprint(data, n);
    if (slot < 0) slot = (int)(t_next++ % NE);
    if (t_cap[slot] < n) {
        uint8_t* c = (uint8_t*)realloc(t_copy[slot], n);
        if (!c) { t_ptr[slot] = NULL; t_len[slot] = 0; return fp; }
        t_copy[slot] = c; t_cap[slot] = n;
    }
    memcpy(t_copy[slot], data, n);
    t_ptr[slot] = data; t_len[slot] = n; t_fp[slot] = fp;
    return fp;
}

