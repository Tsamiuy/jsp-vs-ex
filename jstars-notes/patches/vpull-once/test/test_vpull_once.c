/* VPULL_ONCE check: upload_strip_vpull (cut verbatim from the patched
 * rsx_d3d12_backend.c into vpull_upload.inc) must give every index a vertex
 * whose bytes in the ring equal the guest's bytes for that guest vertex, with
 * VPULL_ONCE on and off, over records of many 256-index batches with strips,
 * restarts, base index, LOCAL and MAIN arrays, attributes past the stride;
 * and report how many bytes each mode copies. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_vpull.h"

u8* vm_base;
u32 cellGcmResolveLocated(int local, u32 off) { return local ? off : off + (8u << 20); }
u32 cellGcmResolveOffset(u32 off) { return off; }

#define VPULL_RING_BYTES (64u << 20)
#define VPULL_RING_SLACK 4096u
#define SIX_CAP (1048576u * 2u)
#define MAX_DRAWS 2048
static struct {
    u8* ring_mapped; u8* tab_mapped; u32 off; u32 rec_base, rec_k;
    u32 seg_k, seg_lo, seg_hi; int seg_valid;
    rsx_fetch_plan plan; rsx_vpull_layout lay; rsx_vpull_tab tab_cur;
    u32 n_rec, n_batch, n_cpu, n_sparse, n_drop; u64 bytes, idx;
} s_vpull;
static struct { u32* mapped; u32 off; } s_six;
static struct { int vp_parity; u32 draw_count; } s_d3d;
static u32 s_six_added, s_six_start;
static u64 s_drop_draws, s_req_verts, s_req_draws;
#include "vpull_upload.inc"

static u32 rng = 99u;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    vm_base = (u8*)malloc(16u << 20);
    for (u32 i = 0; i < (16u << 20); i++) vm_base[i] = (u8)rnd();
    s_vpull.ring_mapped = (u8*)calloc(2u * (VPULL_RING_BYTES + VPULL_RING_SLACK), 1);
    s_vpull.tab_mapped = (u8*)calloc((size_t)MAX_DRAWS * RSX_VPULL_TAB_BYTES * 2, 1);
    s_six.mapped = (u32*)calloc((size_t)SIX_CAP * 2, 4);
    static rsx_state st;
    long bad = 0, checked = 0;
    const char* m = getenv("VPULL_ONCE"); int once = !(m && *m == '0');
    u64 copied = 0, indices = 0;
    for (int rec = 0; rec < 300; rec++) {
        memset(&st, 0, sizeof st);
        const u32 S = 12 + rnd() % 52, main_mem = rnd() & 1;
        const u32 base = rnd() % (3u << 20);
        st.vertex_data_base_index = (rnd() & 3) == 0 ? rnd() % 500 : 0;
        for (int a = 0; a < 4; a++) {
            rsx_vertex_attrib* va = &st.vertex_attribs[a];
            va->enabled = 1; va->stride = S; va->type = 1 + rnd() % 7; va->size = 1 + rnd() % 4;
            va->offset = (base + (a == 3 ? S + rnd() % 8 : rnd() % 8)) | (main_mem ? 0x80000000u : 0);  /* attr 3 past the stride */
        }
        rsx_fetch_plan_init(&st, 0xFu, &s_vpull.plan);
        if (!rsx_vpull_layout_from_plan(&s_vpull.plan, &s_vpull.lay, &s_vpull.tab_cur)) continue;
        /* index buffer: a model of nv vertices walked as 3-index strip pieces + restart,
         * mostly ascending with jumps back (as J-Stars' character strips) */
        const u32 nv = 500 + rnd() % 4000, nb = 2 + rnd() % 20;
        static u8 ibuf[1 << 20];
        const u32 ib_off = 4u << 20;      /* index array in LOCAL memory */
        st.index_array_offset = ib_off; st.index_array_dma = 0x20;   /* u32, LOCAL */
        st.restart_enable = 1; st.restart_index = 0xFFFFFFFFu;
        u32 total = nb * 256, cur = rnd() % nv;
        for (u32 k = 0; k < total; k++) {
            u32 v;
            if ((k & 3) == 3) v = 0xFFFFFFFFu;
            else { if ((rnd() % 50) == 0) cur = rnd() % nv; v = (cur + rnd() % 6) % nv; if ((k & 3) == 2) cur = (cur + 2) % nv; }
            ibuf[k * 4 + 0] = (u8)(v >> 24); ibuf[k * 4 + 1] = (u8)(v >> 16); ibuf[k * 4 + 2] = (u8)(v >> 8); ibuf[k * 4 + 3] = (u8)v;
        }
        memcpy(vm_base + ib_off, ibuf, (size_t)total * 4);
        s_vpull.off = 0; s_six.off = 0; s_d3d.draw_count = 0;
        u32 ix0 = 0; int ok = 1;
        for (u32 b = 0; b < nb; b++) {
            const u32 r = upload_strip_vpull(&st, b * 256, 256, b > 0);
            if (r == 0xFFFFFFFFu) { ok = 0; break; }     /* sparse first batch: CPU path */
            if (b == 0) ix0 = s_six_start;
        }
        if (!ok) continue;
        copied += s_vpull.bytes; s_vpull.bytes = 0; indices += total;
        /* every index -> region vertex: its bytes must be the guest's */
        const u8* region = s_vpull.ring_mapped + s_vpull.rec_base;
        for (u32 k = 0; k < total; k++) {
            const u32 ix = s_six.mapped[ix0 + k];
            const u32 v = ((u32)ibuf[k*4] << 24) | ((u32)ibuf[k*4+1] << 16) | ((u32)ibuf[k*4+2] << 8) | ibuf[k*4+3];
            if (v == 0xFFFFFFFFu) { if (ix != 0xFFFFFFFFu) bad++; continue; }
            const u32 ve = (v + s_vpull.plan.base_index) & 0xFFFFFu;
            for (u32 a = 0; a < 16; a++) {
                const u32 type = s_vpull.tab_cur.attr[a][2]; if (!type) continue;
                const u32 rel = s_vpull.tab_cur.attr[a][0], sz = rsx_vpull_type_size(type, s_vpull.tab_cur.attr[a][3]);
                u8 want[16]; rsx_vpull_copy(want, s_vpull.lay.base + rel + ve * S, sz, s_vpull.lay.main, s_vpull.plan.local_base);
                if (memcmp(region + (u64)ix * S + rel, want, sz) != 0) bad++;
                checked++;
            }
        }
    }
    printf("VPULL_ONCE=%d: %ld attribute reads checked, %ld wrong; copied %.1f MB for %.2f M indices (%.1f bytes/index)\n",
           once, checked, bad, copied / 1048576.0, indices / 1e6, indices ? (double)copied / indices : 0.0);
    return bad ? 1 : 0;
}
