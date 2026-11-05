#!/usr/bin/env python3
"""Build the loading-tips diagnostic patch: apply_diag.py <base.c> <out.c>
  base.c  libs/video/rsx_d3d12_backend.c (jstars-port working tree, LF endings:
          `git show HEAD:libs/video/rsx_d3d12_backend.c` or dos2unix a copy)
  out.c   the patched file; `diff -u base.c out.c` is loading-tips-diag.patch
Every anchor must occur exactly once, so a drifted base fails loudly instead
of patching the wrong place: re-run it on a newer base and fix the anchor it
names. All three switches are opt-in; nothing changes when they are unset."""
import sys

src = open(sys.argv[1], encoding='utf-8').read()
out_path = sys.argv[2]

def rep(old, new, count=1):
    global src
    n = src.count(old)
    if n != count:
        sys.exit("anchor found %d times (want %d):\n%s" % (n, count, old[:300]))
    src = src.replace(old, new)

# ------------------------------------------------- D1: MEMPEEK on a frame window
rep("""      if (mp) { extern uint8_t* vm_base; static int _n = 0;
        u32 addr = 0; int cnt = 8; sscanf(mp, "%x:%d", &addr, &cnt);
        if (vm_base && addr && _n++ < 6) {
""", """      if (mp) { extern uint8_t* vm_base; static int _n = 0;
        u32 addr = 0; int cnt = 8; sscanf(mp, "%x:%d", &addr, &cnt);
        /* MEMPEEK_FRAMES=<from>:<to>: peek on those frame_counts instead of
         * the first 6 -- the loading-tips snapshot texture (0x0082C900) is
         * written minutes into a run (jstars-notes STAGE_TEXTURES.md 11). */
        static long long mf0 = -1, mf1 = -1;
        if (mf0 < 0) { const char* e = getenv("MEMPEEK_FRAMES"); mf0 = mf1 = 0;
                       if (e) sscanf(e, "%lld:%lld", &mf0, &mf1); }
        const int due = mf1 > 0 ? ((long long)s_d3d.frame_count >= mf0 &&
                                   (long long)s_d3d.frame_count <= mf1)
                                : (_n < 6);
        if (vm_base && addr && due) { _n++;
""")
rep("""            fprintf(stderr, "[MEMPEEK] 0x%08X n=%d nonzero=%u range[%g..%g]%c",
                    addr, cnt, nz, lo, hi, 10);
""", """            /* First 16 bytes in hex: a DXT1 block of pure white is
             * FF FF FF FF 00 00 00 00 (compressed from a white source), while
             * sixteen FFs or sixteen 00s mean the buffer was never encoded. */
            char hx[64]; hx[0] = 0;
            for (int i = 0; i < 16; i++)
                snprintf(hx + i * 3, sizeof(hx) - (size_t)i * 3, "%02X ", vm_base[addr + (u32)i]);
            fprintf(stderr, "[MEMPEEK] f=%llu 0x%08X n=%d nonzero=%u range[%g..%g] bytes: %s%c",
                    (unsigned long long)s_d3d.frame_count, addr, cnt, nz, lo, hi, hx, 10);
""")

# ------------------------------------------------- D2: VRAMSCAN on a frame window
rep("""        const char* bs = getenv("VRAMSCAN_BASE");
        if (bs) base = (u32)strtoul(bs, NULL, 0);
        if (vm_base && reported < 6) {
""", """        const char* bs = getenv("VRAMSCAN_BASE");
        if (bs) base = (u32)strtoul(bs, NULL, 0);
        /* VRAMSCAN_FRAMES=<from>:<to>: scan on those frame_counts (the hash of
         * frame from-1 is the baseline) instead of the first 6 changed frames
         * after boot, which never reach a loading screen. */
        static long long vf0 = -1, vf1 = -1;
        if (vf0 < 0) { const char* e = getenv("VRAMSCAN_FRAMES"); vf0 = vf1 = 0;
                       if (e) sscanf(e, "%lld:%lld", &vf0, &vf1); }
        const int vdue = vf1 > 0 ? ((long long)s_d3d.frame_count + 1 >= vf0 &&
                                    (long long)s_d3d.frame_count <= vf1)
                                 : (reported < 6);
        if (vm_base && vdue) {
""")
rep("""                fprintf(stderr, "[VRAMSCAN] base=0x%08X %u/512 blocks changed:%s%c",
                        base, changed, list, 10); }
""", """                fprintf(stderr, "[VRAMSCAN] f=%llu base=0x%08X %u/512 blocks changed:%s%c",
                        (unsigned long long)s_d3d.frame_count, base, changed, list, 10); }
""")

# ------------------------------------------------- D3: RSX_CAP_TEX texture source dump
rep("""        fclose(mf);
    }

    /* Backbuffer (in PRESENT state at capture time) + every used offscreen RT. */
""", """        fclose(mf);
    }

    /* RSX_CAP_TEX=1: the guest bytes every DRAW op samples through unit 0, as
     * tex_<off>_<w>x<h>_<fmt>.bin (one file per distinct offset, 8 MB cap).
     * Ground truth for "is the source blank or is the upload wrong": the
     * loading screen's 1280x720 DXT1 snapshot (0x0082C900) samples pure white
     * (jstars-notes STAGE_TEXTURES.md 11). Decode with tools/texbin.py. */
    { static int ct = -1; if (ct < 0) { const char* e = getenv("RSX_CAP_TEX"); ct = e ? atoi(e) : 0; }
      if (ct) { extern uint8_t* vm_base;
        u32 seen[256]; int ns = 0;
        for (u32 d = 0; d < ndraws && d < MAX_DRAWS && vm_base; d++) {
            const D3D12DrawRecord* r = &s_d3d.draws[d];
            if (r->is_clear || !r->tex[0].set || !r->tex[0].w || !r->tex[0].h) continue;
            const u32 off = r->tex[0].off; int dup = 0;
            for (int i = 0; i < ns; i++) if (seen[i] == off) dup = 1;
            if (dup || ns >= 256) continue;
            seen[ns++] = off;
            const u32 fmt = r->tex[0].fmt & 0x9F, w = r->tex[0].w, h = r->tex[0].h;
            u64 bytes;
            if (fmt == 0x86)                       bytes = (u64)((w + 3) / 4) * ((h + 3) / 4) * 8;   /* DXT1 */
            else if (fmt == 0x87 || fmt == 0x88)   bytes = (u64)((w + 3) / 4) * ((h + 3) / 4) * 16;  /* DXT3/5 */
            else if (r->tex[0].pitch && (r->tex[0].fmt & 0x20)) bytes = (u64)r->tex[0].pitch * h;  /* linear */
            else                                   bytes = (u64)w * h * 4;                           /* swizzled: 32-bit upper bound */
            if (bytes > 0x800000u) bytes = 0x800000u;
            snprintf(path, sizeof path, "%s/tex_%08X_%ux%u_%02X.bin", dir, off, w, h, r->tex[0].fmt);
            FILE* tf = fopen(path, "wb");
            if (tf) { fwrite(vm_base + off, 1, (size_t)bytes, tf); fclose(tf); }
        } } }

    /* Backbuffer (in PRESENT state at capture time) + every used offscreen RT. */
""")

open(out_path, 'w', encoding='utf-8', newline='\n').write(src)
print("ok")
