/* Host test of libs/video/jsp_overlay.h (toolkit patch 2517): canvas, rectangles, text, dirty rectangles, lock.
 *   gcc -O1 -Wall -I<patched tree>/libs/video installer/tools/port_overlay_test.c -o jov_test -lm
 *   JSP_MENU_LABEL_DIR=<folder with menu_font.jmf> ./jov_test [out.ppm]      (menu_font.jmf from menu_font_art.py)
 * Not part of the game build. */
#include "jsp_overlay.h"
#include <assert.h>

static unsigned px(int x, int y, int k) { return jov.px[((size_t)y * jov.w + x) * 4 + k]; }

int main(int argc, char** argv)
{
    jsp_overlay_set_output(1280, 720);
    assert(!jsp_overlay_active());
    assert(jsp_overlay_begin(0, 0) && jov.w == 1280 && jov.h == 720);
    jsp_overlay_fill_rect(10, 10, 100, 50, JSP_RGBA(255, 0, 0, 255));
    jsp_overlay_fill_rect(60, 30, 100, 50, JSP_RGBA(0, 0, 255, 128));      /* half-transparent blue over red and over nothing */
    assert(px(20, 20, 0) == 255 && px(20, 20, 3) == 255);
    assert(px(70, 40, 0) == 127 && px(70, 40, 2) == 128 && px(70, 40, 3) == 255);   /* over red: red halves, blue 128 */
    assert(px(140, 70, 2) == 128 && px(140, 70, 3) == 128);                          /* over nothing: premultiplied */
    jsp_overlay_fill_rect(-50, -50, 60, 60, JSP_RGBA(0, 255, 0, 255));           /* clipped */
    assert(px(0, 0, 1) == 255 && jov.cur.x0 == 0 && jov.cur.y0 == 0);
    float w = jsp_overlay_text_width("Port Settings", 1.0f);
    int font = jov.font_ok;
    if (font) assert(w > 100 && w < 400);
    jsp_overlay_text(200, 100, "Port Settings: 0.1.2 ~", 1.0f, JSP_RGBA(255, 255, 255, 255));
    jsp_overlay_text_style(200, 150, "Hello, World! (scale 2)", 2.0f, JSP_RGBA(255, 255, 255, 255), 1);
    { static uint8_t bm[4 * 4 * 4]; for (int i = 0; i < 16; i++) { bm[i*4] = 255; bm[i*4+1] = 255; bm[i*4+3] = 255; }
      jsp_overlay_blit_rgba(300, 300, 4, 4, bm, 16); assert(px(301, 301, 0) == 255 && px(301, 301, 2) == 0); }
    jsp_overlay_end();
    assert(jsp_overlay_active() && jov.gen == 1);
    for (int i = 0; i < 4; i++) assert(jov.pend[i].x1 > jov.pend[i].x0);           /* every frame index still owes the first upload */
    jov.pend[0] = (jov_rect){0, 0, 0, 0};
    if (argc > 1) { FILE* f = fopen(argv[1], "wb"); fprintf(f, "P6\n%d %d\n255\n", jov.w, jov.h);
        for (int y = 0; y < jov.h; y++) for (int x = 0; x < jov.w; x++) { unsigned a = px(x, y, 3);   /* over a grey-blue ground */
            unsigned bg[3] = {92, 92, 130}; for (int k = 0; k < 3; k++) fputc(px(x, y, k) + bg[k] * (255 - a) / 255, f); }
        fclose(f); }
    /* second edit: only the old and the new bounds are dirty, the rest of the canvas is untouched */
    jov_rect first = jov.used; assert(first.x1 > first.x0 && first.x0 == 0);
    assert(jsp_overlay_begin(0, 0));
    assert(px(20, 20, 3) == 0);                                                    /* last edit's pixels are cleared */
    jsp_overlay_fill_rect(1000, 600, 20, 20, JSP_RGBA(1, 2, 3, 255));
    jsp_overlay_end();
    assert(jov.pend[0].x0 == 0 && jov.pend[0].x1 >= 1020 && jov.pend[0].y1 >= 620);        /* old bounds + new rectangle */
    jov.pend[0] = (jov_rect){0, 0, 0, 0};                                         /* frame 0 uploaded it */
    assert(jsp_overlay_begin(0, 0)); jsp_overlay_fill_rect(1010, 610, 5, 5, JSP_RGBA(9, 9, 9, 255)); jsp_overlay_end();
    assert(jov.pend[0].x0 == 1000 && jov.pend[0].x1 == 1020 && jov.pend[0].y0 == 600 && jov.pend[0].y1 == 620);   /* only the rows that changed */
    /* the renderer's try-lock fails while an edit is open */
    assert(jsp_overlay_begin(0, 0)); assert(!jov_try_lock()); jsp_overlay_end(); assert(jov_try_lock()); jov_unlock();
    jsp_overlay_close(); assert(!jsp_overlay_active());
    printf("jsp_overlay_test OK (font %s, text width %.1f)\n", font ? "loaded" : "MISSING: text not tested", w);
    return 0;
}
