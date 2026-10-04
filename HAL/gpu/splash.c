/* NEXUS-OS — écran de démarrage graphique (démo/fondation framebuffer). */
#include "splash.h"
#include "fb.h"
#include "serial.h"
#include "font8x16.h"   /* FONT_W / FONT_H */

static void utoa(u32 v, char *out) {
    char tmp[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0;
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

void splash_draw(const char *os_name, const char *kernel_name,
                 const char *version, u32 ncpus) {
    if (!fb_ready()) return;
    const struct fb_info *fb = fb_get();
    u32 W = fb->width, H = fb->height;

    /* fond : dégradé vertical bleu nuit -> presque noir */
    for (u32 y = 0; y < H; y++) {
        u32 t = (y * 40) / H;                 /* 0..40 */
        u32 c = fb_rgb(8 + t / 3, 12 + t / 2, 24 + t);
        fb_fill_rect(0, y, W, 1, c);
    }

    u32 accent = fb_rgb(0x5c, 0xcf, 0xe6);    /* cyan MultiKernel */
    u32 accent2 = fb_rgb(0xa3, 0xd4, 0xff);
    u32 white = fb_rgb(0xe8, 0xee, 0xff);
    u32 dim = fb_rgb(0x77, 0x84, 0xa5);
    u32 panel = fb_rgb(0x13, 0x17, 0x22);

    /* logo : un "prisme" (triangle) fait de barres horizontales centrées */
    u32 cx = W / 2, top = H / 6, tri = H / 5;
    for (u32 i = 0; i < tri; i++) {
        u32 half = (i * (W / 8)) / tri;
        u32 g = 0x40 + (i * 0xB0) / tri;
        fb_fill_rect(cx - half, top + i, half * 2, 1, fb_rgb(0x30, g, 0xe0));
    }

    /* titre */
    u32 tscale = (W >= 1024) ? 4 : 3;
    u32 tw = 12 * FONT_W * tscale;            /* "MultiKernel" = 11 chars */
    fb_draw_string_scaled(cx - tw / 2, top + tri + 30, "MultiKernel", accent, 0, tscale);
    fb_draw_string_scaled(cx - tw / 2 + 6 * FONT_W * tscale, top + tri + 30,
                          " Axis", accent2, 0, tscale);

    /* panneau d'infos */
    u32 px = cx - 240, py = top + tri + 30 + FONT_H * tscale + 40, pw = 480, ph = 150;
    fb_fill_rect(px, py, pw, ph, panel);
    fb_fill_rect(px, py, pw, 3, accent);      /* liseré haut */

    char ncpu_s[12]; utoa(ncpus, ncpu_s);
    u32 lx = px + 24, ly = py + 24;
    fb_draw_string(lx, ly,        "OS      : ", dim, panel); fb_draw_string(lx + 10 * FONT_W, ly,        os_name,     white, panel);
    fb_draw_string(lx, ly + 24,   "Kernel  : ", dim, panel); fb_draw_string(lx + 10 * FONT_W, ly + 24,   kernel_name, white, panel);
    fb_draw_string(lx, ly + 48,   "Version : ", dim, panel); fb_draw_string(lx + 10 * FONT_W, ly + 48,   version,     white, panel);
    fb_draw_string(lx, ly + 72,   "CPUs    : ", dim, panel); fb_draw_string(lx + 10 * FONT_W, ly + 72,   ncpu_s,      white, panel);
    fb_draw_string(lx, ly + 96,   "Model   : multikernel asymetrique (Node-L + Node-W)", dim, panel);

    fb_draw_string(cx - 30 * FONT_W / 2, py + ph + 30,
                   "console serie active - boot en cours...", dim, 0);

    serial_printf("[splash] écran de démarrage dessiné (%ux%u)\n", W, H);
}
