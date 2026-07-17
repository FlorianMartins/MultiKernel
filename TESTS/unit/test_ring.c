/* NEXUS-OS — test unitaire hôte du ring SPSC lock-free.
 * 2 threads (producteur/consommateur), 10^7 messages : 0 perte, ordre FIFO, checksum.
 * Compilé et exécuté sur l'HÔTE (pthreads) — voir `make test-unit`. */
#include "ring.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define N 10000000u   /* 10^7 messages */

static struct ipc_ring ring __attribute__((aligned(64)));
static volatile int    go;

static unsigned long long g_sum;
static int                g_order_ok = 1;
static unsigned           g_got;

static void *producer(void *arg) {
    (void)arg;
    while (!go) { }
    struct ipc_msg m;
    memset(&m, 0, sizeof(m));
    for (u32 i = 0; i < N; i++) {
        m.seq = i;
        m.len = 4;
        memcpy(m.data, &i, 4);
        while (!ipc_ring_push(&ring, &m)) { /* plein : busy-wait, jamais bloquant */ }
    }
    return 0;
}

static void *consumer(void *arg) {
    (void)arg;
    while (!go) { }
    struct ipc_msg m;
    for (u32 i = 0; i < N; i++) {
        while (!ipc_ring_pop(&ring, &m)) { /* vide : réessaie */ }
        if (m.seq != i) g_order_ok = 0;
        g_sum += m.seq;
        g_got++;
    }
    return 0;
}

int main(void) {
    ipc_ring_init(&ring);

    /* Sanity ABI */
    if (ring.magic != IPC_RING_MAGIC || sizeof(struct ipc_msg) != IPC_SLOT_SIZE) {
        printf("FAIL: ABI ring incohérente\n");
        return 1;
    }

    pthread_t p, c;
    struct timespec t0, t1;
    pthread_create(&c, 0, consumer, 0);
    pthread_create(&p, 0, producer, 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    go = 1;
    pthread_join(p, 0);
    pthread_join(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    unsigned long long expect = (unsigned long long)(N - 1) * N / 2ull;
    int ok = g_order_ok && g_got == N && g_sum == expect;

    printf("messages=%u  got=%u  order_ok=%d  sum=%llu  expect=%llu\n",
           N, g_got, g_order_ok, g_sum, expect);
    printf("debit=%.1f M msg/s  (%.3fs)\n", (N / 1e6) / secs, secs);
    printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
