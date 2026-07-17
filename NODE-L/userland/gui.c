/* NEXUS-OS Node-L userland — compositeur + terminal graphique branché sur le shell.
 * Ring 3 : dessine dans un back buffer RAM puis SYS_fb_present (le noyau copie -> FB).
 * Une fenêtre "Terminal" émule un terminal (grille de caractères) alimenté par le
 * clavier ; le shell tourne dans la boucle du compositeur et écrit dans le terminal. */
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

static unsigned long rdtsc(void){ unsigned lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((unsigned long)hi<<32)|lo; }
static unsigned long slen(const char *s){ unsigned long n=0; while(s[n])n++; return n; }
static void logs(const char *s){ sys_write(s, slen(s)); }         /* trace série (debug/test) */
static void log_uint(unsigned long v){ char b[24];int i=0; if(!v){sys_write("0",1);return;} while(v){b[i++]='0'+v%10;v/=10;} char o[24];int j=0; while(i)o[j++]=b[--i]; sys_write(o,j); }
static int  starts(const char *l,const char *k){ unsigned i=0; while(k[i]){ if(l[i]!=k[i])return 0; i++; } return l[i]==0||l[i]==' '; }

/* --- back buffer + primitives --- */
#define MAXW 1024u
#define MAXH 768u
static unsigned int bb[MAXW*MAXH];
static unsigned int SW,SH;
static inline unsigned rgb(unsigned r,unsigned g,unsigned b){ return (r<<16)|(g<<8)|b; }
static void px(int x,int y,unsigned c){ if((unsigned)x<SW&&(unsigned)y<SH) bb[(unsigned)y*SW+(unsigned)x]=c; }
static void rect(int x,int y,int w,int h,unsigned c){ for(int j=0;j<h;j++){int yy=y+j; if((unsigned)yy>=SH)continue; for(int i=0;i<w;i++){int xx=x+i; if((unsigned)xx<SW) bb[(unsigned)yy*SW+(unsigned)xx]=c;}} }
static void rect_outline(int x,int y,int w,int h,unsigned c){ rect(x,y,w,1,c);rect(x,y+h-1,w,1,c);rect(x,y,1,h,c);rect(x+w-1,y,1,h,c); }
static void chr_(int x,int y,char ch,unsigned fg){ unsigned u=(unsigned char)ch; if(u<FONT_FIRST||u>FONT_LAST)u='?'; const unsigned char *g=font8x16[u-FONT_FIRST]; for(int r=0;r<FONT_H;r++){unsigned char bt=g[r]; for(int c=0;c<FONT_W;c++) if(bt&(0x80>>c)) px(x+c,y+r,fg);} }
static void str_(int x,int y,const char *s,unsigned fg){ for(;*s;s++){ chr_(x,y,*s,fg); x+=FONT_W; } }

static const unsigned short cur[19]={0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,0xFF80,0xFFC0,0xFFE0,0xFE00,0xEF00,0xCF00,0x8780,0x0780,0x03C0,0x03C0,0x0180};
static void draw_cursor(int mx,int my){ unsigned w=rgb(255,255,255),k=rgb(0,0,0); for(int r=0;r<19;r++)for(int c=0;c<12;c++) if(cur[r]&(0x8000>>c)){px(mx+c+1,my+r,k);px(mx+c,my+r,w);} }

/* ============ émulateur de terminal ============ */
#define TCOLS 52
#define TROWS 17
static char tbuf[TROWS][TCOLS];
static int  tcx,tcy;

static void term_clear(void){ for(int r=0;r<TROWS;r++)for(int c=0;c<TCOLS;c++)tbuf[r][c]=' '; tcx=tcy=0; }
static void term_scroll(void){ for(int r=0;r<TROWS-1;r++)for(int c=0;c<TCOLS;c++)tbuf[r][c]=tbuf[r+1][c]; for(int c=0;c<TCOLS;c++)tbuf[TROWS-1][c]=' '; tcy=TROWS-1; }
static void term_nl(void){ tcx=0; if(++tcy>=TROWS) term_scroll(); }
static void term_putc(char c){
    if(c=='\n'){ term_nl(); return; }
    if(c=='\b'){ if(tcx>0){tcx--; tbuf[tcy][tcx]=' ';} return; }
    if(c<32) return;
    tbuf[tcy][tcx]=c; if(++tcx>=TCOLS) term_nl();
}
static void term_puts(const char *s){ for(;*s;s++) term_putc(*s); }

/* rend le terminal dans le corps de la fenêtre (x,y = coin du corps, w,h = taille corps) */
static void term_render(int x,int y,int w,int h){
    rect(x,y,w,h,rgb(0x05,0x07,0x0c));                       /* fond terminal */
    int pad=8;
    unsigned green=rgb(0x95,0xe6,0xcb), white=rgb(0xe8,0xee,0xff);
    for(int r=0;r<TROWS;r++){
        int py=y+pad+r*FONT_H; if(py+FONT_H>y+h) break;
        for(int c=0;c<TCOLS;c++){ char ch=tbuf[r][c]; if(ch!=' ') chr_(x+pad+c*FONT_W,py,ch, r==tcy?white:green); }
    }
    /* curseur bloc */
    int cxp=x+pad+tcx*FONT_W, cyp=y+pad+tcy*FONT_H;
    if(cyp+FONT_H<=y+h) rect(cxp,cyp+FONT_H-2,FONT_W,2,rgb(0x5c,0xcf,0xe6));
}

/* ============ shell (dans la boucle du compositeur) ============ */
static char cmd[128]; static int cmdn;
static unsigned long g_cmds;

static void prompt(void){ term_puts("prism:/ $ "); }

static void shell_exec(void){
    cmd[cmdn]=0;
    /* trace série pour l'automatisation */
    logs("[term] cmd: "); logs(cmd); logs("\n");
    g_cmds++;
    if(cmd[0]==0){ }
    else if(starts(cmd,"help")){ term_puts("commandes: help echo ps clear uname about ver\n"); }
    else if(starts(cmd,"echo")){ const char*a=cmd; while(*a&&*a!=' ')a++; if(*a==' ')a++; term_puts(a); term_putc('\n'); }
    else if(starts(cmd,"ps")){ term_puts("  PID  DOMAINE  CMD\n  0    COORD    axis\n  100  NODE-L   compositor\n  200  NODE-W   hello.exe\n"); }
    else if(starts(cmd,"clear")){ term_clear(); }
    else if(starts(cmd,"uname")){ term_puts("Prism OS / Axis kernel (multikernel asymetrique x86_64)\n"); }
    else if(starts(cmd,"ver")){ term_puts("Prism 0.11 - Axis coordinator\n"); }
    else if(starts(cmd,"about")){ term_puts("Prism : Node-L POSIX + Node-W NT, IPC lock-free,\nW^X, hot-restart, GUI en ring 3.\n"); }
    else { term_puts("commande inconnue: "); term_puts(cmd); term_putc('\n'); }
    cmdn=0;
    prompt();
}

static void shell_key(char c){
    if(c=='\n'||c=='\r'){ term_putc('\n'); shell_exec(); }
    else if(c=='\b'||c==127){ if(cmdn>0){ cmdn--; term_putc('\b'); } }
    else if((unsigned char)c>=32 && (unsigned char)c<127){ if(cmdn<127){ cmd[cmdn++]=c; term_putc(c); } }
}

/* ============ fenêtres ============ */
#define TITLE_H 26
#define NWIN 3
#define KIND_INFO 0
#define KIND_TERM 1
struct win { int x,y,w,h; const char *title; unsigned accent; int kind; const char *body[4]; };
static struct win wins[NWIN]={
    {60,80,448,322,"Terminal",0x5ccfe6,KIND_TERM,{0,0,0,0}},
    {540,110,330,200,"Fichiers",0x95e6cb,KIND_INFO,{"/  bin  etc  home","kernel.elf","hello_pe.exe",""}},
    {560,360,340,220,"Systeme",0xa3d4ff,KIND_INFO,{"Node-L : POSIX (ring 3)","Node-W : NT-compat","IPC lock-free + IOMMU","W^X, hot-restart"}},
};
static int zorder[NWIN]={1,2,0};   /* Terminal (0) au premier plan */
static int TERMWIN=0;

static void bring_front(int wi){ int pos=0; for(int i=0;i<NWIN;i++) if(zorder[i]==wi)pos=i; for(int i=pos;i<NWIN-1;i++)zorder[i]=zorder[i+1]; zorder[NWIN-1]=wi; }
static int  front_win(void){ return zorder[NWIN-1]; }

static void draw_window(struct win *w,int focused){
    unsigned body=rgb(0x13,0x17,0x22),dim=rgb(0x77,0x84,0xa5),white=rgb(0xe8,0xee,0xff);
    rect(w->x+4,w->y+4,w->w,w->h,rgb(0x02,0x03,0x06));                 /* ombre */
    rect(w->x,w->y,w->w,TITLE_H,w->accent);                           /* barre de titre */
    if(w->kind==KIND_TERM) term_render(w->x,w->y+TITLE_H,w->w,w->h-TITLE_H);
    else { rect(w->x,w->y+TITLE_H,w->w,w->h-TITLE_H,body);
           for(int i=0;i<4;i++) if(w->body[i]&&w->body[i][0]) str_(w->x+14,w->y+TITLE_H+14+i*20,w->body[i], i==0?white:dim); }
    rect_outline(w->x,w->y,w->w,w->h, focused?white:rgb(0x30,0x38,0x50));
    rect(w->x+w->w-18,w->y+8,10,10,rgb(0xf2,0x87,0x79));              /* bouton fermer */
    str_(w->x+12,w->y+5,w->title,rgb(0x0a,0x0e,0x14));
}

void gui_main(void){
    struct fb_info_user fi; sys_fb_info(&fi);
    if(!fi.w||fi.w>MAXW||fi.h>MAXH){ logs("[gui] framebuffer indisponible\n"); return; }
    SW=fi.w; SH=fi.h;
    logs("[gui] compositeur+terminal ("); log_uint(SW); logs("x"); log_uint(SH); logs(")\n");

    term_clear();
    term_puts("Prism terminal (ring 3) - tape 'help'\n");
    prompt();

    struct mouse_user m={0}; unsigned prev_btn=0;
    int drag=-1,dox=0,doy=0;
    unsigned long frames=0,keys=0;
    unsigned long start=rdtsc(), budget=30000000000ull;

    while(rdtsc()-start<budget){
        /* --- entrée clavier -> shell (si Terminal au premier plan) --- */
        char ch;
        while(sys_read(&ch,1)==1){
            keys++;
            if(front_win()==TERMWIN) shell_key(ch);
        }

        /* --- souris : drag des fenêtres --- */
        sys_mouse(&m); int mx=m.x,my=m.y; unsigned btn=m.buttons;
        int press=(btn&1)&&!(prev_btn&1), release=!(btn&1)&&(prev_btn&1);
        if(press){
            for(int i=NWIN-1;i>=0;i--){ struct win *w=&wins[zorder[i]];
                if(mx>=w->x&&mx<w->x+w->w&&my>=w->y&&my<w->y+w->h){
                    drag=zorder[i]; dox=mx-w->x; doy=my-w->y; bring_front(drag);
                    logs("[gui] focus '"); logs(w->title); logs("'\n"); break; } }
        }
        if(release) drag=-1;
        if(drag>=0){ int nx=mx-dox,ny=my-doy; if(nx<0)nx=0; if(ny<28)ny=28; if(nx>(int)SW-60)nx=(int)SW-60; if(ny>(int)SH-40)ny=(int)SH-40; wins[drag].x=nx; wins[drag].y=ny; }
        prev_btn=btn;

        /* --- rendu (double buffering) --- */
        for(unsigned y=0;y<SH;y+=2){ unsigned t=(y*36)/SH; rect(0,y,SW,2,rgb(0x0a + t/3, 0x0e + t/2, 0x1c + t)); }
        rect(0,0,SW,28,rgb(0x0d,0x11,0x1c));
        str_(14,6,"Prism",rgb(0x5c,0xcf,0xe6)); str_(14+6*FONT_W,6,"desktop",rgb(0x77,0x84,0xa5));
        str_(SW-160,6,"Node-L  ring3",rgb(0x77,0x84,0xa5));
        for(int i=0;i<NWIN;i++) draw_window(&wins[zorder[i]], i==NWIN-1);
        draw_cursor(mx,my);
        sys_fb_present(bb,(unsigned long)SW*SH*4);
        frames++;
        if((frames%60)==0){ logs("[gui] frames="); log_uint(frames); logs(" keys="); log_uint(keys); logs(" cmds="); log_uint(g_cmds); logs("\n"); }

        unsigned long f=rdtsc(); while(rdtsc()-f<5000000ull){}
    }
    logs("[gui] fin frames="); log_uint(frames); logs(" keys="); log_uint(keys); logs(" cmds="); log_uint(g_cmds); logs("\n");
    sys_exit(0);
}
