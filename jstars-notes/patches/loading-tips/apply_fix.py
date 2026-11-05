#!/usr/bin/env python3
"""Build the loading-tips FIX patch: apply_fix.py <base.c> <out.c>
  base.c  libs/video/rsx_d3d12_backend.c (jstars-port working tree, LF endings)
  out.c   the patched file; `diff -u base.c out.c` is loading-tips-fix.patch
Two anchors, each must occur exactly once. Default on; SCREEN_AS_TEX_FORCE=1
restores the old behaviour (frame copy bound for every display-sized non-RT
texture, whatever its format or contents)."""
import sys

src = open(sys.argv[1], encoding='utf-8').read()
out_path = sys.argv[2]

def rep(old, new, count=1):
    global src
    n = src.count(old)
    if n != count:
        sys.exit("anchor found %d times (want %d):\n%s" % (n, count, old[:300]))
    src = src.replace(old, new)

# ---- F1: helper next to the other small switch readers
rep("""static int menu_tex_pitch_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("MENU_TEX_PITCH"); on = e ? atoi(e) : 0; }
    return on;
}
""", """static int menu_tex_pitch_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("MENU_TEX_PITCH"); on = e ? atoi(e) : 0; }
    return on;
}
/* Loading-tips fix (jstars-notes FINDINGS 69, patches\\loading-tips): should the
 * frame copy stand in for this display-sized, non-RT texture? Only when the
 * texture is one the RSX could have written (not DXT: the GPU never writes
 * compressed data) AND the guest bytes behind it are empty, i.e. the title
 * samples a surface we rendered but never wrote back (the water reflection).
 * CPU content at the display size keeps its own bytes: the boot logos
 * (uncompressed) and the match loading screen's DXT1 tips snapshot at
 * 0x0082C900, both of which the size-only rule drew as the previous frame.
 * 256 samples spread over the whole image, so a picture with an empty top
 * band still counts as written. SCREEN_AS_TEX_FORCE=1: the old size-only rule. */
static int screen_as_tex_wants(const D3D12DrawRecord* dr, int u)
{
    static int force = -1;
    if (force < 0) { const char* e = getenv("SCREEN_AS_TEX_FORCE"); force = e ? atoi(e) : 0; }
    if (force) return 1;
    const u32 bf = dr->tex[u].fmt & 0x9F;
    if (bf >= 0x86 && bf <= 0x88) return 0;          /* compressed: never a frame */
    extern uint8_t* vm_base;
    if (!vm_base || !dr->tex[u].off) return 1;
    const u32 sz = dr->tex[u].w * dr->tex[u].h * 4u;
    const u32 step = sz > 0x10000u ? sz / 256u : 256u;
    for (u32 i = 0; i < sz; i += step)
        if (vm_base[dr->tex[u].off + i]) return 0;    /* written by the CPU: use it */
    return 1;
}
""")

# ---- F2: the bind site
rep("""            if (dr->tex[_u].set && s_screen_copy &&
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height &&
                off_rt_find(dr->tex[_u].raw) < 0) {
                static int en = -1;
                if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 1; }
                if (en) {
""", """            /* Loading-tips fix: only for a texture the RSX could have written
             * whose guest bytes are empty (screen_as_tex_wants). The boot logos
             * and the loading screen's DXT1 tips snapshot are display-sized CPU
             * content and drew the previous frame before this (FINDINGS 69). */
            if (dr->tex[_u].set && s_screen_copy &&
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height &&
                off_rt_find(dr->tex[_u].raw) < 0 && screen_as_tex_wants(dr, _u)) {
                static int en = -1;
                if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 1; }
                if (en) {
""")

open(out_path, 'w', encoding='utf-8', newline='\n').write(src)
print("ok")
