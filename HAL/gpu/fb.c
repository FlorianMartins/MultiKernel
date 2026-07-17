/* NEXUS-OS HAL — framebuffer linéaire + rendu 2D.
 * Le framebuffer physique (< 4 GiB sous QEMU) est déjà mappé en identité par le stub
 * de boot (pagination 4 GiB), donc directement adressable depuis le Coordinator. */
#include "fb.h"
#include "kc/string.h"
#include "serial.h"
#include "font8x16.h"

static struct fb_info g_fb;

u32 fb_rgb(u8 r, u8 g, u8 b) {
    if (!g_fb.present) return 0;
    return ((u32)r << g_fb.red_pos) | ((u32)g << g_fb.green_pos) | ((u32)b << g_fb.blue_pos);
}

void fb_init(const struct mb2_tag_framebuffer *tag) {
    memset(&g_fb, 0, sizeof(g_fb));
    if (!tag || tag->fb_type != 1 || tag->bpp != 32) {
        serial_printf("[fb] pas de framebuffer RGB 32bpp exploitable\n");
        return;
    }
    g_fb.present   = true;
    g_fb.addr      = tag->addr;
    g_fb.pitch     = tag->pitch;
    g_fb.width     = tag->width;
    g_fb.height    = tag->height;
    g_fb.bpp       = tag->bpp;
    g_fb.red_pos   = tag->red_pos;
    g_fb.green_pos = tag->green_pos;
    g_fb.blue_pos  = tag->blue_pos;
    serial_printf("[fb] %ux%u %ubpp pitch=%u @0x%lx (R%u G%u B%u)\n",
                  g_fb.width, g_fb.height, g_fb.bpp, g_fb.pitch, g_fb.addr,
                  g_fb.red_pos, g_fb.green_pos, g_fb.blue_pos);
}

bool fb_ready(void) { return g_fb.present; }
const struct fb_info *fb_get(void) { return &g_fb; }

static inline u32 *pixel_at(u32 x, u32 y) {
    return (u32 *)(uintptr_t)(g_fb.addr + (u64)y * g_fb.pitch + (u64)x * 4);
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    if (!g_fb.present || x >= g_fb.width || y >= g_fb.height) return;
    *pixel_at(x, y) = color;
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!g_fb.present) return;
    for (u32 j = 0; j < h; j++) {
        u32 py = y + j;
        if (py >= g_fb.height) break;
        u32 *row = pixel_at(x, py);
        for (u32 i = 0; i < w; i++) {
            if (x + i >= g_fb.width) break;
            row[i] = color;
        }
    }
}

void fb_clear(u32 color) {
    if (!g_fb.present) return;
    fb_fill_rect(0, 0, g_fb.width, g_fb.height, color);
}

void fb_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    if (!g_fb.present) return;
    unsigned uc = (unsigned char)c;
    if (uc < FONT_FIRST || uc > FONT_LAST) uc = '?';
    const u8 *glyph = font8x16[uc - FONT_FIRST];
    for (u32 row = 0; row < FONT_H; row++) {
        u8 bits = glyph[row];
        for (u32 col = 0; col < FONT_W; col++) {
            u32 color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(u32 x, u32 y, const char *s, u32 fg, u32 bg) {
    for (; *s; s++) { fb_draw_char(x, y, *s, fg, bg); x += FONT_W; }
}

void fb_draw_string_scaled(u32 x, u32 y, const char *s, u32 fg, u32 bg, u32 scale) {
    if (scale <= 1) { fb_draw_string(x, y, s, fg, bg); return; }
    for (; *s; s++) {
        unsigned uc = (unsigned char)*s;
        if (uc < FONT_FIRST || uc > FONT_LAST) uc = '?';
        const u8 *glyph = font8x16[uc - FONT_FIRST];
        for (u32 row = 0; row < FONT_H; row++) {
            u8 bits = glyph[row];
            for (u32 col = 0; col < FONT_W; col++) {
                if (bits & (0x80 >> col))
                    fb_fill_rect(x + col * scale, y + row * scale, scale, scale, fg);
            }
        }
        x += FONT_W * scale;
    }
}
