/* NEXUS-OS — benchmark ring SPSC sur MATÉRIEL RÉEL (hôte), pas sous TCG.
 * Mesure le débit cross-cœur (2 threads) et la latence (cycles/op). C'est la mesure
 * de perf autoritaire du ring (vraie cohérence de cache, vrai ordonnancement). */
#include "ring.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define N 20000000u   /* 2·10^7 messages */

static struct ipc_ring ring __attribute__((aligned(64)));
static volatile int go;
static volatile unsigned long long g_sum;
static volatile uint64_t g_cons_cycles;

static void *producer(void *a) {
    (void)a;
    while (!go) { }
    struct ipc_msg m; memset(&m, 0, sizeof(m)); m.len = 4;
    for (uint32_t i = 0; i < N; i++) { m.seq = i; while (!ipc_ring_push(&ring, &m)) { } }
    return 0;
}
static void *consumer(void *a) {
    (void)a;
    while (!go) { }
    struct ipc_msg m; unsigned long long s = 0;
    uint64_t t0 = rdtsc();
    for (uint32_t i = 0; i < N; i++) { while (!ipc_ring_pop(&ring, &m)) { } s += m.seq; }
    g_cons_cycles = rdtsc() - t0;
    g_sum = s;
    return 0;
}

int main(void) {
    ipc_ring_init(&ring);
    pthread_t p, c;
    struct timespec t0, t1;
    pthread_create(&c, 0, consumer, 0);
    pthread_create(&p, 0, producer, 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    go = 1;
    pthread_join(p, 0); pthread_join(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    unsigned long long expect = (unsigned long long)(N - 1) * N / 2ull;
    int ok = (g_sum == expect);
    printf("throughput = %.1f M msg/s   consumer = %.1f cycles/msg   [%s]\n",
           (N / 1e6) / secs, (double)g_cons_cycles / N, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
