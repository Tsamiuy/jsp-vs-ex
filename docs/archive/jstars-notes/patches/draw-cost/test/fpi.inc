#define FPI_SETS 512u
#define FPI_WAYS 4u
typedef struct {
    const u8* p;                 /* program start (vm_base + offset) */
    u32 nblk;                    /* instruction blocks */
    u16* boff; u8* blk; u32 cap; /* their offsets and bytes */
    u32 hash, size;              /* rsx_fp_code_hash, rsx_fp_program_size */
    u32 tex_units;               /* rsx_fp_texture_gamma_units with every mask set */
    u32 txp_units;               /* fp_txp_units */
    int nk;                      /* rsx_fp_extract_consts's count */
    u16 koff[FP_MAX_KCONST];     /* its constant slots (0 = counted, no data) */
} FpInfo;
static struct { FpInfo e[FPI_SETS][FPI_WAYS]; u8 rr[FPI_SETS]; } *s_fpi;
static u32 s_fpi_hit, s_fpi_miss;
static int fp_info_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("FP_INFO_CACHE"); on = (e && *e == '0') ? 0 : 1; }
    return on;
}
static u32 fpi_txp_walk(const u8* uc)   /* fp_txp_units' loop, on a pointer */
{
    u32 units = 0;
    for (u32 o = 0; o + 16 <= FP_MAX_BYTES;) {
        const u32 w0 = rsx_fp_read_word(uc + o), w1 = rsx_fp_read_word(uc + o + 4);
        const u32 w2 = rsx_fp_read_word(uc + o + 8), w3 = rsx_fp_read_word(uc + o + 12);
        if (!(w2 & (1u << 31)) && ((w0 >> 24) & 63u) == 0x18u)      /* TXP */
            units |= 1u << ((w0 >> 17) & 15u);
        o += 16;
        if (!(w2 & (1u << 31)) && ((w1 & 3u) == 2 || (w2 & 3u) == 2 || (w3 & 3u) == 2))
            o += 16;                                                   /* inline constant */
        if (w0 & 1u) break;                                            /* END */
    }
    return units;
}
/* The program at fp_addr, or NULL (cache off, unresolvable, out of memory). */
static const FpInfo* fp_info(u32 fp_addr)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    if (!fp_info_on() || !fp_addr || !vm_base) return NULL;
    const u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (off == 0xFFFFFFFFu) return NULL;
    const u8* p = vm_base + off;
    if (!s_fpi && !(s_fpi = calloc(1, sizeof *s_fpi))) return NULL;
    const u32 set = (u32)((((uintptr_t)p) >> 4) * 2654435761u >> 7) & (FPI_SETS - 1u);
    FpInfo* e = s_fpi->e[set];
    for (u32 w = 0; w < FPI_WAYS; w++) {
        FpInfo* c = &e[w];
        if (c->p != p) continue;
        u32 i = 0;
        for (; i < c->nblk; i++)
            if (memcmp(p + c->boff[i], c->blk + (size_t)i * 16u, 16) != 0) break;
        if (i == c->nblk) { s_fpi_hit++; return c; }
    }
    s_fpi_miss++;
    /* Miss: the walk every helper makes (rsx_fp_code_hash's), recording the
     * instruction blocks and the constant slots as rsx_fp_extract_consts
     * counts them; then the original functions for the answers. */
    static u16 boff[FP_MAX_BYTES / 16]; u32 n = 0;
    static u16 koff[FP_MAX_KCONST]; int nk = 0;
    for (u32 o = 0; o + 16 <= FP_MAX_BYTES;) {
        const u32 w0 = rsx_fp_read_word(p + o), w1 = rsx_fp_read_word(p + o + 4);
        const u32 w2 = rsx_fp_read_word(p + o + 8), w3 = rsx_fp_read_word(p + o + 12);
        boff[n++] = (u16)o;
        o += 16;
        if (!(w2 & (1u << 31)) && ((w1 & 3u) == 2 || (w2 & 3u) == 2 || (w3 & 3u) == 2)) {
            if (nk < FP_MAX_KCONST) koff[nk++] = (o + 16 <= FP_MAX_BYTES) ? (u16)o : 0;
            o += 16;
        }
        if (w0 & 1u) break;
    }
    FpInfo* v = &e[s_fpi->rr[set]++ % FPI_WAYS];
    if (v->cap < n) {
        u16* nb = (u16*)realloc(v->boff, (size_t)n * sizeof(u16));
        if (nb) v->boff = nb;
        u8* nk2 = (u8*)realloc(v->blk, (size_t)n * 16u);
        if (nk2) v->blk = nk2;
        if (!nb || !nk2) { v->p = NULL; v->nblk = 0; return NULL; }
        v->cap = n;
    }
    for (u32 i = 0; i < n; i++) memcpy(v->blk + (size_t)i * 16u, p + boff[i], 16);
    memcpy(v->boff, boff, (size_t)n * sizeof(u16));
    v->nblk = n;
    v->hash = rsx_fp_code_hash(p, FP_MAX_BYTES);
    v->size = rsx_fp_program_size(p, FP_MAX_BYTES);
    { u8 all[16]; memset(all, 0xFF, sizeof all);
      v->tex_units = rsx_fp_texture_gamma_units(p, FP_MAX_BYTES, all); }
    v->txp_units = fpi_txp_walk(p);
    v->nk = nk;
    memcpy(v->koff, koff, (size_t)nk * sizeof(u16));
    v->p = p;
    return v;
}
/* rsx_fp_extract_consts for a cached program: the same words, re-read now. */
static int fpi_consts(const FpInfo* fi, float* out, int max_out)
{
    extern u32 rsx_fp_sanitize_constant_word(u32 w);
    int n = 0;
    for (int k = 0; k < fi->nk; k++) {
        if (fi->koff[k] && n < max_out)
            for (int i = 0; i < 4; i++) {
                u32 cw = rsx_fp_sanitize_constant_word(rsx_fp_read_word(fi->p + fi->koff[k] + i * 4));
                memcpy(&out[n * 4 + i], &cw, 4);
            }
        if (n < max_out) n++;
    }
    return n;
}

