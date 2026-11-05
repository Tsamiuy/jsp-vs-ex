static u32 upload_strip_vpull(const rsx_state* state, u32 first, u32 count, int cont)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    s_six_added = 0; s_six_start = s_six.off;
    const rsx_vpull_layout* lay = &s_vpull.lay;
    const u32 S = lay->stride, bix = s_vpull.plan.base_index;
    /* Same index and restart rules as upload_strip_vp_native. */
    const int idx16 = (((state->index_array_dma >> 4) & 0xF) == 1);
    const int restart = state->restart_enable && !(idx16 && state->restart_index > 0xFFFFu);
    const u32 ridx = state->restart_index;
    const u8* ib = vm_base + cellGcmResolveLocated((state->index_array_dma & 0xF) == 0,
                                                   state->index_array_offset);
    u32 vmin = 0xFFFFFFFFu, vmax = 0;
    for (u32 k = 0; k < count; k++) {
        u32 v;
        if (idx16) { const u8* q = ib + (u64)(first + k) * 2; v = ((u32)q[0] << 8) | q[1]; }
        else { const u8* q = ib + (u64)(first + k) * 4;
               v = ((u32)q[0] << 24) | ((u32)q[1] << 16) | ((u32)q[2] << 8) | q[3]; }
        if (restart && v == ridx) continue;
        v = (v + bix) & 0x000FFFFFu;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    const u32 span = (vmin <= vmax) ? vmax - vmin + 1u : 0u;
    /* A new record whose first batch spans far more vertices than it uses
     * would copy mostly unused bytes: leave it to the CPU path. A continuing
     * batch is pulled whatever it spans, or the strip would break. */
    if (!cont && span > 4096u && span > 8u * count) { s_vpull.n_sparse++; return 0xFFFFFFFFu; }
    if (!cont) { s_vpull.rec_base = (s_vpull.off + 15u) & ~15u; s_vpull.rec_k = 0; s_vpull.seg_valid = 0; }
    /* VPULL_ONCE (default on, VPULL_ONCE=0 off): the batches of one record
     * walk the same model, so copying each batch's whole span re-copied the
     * same vertices over and over (~47 MB per batch in the demo), which
     * filled the 64 MB ring and split heavy frames in two (CPU_PROFILE §17,
     * §20). A batch whose vertices lie in, or just above, the record's
     * current segment uses the copy already made and appends only the
     * vertices above it; a batch that starts below the segment opens a new
     * one, as every batch did before. */
    static int once = -1;
    if (once < 0) { const char* e = getenv("VPULL_ONCE"); once = (e && *e == '0') ? 0 : 1; }
    u64 at = 0, len = 0; u32 copy_lo = vmin, k0 = 0, seg_k = 0, seg_lo = 0, seg_hi = 0;
    if (span) {
        if (once && cont && s_vpull.seg_valid && vmin >= s_vpull.seg_lo &&
            vmin <= s_vpull.seg_hi + 64u) {
            seg_k = s_vpull.seg_k; seg_lo = s_vpull.seg_lo; seg_hi = s_vpull.seg_hi;
            if (vmax >= seg_hi) {   /* append seg_hi..vmax after the segment */
                copy_lo = seg_hi;
                at = (u64)s_vpull.rec_base + (u64)(seg_k + (seg_hi - seg_lo)) * S;
                len = (u64)(vmax - seg_hi) * S + lay->extent;
                seg_hi = vmax + 1u;
            }
        } else {                    /* a new segment at the end of the record */
            seg_k = s_vpull.rec_k; seg_lo = vmin; seg_hi = vmax + 1u;
            at = (u64)s_vpull.rec_base + (u64)s_vpull.rec_k * S;
            len = (u64)(span - 1u) * S + lay->extent;
        }
        k0 = seg_k - seg_lo;        /* index = seg_k + (v - seg_lo), mod 2^32 */
    }
    if (at + len > VPULL_RING_BYTES || count > SIX_CAP - s_six.off) { s_vpull.n_drop++; s_drop_draws++; return 0; }
    u8* ring = s_vpull.ring_mapped + (u64)s_d3d.vp_parity * (VPULL_RING_BYTES + VPULL_RING_SLACK);
    if (len) rsx_vpull_copy(ring + at, lay->base + copy_lo * S, (u32)len, lay->main, s_vpull.plan.local_base);
    u32* ixo = s_six.mapped + (u64)s_d3d.vp_parity * SIX_CAP + s_six.off;
    for (u32 k = 0; k < count; k++) {
        u32 v;
        if (idx16) { const u8* q = ib + (u64)(first + k) * 2; v = ((u32)q[0] << 8) | q[1]; }
        else { const u8* q = ib + (u64)(first + k) * 4;
               v = ((u32)q[0] << 24) | ((u32)q[1] << 16) | ((u32)q[2] << 8) | q[3]; }
        ixo[k] = (restart && v == ridx) ? 0xFFFFFFFFu : k0 + ((v + bix) & 0x000FFFFFu);
    }
    if (span) {
        s_vpull.seg_k = seg_k; s_vpull.seg_lo = seg_lo; s_vpull.seg_hi = seg_hi; s_vpull.seg_valid = 1;
        s_vpull.rec_k = seg_k + (seg_hi - seg_lo) + lay->pad;
    }
    s_vpull.off = (u32)((u64)s_vpull.rec_base + (u64)s_vpull.rec_k * S);
    if (!cont && s_d3d.draw_count < MAX_DRAWS)   /* the record this batch opens takes slot draw_count */
        memcpy(s_vpull.tab_mapped + ((u64)s_d3d.vp_parity * MAX_DRAWS + s_d3d.draw_count) * RSX_VPULL_TAB_BYTES,
               &s_vpull.tab_cur, RSX_VPULL_TAB_BYTES);
    s_six.off += count;
    s_six_added = count;
    s_vpull.n_batch++; s_vpull.n_rec += cont ? 0u : 1u; s_vpull.bytes += len; s_vpull.idx += count;
    s_req_verts += count; s_req_draws++;
    return count;
}
