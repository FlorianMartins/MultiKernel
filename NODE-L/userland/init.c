/* NEXUS-OS Node-L userland `init` — s'exécute en RING 3, ne parle au noyau que par syscalls.
 * Freestanding, statique, lié dans la fenêtre user. Aucun accès direct au matériel. */
#include "syscall.h"

/* --- wrappers syscall (int 0x80) --- */
static long sys3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory", "rcx", "r11");
    return r;
}
static long sys_write(const char *s, unsigned long n) { return sys3(SYS_write, 1, (long)s, (long)n); }
static long sys_read(char *s, unsigned long n)        { return sys3(SYS_read, 0, (long)s, (long)n); }
static long sys_getpid(void)                          { return sys3(SYS_getpid, 0, 0, 0); }
static void sys_yield(void)                           { sys3(SYS_yield, 0, 0, 0); }
static void sys_exit(long code)                       { sys3(SYS_exit, code, 0, 0); }

static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static void puts_(const char *s) { sys_write(s, slen(s)); }

static void put_uint(unsigned long v) {
    char buf[24]; int i = 0;
    if (v == 0) { sys_write("0", 1); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    char out[24]; int j = 0;
    while (i > 0) out[j++] = buf[--i];
    sys_write(out, j);
}

/* Compare le début d'une ligne à un préfixe (mot-clé + espace ou fin). */
static int starts(const char *line, const char *kw) {
    unsigned long i = 0;
    while (kw[i]) { if (line[i] != kw[i]) return 0; i++; }
    return line[i] == 0 || line[i] == ' ' || line[i] == '\n';
}

void _start(void) {
#ifdef NODEL_GUI
    /* Mode graphique : lancer le compositeur au lieu du shell (Phase 11). */
    extern void gui_main(void);
    gui_main();
    sys_exit(0);
#endif
    puts_("\n");
    puts_("  +--------------------------------------------+\n");
    puts_("  |  Prism Node-L userland  (ring 3, POSIX)     |\n");
    puts_("  +--------------------------------------------+\n");
    puts_("[init] pid = ");
    put_uint((unsigned long)sys_getpid());
    puts_("\n");

    /* Bench syscall (Phase 7) : latence aller-retour ring3<->ring0 en cycles. */
    {
        unsigned long n = 20000, i;
        unsigned int lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        unsigned long t0 = ((unsigned long)hi << 32) | lo;
        for (i = 0; i < n; i++) (void)sys_getpid();
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        unsigned long t1 = ((unsigned long)hi << 32) | lo;
        puts_("[bench] syscall (getpid) : ");
        put_uint((t1 - t0) / n);
        puts_(" cycles/syscall\n");
    }

#ifdef NODEL_WX_TEST
    /* Durcissement W^X (Phase 6) : tenter d'exécuter du code depuis la pile (NX)
     * -> doit provoquer un #PF. Prouve que la pile user n'est pas exécutable. */
    puts_("[init] W^X test: executing from NX stack (expect #PF)...\n");
    {
        volatile unsigned char code[16];
        code[0] = 0xC3;                 /* opcode 'ret' */
        void (*fn)(void) = (void (*)(void))(void *)code;
        fn();                           /* -> #PF (NX) : ne revient pas */
        puts_("[init] W^X test FAILED: stack was executable!\n");
    }
#endif

    puts_("[init] mini-shell — commandes: help echo ps exit\n");

    /* Mini-shell BORNÉ : lit au plus N lignes via SYS_read (non bloquant), yield tant
     * que rien n'arrive (budget d'attente fini). Termine sur 'exit', EOF, ou N lignes
     * -> jamais de hang, même sans entrée (exécution automatisée). */
    char line[128];
    int max_lines = 48;
    int running = 1;

    while (running && max_lines-- > 0) {
        puts_("prism:/ $ ");

        unsigned long len = 0;
        long idle = 0; int got_input = 0;
        while (len < sizeof(line) - 1) {
            char ch;
            long r = sys_read(&ch, 1);
            if (r <= 0) {
                /* Fenêtre d'attente élargie (banc de test web : laisse le temps de
                 * taper). Les tests automatisés envoient 'exit' -> sortie immédiate. */
                if (++idle > 600000) break;
                sys_yield();
                continue;
            }
            got_input = 1; idle = 0;
            if (ch == '\n' || ch == '\r') break;
            line[len++] = ch;
        }
        line[len] = 0;

        if (!got_input) { puts_("\n[init] no more input\n"); break; }  /* EOF */
        puts_(line); puts_("\n");                                      /* écho */

        if (starts(line, "help")) {
            puts_("  help  - cette aide\n  echo X - affiche X\n  ps    - liste des taches\n  exit  - quitte\n");
        } else if (starts(line, "echo")) {
            const char *arg = line;
            while (*arg && *arg != ' ') arg++;
            if (*arg == ' ') arg++;
            puts_(arg); puts_("\n");
        } else if (starts(line, "ps")) {
            puts_("  PID  CMD\n  100  node-l\n  ---  init(ring3)\n");
        } else if (starts(line, "exit")) {
            puts_("[init] bye\n");
            running = 0;
        } else if (len == 0) {
            /* ligne vide : ignore */
        } else {
            puts_("  commande inconnue: "); puts_(line); puts_("\n");
        }
    }

    puts_("[init] exiting via SYS_exit(0)\n");
    sys_exit(0);
    for (;;) { }   /* sécurité : ne doit jamais être atteint */
}
