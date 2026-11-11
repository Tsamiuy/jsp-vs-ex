// Host test for the overlay text spacing (letters must not run into each other).
// Reads menu_font.jmf (layout: the header of menu_font_art.py) and lays a string out exactly as the overlay's
// jsp_overlay_text_style does (pen += advance/16 * scale, glyph cell at pen - pen_x; per-glyph alpha-over, style 0), at scale 1 with the pen
// rounded to whole pixels (the blitter interpolates fractional pens; whole pixels are the worst case of neither).
//   ./overlay_font_test JMF                     checks, exit 1 on failure
//   ./overlay_font_test JMF --ppm OUT.ppm "text"   writes a render (dark panel colour background) for the before/after pictures
// The atlas has rim and body planes (styles 2, 3) and the draw is two-pass (all rims of the string, then all bodies), so the next
// glyph's rim can no longer cover a body: only its body can. The model below does the same; with an old 2-style atlas it falls back to the single pass.
// The metric: a "fill" pixel is a pixel of the white-to-grey letter body (style 0, alpha >= 230, lightness >= 100); the rim is black.
// Glyph n+1 is drawn over glyph n, so every fill pixel of n that lies under an opaque (alpha >= 128) pixel of n+1 is a letter body that the
// next letter's black rim or body eats: "the letters run into each other". The check counts those pixels per adjacent pair.
// THRESHOLD: none. With the two-pass blit a rim cannot cover a body, so no body pixel of any glyph of the 258 sample pairs may lie under
// the next glyph's body (kMaxEaten = kMaxRow = 0); the track in the atlas is the smallest 0.5 px step that achieves that.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

struct Font {
    uint32_t aw, ah, nst, first, count, cell_h, base, stride, cap;
    std::vector<uint16_t> met;
    std::vector<uint8_t> atlas;
    bool load(const char* path)
    {
        FILE* f = fopen(path, "rb"); if (!f) return false;
        uint32_t hd[10];
        if (fread(hd, 4, 10, f) != 10 || memcmp(hd, "JMF1", 4)) { fclose(f); return false; }
        aw = hd[1]; ah = hd[2]; nst = hd[3]; first = hd[4]; count = hd[5]; cell_h = hd[6]; base = hd[7]; stride = hd[8]; cap = hd[9];
        met.resize(count * 8); atlas.resize((size_t)aw * ah * 4);
        bool ok = fread(met.data(), 2, met.size(), f) == met.size() && fread(atlas.data(), 1, atlas.size(), f) == atlas.size();
        fclose(f); return ok;
    }
    const uint16_t* m(char c) const { unsigned u = (unsigned char)c; if (u < first || u >= first + count) u = '?'; return &met[(u - first) * 8]; }
    const uint8_t* px(const uint16_t* g, int x, int y, int style = 0) const { return &atlas[((size_t)(g[1] + y + style * stride) * aw + g[0] + x) * 4]; }
    bool two_pass() const { return nst >= 6; }   // planes 2 (rim) and 3 (body) of style 0 are blitted rims first, then bodies
};

struct Canvas {
    int w, h; std::vector<float> rgb;   // straight colour over an opaque background
    Canvas(int w_, int h_, float r, float g, float b) : w(w_), h(h_), rgb((size_t)w_ * h_ * 3) { for (size_t i = 0; i < rgb.size(); i += 3) { rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b; } }
};

// Lay out; calls draw(glyph index, pen x) per glyph. Returns the end pen.
static int layout(const Font& F, const std::string& s, int x0, std::vector<int>& pens)
{
    float x = (float)x0;
    for (char c : s) { pens.push_back((int)(x + 0.5f)); x += F.m(c)[4] / 16.0f; }
    return (int)(x + 0.5f);
}

static void draw(const Font& F, Canvas& C, const std::string& s, int x0, int base_y)
{
    std::vector<int> pens; layout(F, s, x0, pens);
    for (int pass = 0; pass < (F.two_pass() ? 2 : 1); pass++)
    for (size_t i = 0; i < s.size(); i++) {
        const uint16_t* g = F.m(s[i]); if (!g[2]) continue;
        const int dx = pens[i] - (int16_t)g[3], dy = base_y - (int)F.base;
        for (int y = 0; y < (int)F.cell_h; y++) for (int x = 0; x < (int)g[2]; x++) {
            const uint8_t* p = F.px(g, x, y, F.two_pass() ? 2 + pass : 0); const int X = dx + x, Y = dy + y;
            if (!p[3] || X < 0 || Y < 0 || X >= C.w || Y >= C.h) continue;
            float a = p[3] / 255.0f; float* d = &C.rgb[((size_t)Y * C.w + X) * 3];
            for (int k = 0; k < 3; k++) d[k] = p[k] * a + d[k] * (1 - a);
        }
    }
}

struct Pair { char a, b; int eaten, worst_row; };

// Fill pixels of glyph a under an opaque pixel of glyph b, with b's pen at a's pen + adv(a).
static float g_track = 0;   // extra advance in px on top of the atlas's, for sizing only (the shipped test runs with 0: the atlas carries its own)
static Pair measure(const Font& F, char a, char b)
{
    const uint16_t* ga = F.m(a); const uint16_t* gb = F.m(b);
    Pair r{a, b, 0, 0};
    if (!ga[2] || !gb[2]) return r;
    const int pa = 0, pb = (int)(ga[4] / 16.0f + g_track + 0.5f);
    const int dxa = pa - (int16_t)ga[3], dxb = pb - (int16_t)gb[3];
    for (int y = 0; y < (int)F.cell_h; y++) {
        int row = 0;
        for (int x = 0; x < (int)ga[2]; x++) {
            // two-pass: a's body is the body plane, and only b's body plane can cover it (every rim was drawn before every body)
            const uint8_t* p = F.px(ga, x, y, F.two_pass() ? 3 : 0);
            if (p[3] < 230 || (p[0] + p[1] + p[2]) / 3 < 100) continue;          // not body
            const int bx = dxa + x - dxb; if (bx < 0 || bx >= (int)gb[2]) continue;
            if (F.px(gb, bx, y, F.two_pass() ? 3 : 0)[3] >= 128) row++;
        }
        r.eaten += row; r.worst_row = std::max(r.worst_row, row);
    }
    return r;
}

static const int kMaxEaten = 0;       // two-pass blit, so no body pixel of a glyph may be covered by the next glyph's body
static const int kMaxRow = 0;

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s menu_font.jmf [--ppm out.ppm \"text\"]\n", argv[0]); return 2; }
    if (getenv("TRACK")) g_track = (float)atof(getenv("TRACK"));
    Font F; if (!F.load(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    if (argc >= 5 && !strcmp(argv[2], "--ppm")) {
        const char* lines[] = {argv[4], "Settings files: 0 read, 0 rejected", "Online name: (none) (file)"};
        Canvas C(840, 170, 16, 20, 40);
        draw(F, C, lines[0], 20, 40); draw(F, C, lines[1], 20, 90); draw(F, C, lines[2], 20, 140);
        FILE* o = fopen(argv[3], "wb"); fprintf(o, "P6\n%d %d\n255\n", C.w, C.h);
        for (float v : C.rgb) fputc((int)std::min(255.0f, v + 0.5f), o);
        fclose(o); return 0;
    }
    const char* samples[] = {"Port Settings - Online", "Settings files: 0 read, 0 rejected", "Online play this session: no", "Community preset: (none) (0 saved)",
                             "Read only. Changes apply on the next start.", "The quick brown fox jumps over the lazy dog 0123456789",
                             "Method: Off (file)", "Saved. Applies on the next start."};
    int fails = 0, pairs = 0, worst = 0; char wa = 0, wb = 0;
    for (const char* s : samples) {
        for (size_t i = 0; s[i] && s[i + 1]; i++) {
            Pair p = measure(F, s[i], s[i + 1]); pairs++;
            if (p.eaten > worst) { worst = p.eaten; wa = p.a; wb = p.b; }
            if (p.eaten > kMaxEaten || p.worst_row > kMaxRow) { fails++; if (fails <= 12) printf("  eaten: '%c''%c' %d body px (worst row %d)\n", p.a, p.b, p.eaten, p.worst_row); }
        }
    }
    printf("pairs %d, failing %d (> %d px or > %d per row), worst '%c''%c' %d px\n", pairs, fails, kMaxEaten, kMaxRow, wa, wb, worst);
    printf(fails ? "FAIL overlay font spacing\n" : "ok   overlay font spacing\n");
    return fails ? 1 : 0;
}
