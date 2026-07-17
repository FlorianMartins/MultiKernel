/* NEXUS-OS COORDINATOR — micro-benchmarks TSC.
 * Mesure les chemins chauds : ring SPSC, memcpy/memset. Résultats en cycles/op. */
#include "bench.h"
#include "kc/cpu.h"
#include "kc/string.h"
#include "serial.h"
#include "ring.h"

/* Barrière de sérialisation autour de rdtsc (évite le réordonnancement OoO). */
static inline u64 tsc_serial(void) {
    u32 a, b, c, d;
    cpuid_raw(0, &a, &b, &c, &d);   /* cpuid = barrière sérialisante */
    return rdtsc();
}

static struct ipc_ring bench_ring __attribute__((aligned(64)));
static u8 bench_src[65536] __attribute__((aligned(64)));
static u8 bench_dst[65536] __attribute__((aligned(64)));

static void bench_ring_op(void) {
    ipc_ring_init(&bench_ring);
    struct ipc_msg m, out;
    memset(&m, 0, sizeof(m));
    m.len = 4;

    const u32 N = 200000;
    u64 t0 = tsc_serial();
    for (u32 i = 0; i < N; i++) {
        m.seq = i;
        ipc_ring_push(&bench_ring, &m);   /* SPSC : push puis pop immédiat (mono-cœur) */
        ipc_ring_pop(&bench_ring, &out);
    }
    u64 t1 = tsc_serial();
    u64 cyc = (t1 - t0) / N;
    serial_printf("[bench] ring SPSC push+pop : %lu cycles/op  (N=%u)\n", cyc, N);
}

static void bench_memcpy(void) {
    for (u32 i = 0; i < sizeof(bench_src); i++) bench_src[i] = (u8)i;

    const u32 N = 4000;
    const u32 sz = sizeof(bench_src);   /* 64 Kio */
    u64 t0 = tsc_serial();
    for (u32 i = 0; i < N; i++) memcpy(bench_dst, bench_src, sz);
    u64 t1 = tsc_serial();
    /* cycles pour 64 Kio, ramenés au Kio */
    u64 cyc_per_kib = (t1 - t0) / (N * (sz / 1024));
    serial_printf("[bench] memcpy : %lu cycles/Kio  (N=%u x %u Kio)\n", cyc_per_kib, N, sz / 1024);
}

static void bench_memset(void) {
    const u32 N = 4000;
    const u32 sz = sizeof(bench_dst);
    u64 t0 = tsc_serial();
    for (u32 i = 0; i < N; i++) memset(bench_dst, i & 0xFF, sz);
    u64 t1 = tsc_serial();
    u64 cyc_per_kib = (t1 - t0) / (N * (sz / 1024));
    serial_printf("[bench] memset : %lu cycles/Kio  (N=%u x %u Kio)\n", cyc_per_kib, N, sz / 1024);
}

void bench_run(void) {
    serial_printf("\n[bench] --- micro-benchmarks (cycles/op, TSC) ---\n");
    serial_printf("[bench] NB: sous QEMU/TCG le timing n'est PAS représentatif du matériel\n");
    serial_printf("[bench]     (rep movsb émulé en boucle, cohérence de cache absente).\n");
    serial_printf("[bench]     Mesure réelle du ring : bench hôte natif (make test-unit).\n");
    bench_ring_op();
    bench_memcpy();
    bench_memset();
    serial_printf("[bench] --- fin ---\n");
}
