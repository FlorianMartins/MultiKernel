/* NEXUS-OS Node-L userland — compositeur / gestionnaire de fenêtres (ring 3).
 * Dessine dans un back buffer RAM (sa fenêtre user) puis SYS_fb_present : le noyau
 * copie vers le framebuffer matériel (le ring 3 ne touche jamais la MMIO GPU). */
#include "syscall.h"
#include "font8x16.h"

/* --- syscalls (int 0x80) --- */
static long sys3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "memory", "rcx", "r11");
    return r;
}
static void sys_write(const char *s, unsigned long n) { sys3(SYS_write, 1, (long)s, (long)n); }
static long sys_read(char *s, unsigned long n)        { return sys3(SYS_read, 0, (long)s, (long)n); }
static void sys_exit(long c)                          { sys3(SYS_exit, c, 0, 0); }
static void sys_fb_info(struct fb_info_user *i)       { sys3(SYS_fb_info, (long)i, 0, 0); }
static void sys_fb_present(const void *b, unsigned long n) { sys3(SYS_fb_present, (long)b, (long)n, 0); }
static void sys_mouse(struct mouse_user *m)           { sys3(SYS_mouse, (long)m, 0, 0); }

static unsigned long rdtsc(void) {
    unsigned lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long)hi << 32) | lo;
}
static unsigned long slen(const char *s){ unsigned long n=0; while(s[n])n++; return n; }
static void puts_(const char *s){ sys_write(s, slen(s)); }
static void put_uint(unsigned long v){
    char b[24]; int i=0; if(!v){sys_write("0",1);return;}
    while(v){b[i++]='0'+v%10;v/=10;} char o[24]; int j=0; while(i)o[j++]=b[--i]; sys_write(o,j);
}

/* --- back buffer + primitives de dessin --- */
#define MAXW 1024u
#define MAXH 768u
static unsigned int bb[MAXW * MAXH];
static unsigned int SW, SH;

static inline unsigned rgb(unsigned r,unsigned g,unsigned b){ return (r<<16)|(g<<8)|b; }
static void px(int x,int y,unsigned c){ if((unsigned)x<SW&&(unsigned)y<SH) bb[(unsigned)y*SW+(unsigned)x]=c; }
static void rect(int x,int y,int w,int h,unsigned c){
    for(int j=0;j<h;j++){ int yy=y+j; if((unsigned)yy>=SH)continue;
        for(int i=0;i<w;i++){ int xx=x+i; if((unsigned)xx<SW) bb[(unsigned)yy*SW+(unsigned)xx]=c; } }
}
static void rect_outline(int x,int y,int w,int h,unsigned c){
    rect(x,y,w,1,c); rect(x,y+h-1,w,1,c); rect(x,y,1,h,c); rect(x+w-1,y,1,h,c);
}
static void chr_(int x,int y,char ch,unsigned fg){
    unsigned u=(unsigned char)ch; if(u<FONT_FIRST||u>FONT_LAST)u='?';
    const unsigned char *g=font8x16[u-FONT_FIRST];
    for(int r=0;r<FONT_H;r++){ unsigned char bits=g[r];
        for(int c=0;c<FONT_W;c++) if(bits&(0x80>>c)) px(x+c,y+r,fg); }
}
static void str_(int x,int y,const char *s,unsigned fg){ for(;*s;s++){ chr_(x,y,*s,fg); x+=FONT_W; } }

/* curseur flèche 12x19 */
static const unsigned short cur[19]={
    0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,0xFF80,0xFFC0,
    0xFFE0,0xFE00,0xEF00,0xCF00,0x8780,0x0780,0x03C0,0x03C0,0x0180};
static void draw_cursor(int mx,int my){
    unsigned white=rgb(0xff,0xff,0xff), black=rgb(0,0,0);
    for(int r=0;r<19;r++) for(int c=0;c<12;c++) if(cur[r]&(0x8000>>c)){ px(mx+c+1,my+r,black); px(mx+c,my+r,white); }
}

/* --- fenêtres --- */
#define TITLE_H 26
#define NWIN 3
struct win { int x,y,w,h; const char *title; unsigned accent; const char *body[4]; };
static struct win wins[NWIN]={
    {120,120,360,210,"Terminal", 0x5ccfe6,{"prism:/ $ uname","Prism OS / Axis kernel","multikernel asymetrique",""}},
    {540,150,320,210,"Fichiers", 0x95e6cb,{"/  bin  etc  home","kernel.elf","hello_pe.exe",""}},
    {40,440,470,300,"Systeme",  0xa3d4ff,{"Node-L : POSIX (ring 3)","Node-W : NT-compat","IPC lock-free + IOMMU","W^X, hot-restart"}},
};
static int zorder[NWIN]={0,1,2};   /* du fond vers le premier plan */

static void bring_front(int wi){
    int pos=0; for(int i=0;i<NWIN;i++) if(zorder[i]==wi)pos=i;
    for(int i=pos;i<NWIN-1;i++) zorder[i]=zorder[i+1];
    zorder[NWIN-1]=wi;
}

static void draw_window(struct win *w, int focused){
    unsigned body=rgb(0x13,0x17,0x22), title=w->accent, dim=rgb(0x77,0x84,0xa5), white=rgb(0xe8,0xee,0xff);
    rect(w->x+4,w->y+4,w->w,w->h,rgb(0x02,0x03,0x06));          /* ombre */
    rect(w->x,w->y,w->w,TITLE_H,title);                         /* barre de titre */
    rect(w->x,w->y+TITLE_H,w->w,w->h-TITLE_H,body);             /* corps */
    rect_outline(w->x,w->y,w->w,w->h, focused?white:rgb(0x30,0x38,0x50));
    /* boutons */
    rect(w->x+w->w-18,w->y+8,10,10,rgb(0xf2,0x87,0x79));
    str_(w->x+12,w->y+5,w->title,rgb(0x0a,0x0e,0x14));
    for(int i=0;i<4;i++) if(w->body[i][0]) str_(w->x+14,w->y+TITLE_H+14+i*20,w->body[i], i==0?white:dim);
}

void gui_main(void){
    struct fb_info_user fi; sys_fb_info(&fi);
    if(!fi.w || fi.w>MAXW || fi.h>MAXH){ puts_("[gui] framebuffer indisponible\n"); return; }
    SW=fi.w; SH=fi.h;
    puts_("[gui] compositeur demarre ("); put_uint(SW); puts_("x"); put_uint(SH); puts_(")\n");

    struct mouse_user m={0}; unsigned prev_btn=0;
    int drag=-1, dox=0, doy=0;      /* fenêtre en cours de drag + offset */
    unsigned long frames=0, moves=0, clicks=0;
    unsigned long start=rdtsc(), budget=10000000000ull;   /* borne ~ quelques secondes */
    int last_mx=-1,last_my=-1;

    while(rdtsc()-start < budget){
        sys_mouse(&m);
        int mx=m.x, my=m.y; unsigned btn=m.buttons;
        if(mx!=last_mx||my!=last_my){ moves++; last_mx=mx; last_my=my; }

        int press = (btn&1) && !(prev_btn&1);
        int release = !(btn&1) && (prev_btn&1);

        if(press){
            clicks++;
            puts_("[gui] click @("); put_uint(mx); puts_(","); put_uint(my); puts_(") ");
            /* de haut z vers le bas : fenêtre cliquée (grab n'importe où sur la fenêtre). */
            int hit=-1;
            for(int i=NWIN-1;i>=0;i--){ struct win *w=&wins[zorder[i]];
                if(mx>=w->x&&mx<w->x+w->w&&my>=w->y&&my<w->y+w->h){
                    drag=zorder[i]; dox=mx-w->x; doy=my-w->y; bring_front(drag); hit=drag;
                    puts_("window '"); puts_(w->title); puts_("' -> drag start\n");
                    break;
                }
            }
            if(hit<0) puts_("desktop\n");
        }
        if(release && drag>=0){
            puts_("[gui] drop '"); puts_(wins[drag].title); puts_("' @("); put_uint(wins[drag].x);
            puts_(","); put_uint(wins[drag].y); puts_(")\n"); drag=-1;
        }
        if(drag>=0){
            int nx=mx-dox, ny=my-doy;
            if(nx<0)nx=0; if(ny<28)ny=28;                       /* garder sous la barre de menu */
            if(nx>(int)SW-60)nx=(int)SW-60; if(ny>(int)SH-40)ny=(int)SH-40;
            wins[drag].x=nx; wins[drag].y=ny;
        }
        prev_btn=btn;

        /* --- rendu (double buffering) --- */
        for(unsigned y=0;y<SH;y+=2){ unsigned t=(y*36)/SH; rect(0,y,SW,2,rgb(0x0a + t/3, 0x0e + t/2, 0x1c + t)); }
        rect(0,0,SW,28,rgb(0x0d,0x11,0x1c));                    /* barre de menu */
        str_(14,6,"Prism",rgb(0x5c,0xcf,0xe6)); str_(14+6*FONT_W,6,"desktop",rgb(0x77,0x84,0xa5));
        str_(SW-160,6,"Node-L  ring3",rgb(0x77,0x84,0xa5));
        for(int i=0;i<NWIN;i++) draw_window(&wins[zorder[i]], i==NWIN-1);
        draw_cursor(mx,my);

        sys_fb_present(bb, (unsigned long)SW*SH*4);
        frames++;
        if((frames % 40)==0){ puts_("[gui] cursor=("); put_uint(mx); puts_(","); put_uint(my);
            puts_(") drag="); put_uint(drag>=0?1:0); puts_("\n"); }

        /* petite pause + laisser respirer */
        unsigned long f=rdtsc(); while(rdtsc()-f < 6000000ull){}

        /* sortie anticipée si 'q' au clavier */
        char ch; if(sys_read(&ch,1)==1 && (ch=='q'||ch=='Q')) break;
    }

    puts_("[gui] fin : frames="); put_uint(frames);
    puts_(" mouse-moves="); put_uint(moves);
    puts_(" clicks="); put_uint(clicks); puts_("\n");
    sys_exit(0);
}
