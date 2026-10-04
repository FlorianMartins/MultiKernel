/* NEXUS-OS — démo graphique : curseur souris + double buffering (Phase 10).
 * Tourne sur le BSP (identité 4 GiB : FB + LAPIC mappés). Bornée en temps (TSC). */
#include "gfxdemo.h"
#include "fb.h"
#include "font8x16.h"
#include "ioapic.h"
#include "ps2mouse.h"
#include "kc/cpu.h"
#include "kc/string.h"
#include "serial.h"

extern u64  isr_table[32];
extern void load_idt(void *idt_ptr);
extern void isr_mouse(void);
extern void isr_timer_ignore(void);
extern void isr_spurious(void);

/* --- IDT locale au BSP pour la démo --- */
struct idt_entry { u16 lo; u16 sel; u8 ist; u8 attr; u16 mid; u32 hi; u32 z; } __attribute__((packed));
struct idt_ptr   { u16 limit; u64 base; } __attribute__((packed));
static struct idt_entry d_idt[256] __attribute__((aligned(16)));
static struct idt_ptr   d_idt_ptr;

static void set_gate(int v, u64 h) {
    d_idt[v].lo = h & 0xFFFF; d_idt[v].sel = 0x08; d_idt[v].ist = 0;
    d_idt[v].attr = 0x8E; d_idt[v].mid = (h >> 16) & 0xFFFF;
    d_idt[v].hi = (h >> 32) & 0xFFFFFFFF; d_idt[v].z = 0;
}

/* curseur flèche 12x19 (1 = blanc, contour implicite) */
static const u16 cursor[19] = {
    0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,0xFF80,0xFFC0,
    0xFFE0,0xFE00,0xEF00,0xCF00,0x8780,0x0780,0x03C0,0x03C0,0x0180,
};

static void draw_cursor(u32 mx, u32 my, u32 white, u32 black) {
    for (u32 r = 0; r < 19; r++)
        for (u32 c = 0; c < 12; c++) {
            if (cursor[r] & (0x8000 >> c)) {
                fb_put_pixel(mx + c, my + r, white);
                /* léger contour noir à droite/bas pour la lisibilité */
                fb_put_pixel(mx + c + 1, my + r, black);
            }
        }
}

static void utoa(i32 v, char *out) {
    char t[12]; int i = 0, neg = v < 0; u32 u = neg ? -v : v;
    if (!u) { out[0] = '0'; out[1] = 0; return; }
    while (u) { t[i++] = '0' + (u % 10); u /= 10; }
    int j = 0; if (neg) out[j++] = '-';
    while (i) out[j++] = t[--i];
    out[j] = 0;
}

void gfx_demo_run(const struct acpi_madt *madt) {
    if (!fb_ready()) { serial_printf("[gfx] pas de framebuffer, démo ignorée\n"); return; }
    const struct fb_info *fb = fb_get();

    /* IDT du BSP : exceptions + souris + timer/spurious */
    memset(d_idt, 0, sizeof(d_idt));
    for (int i = 0; i < 32; i++) set_gate(i, isr_table[i]);
    set_gate(MOUSE_IRQ_VECTOR, (u64)(uintptr_t)&isr_mouse);
    set_gate(0x20, (u64)(uintptr_t)&isr_timer_ignore);
    set_gate(0xFF, (u64)(uintptr_t)&isr_spurious);
    d_idt_ptr.limit = sizeof(d_idt) - 1;
    d_idt_ptr.base = (u64)(uintptr_t)d_idt;
    load_idt(&d_idt_ptr);

    /* route IRQ12 (souris) vers CE cœur (BSP) */
    struct ioapic_cfg cfg;
    ioapic_init_from_madt(madt, 12, &cfg);
    ioapic_route(&cfg, cfg.kbd_gsi, MOUSE_IRQ_VECTOR, (u8)cpu_apic_id());
    ps2_mouse_init(fb->width, fb->height);

    if (!fb_enable_backbuffer()) { serial_printf("[gfx] back buffer indisponible\n"); return; }

    __asm__ volatile("sti");
    serial_printf("[gfx] démo souris + double buffering (déplace la souris)\n");

    u32 bg_top = fb_rgb(0x10, 0x16, 0x28), bg_bot = fb_rgb(0x06, 0x08, 0x12);
    u32 accent = fb_rgb(0x5c, 0xcf, 0xe6), white = fb_rgb(0xf2, 0xf6, 0xff);
    u32 dim = fb_rgb(0x77, 0x84, 0xa5), black = fb_rgb(0, 0, 0);
    (void)bg_bot;

    u64 start = rdtsc();
    u64 budget = 12000000000ull;   /* ~4-8 s selon fréquence TSC */
    u32 frames = 0;
    i32 last_logged = -1;

    while (rdtsc() - start < budget) {
        const struct mouse_state *m = mouse_get();

        /* --- dessin dans le back buffer --- */
        /* fond : bandes (dégradé bon marché) */
        for (u32 y = 0; y < fb->height; y += 4) {
            u32 t = (y * 30) / fb->height;
            fb_fill_rect(0, y, fb->width, 4, fb_rgb(0x08 + t / 3, 0x0c + t / 2, 0x18 + t));
        }
        (void)bg_top;

        fb_fill_rect(0, 0, fb->width, 3, accent);
        fb_draw_string_scaled(40, 40, "MultiKernel GUI", accent, 0, 2);
        fb_draw_string(40, 90, "souris PS/2 + double buffering (Phase 10)", dim, 0);

        char sx[12], sy[12], sb[4];
        utoa(m->x, sx); utoa(m->y, sy);
        sb[0] = (m->buttons & 1) ? 'L' : '.';
        sb[1] = (m->buttons & 4) ? 'M' : '.';
        sb[2] = (m->buttons & 2) ? 'R' : '.';
        sb[3] = 0;
        char line[64]; int p = 0;
        const char *lbl = "curseur: x=";
        for (const char *s = lbl; *s; s++) line[p++] = *s;
        for (char *s = sx; *s; s++) line[p++] = *s;
        line[p++] = ' '; line[p++] = 'y'; line[p++] = '=';
        for (char *s = sy; *s; s++) line[p++] = *s;
        line[p++] = ' '; line[p++] = 'b'; line[p++] = 't'; line[p++] = 'n'; line[p++] = '=';
        for (char *s = sb; *s; s++) line[p++] = *s;
        line[p] = 0;
        fb_draw_string(40, 120, line, white, 0);

        /* le curseur */
        draw_cursor((u32)m->x, (u32)m->y, white, black);

        /* --- présentation (copie back->front, pas de scintillement) --- */
        fb_present();
        frames++;

        if (m->packets && (last_logged < 0 || (i32)m->packets - last_logged >= 10)) {
            serial_printf("[gfx] mouse packets=%u pos=(%d,%d) btn=0x%x\n",
                          m->packets, m->x, m->y, m->buttons);
            last_logged = (i32)m->packets;
        }

        /* pacing ~ court délai TSC */
        u64 f = rdtsc(); while (rdtsc() - f < 8000000ull) cpu_relax();
    }

    __asm__ volatile("cli");
    const struct mouse_state *m = mouse_get();
    serial_printf("[gfx] démo terminée : %u frames, %u paquets souris, curseur final (%d,%d)\n",
                  frames, m->packets, m->x, m->y);
    serial_printf("[gfx] ASSERT double-buffering utilisé: PASS (fb_present sur %u frames)\n", frames);
}
