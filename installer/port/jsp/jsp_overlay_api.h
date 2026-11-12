/* Port-side API of the present-time overlay and the menu label switches (toolkit patches 2517 and 2518; ENHANCEMENT).
 * Plain C, defined in the toolkit's rsx_d3d12_backend.c (libs/video/jsp_overlay.h). Declarations only; link the runtime.
 *
 * Overlay: a retained RGBA layer alpha-blended onto the back buffer at present. Draw only when something changes:
 *     if (jsp_overlay_begin(0, 0)) {                       // 0, 0 = the output (back buffer) size; or 1280, 720 for a fixed layout space (stretched)
 *         jsp_overlay_fill_rect(40, 40, 600, 400, JSP_RGBA(0, 0, 0, 200));
 *         jsp_overlay_text_style(60, 60, "Port Settings", 1.5f, JSP_RGBA(255, 255, 255, 255), 0);
 *         jsp_overlay_end();                               // published: shown from the next present on
 *     }
 *     ...  jsp_overlay_close();                            // page closed: a present costs one flag test again
 * Thread rules: begin ... end on one thread and short (a spin lock the render thread only try-locks). Colours 0xAABBGGRR, straight alpha.
 * Text: y is the top of the capital letters (cap height 21 px at scale 1); style 0 = white-grey with black rim (the menu's "off" label),
 * style 1 = black with white glow ("on"); the colour modulates the atlas, so 0xFFFFFFFF is as authored. ASCII 0x20-0x7E, others print '?'.
 * The atlas menu_font.jmf (installer/tools/menu_font_art.py) must sit in JSP_MENU_LABEL_DIR, next to the exe, or in the working directory.
 */
#ifndef JSP_OVERLAY_API_H
#define JSP_OVERLAY_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef JSP_RGBA
#define JSP_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#endif

int   jsp_overlay_begin(int w, int h);                 /* 1 = editing (clears what the last edit drew); 0 = bad size / out of memory */
void  jsp_overlay_fill_rect(int x, int y, int w, int h, uint32_t rgba);
void  jsp_overlay_blit_rgba(int x, int y, int w, int h, const uint8_t* straight_rgba8, int stride_bytes);
void  jsp_overlay_text(float x, float y, const char* str, float scale, uint32_t rgba);
void  jsp_overlay_text_style(float x, float y, const char* str, float scale, uint32_t rgba, int style);
float jsp_overlay_text_width(const char* str, float scale);   /* pixels; 0 when the atlas is missing */
float jsp_overlay_cap_height(float scale);
void  jsp_overlay_end(void);
void  jsp_overlay_close(void);
int   jsp_overlay_active(void);
void  jsp_overlay_output_size(int* w, int* h);         /* the back buffer size (0, 0 before the backend started) */

/* Menu labels (patch 2518). mask bit 0: "Training" on panel 3 (Free Battle) third row; bit 1: "Port Settings" on panel 8 (Options) sixth row.
 * jsp_menu_label_set(1) = the old Training switch. Call jsp_menu_label_panel(obj[0x44]) whenever the open sub-menu panel changes
 * (0 or any other id = none of ours): a replaced label shows only while its own panel is the open one. */
void jsp_menu_label_set(int mask);
void jsp_menu_label_panel(int panel_id);
#ifdef __cplusplus
}
#endif
#endif
