/* NEXUS-OS Node-L — ordonnanceur coopératif.
 * Changement de contexte réel via switch_context (asm) : sauvegarde/restaure les
 * registres callee-saved et bascule rsp. Cadre initial forgé pour démarrer sur task_trampoline. */
#include "sched.h"
#include "kc/string.h"

extern void switch_context(u64 *save_old_rsp, u64 new_rsp);

enum task_state { TASK_UNUSED = 0, TASK_READY, TASK_DONE };

struct task {
    u64             rsp;
    enum task_state state;
    task_fn         fn;
    u8              stack[SCHED_STACK_SIZE] __attribute__((aligned(16)));
};

static struct task tasks[SCHED_MAX_TASKS];
static int          n_tasks;
static int          cur = 0;        /* index 0 = contexte principal (node_l_main) */

void sched_init(void) {
    memset(tasks, 0, sizeof(tasks));
    tasks[0].state = TASK_READY;    /* le contexte courant */
    tasks[0].fn    = 0;
    n_tasks = 1;
    cur = 0;
}

static void task_trampoline(void) {
    tasks[cur].fn();                /* exécute la tâche */
    tasks[cur].state = TASK_DONE;   /* terminée -> l'ordonnanceur ne la reprendra plus */
    for (;;) sched_yield();         /* rend la main définitivement */
}

int sched_spawn(task_fn fn) {
    if (n_tasks >= SCHED_MAX_TASKS) return -1;
    int id = n_tasks++;
    struct task *t = &tasks[id];
    t->fn    = fn;
    t->state = TASK_READY;

    /* Cadre initial : [r15][r14][r13][r12][rbp][rbx][retaddr=task_trampoline] */
    u64 *sp = (u64 *)(t->stack + SCHED_STACK_SIZE);
    *--sp = (u64)(uintptr_t)task_trampoline;  /* adresse de retour du 1er switch */
    *--sp = 0;  /* rbx */
    *--sp = 0;  /* rbp */
    *--sp = 0;  /* r12 */
    *--sp = 0;  /* r13 */
    *--sp = 0;  /* r14 */
    *--sp = 0;  /* r15 */
    t->rsp = (u64)(uintptr_t)sp;
    return id;
}

static int pick_next(void) {
    for (int i = 1; i <= n_tasks; i++) {
        int c = (cur + i) % n_tasks;
        if (tasks[c].state == TASK_READY) return c;
    }
    return -1;
}

void sched_yield(void) {
    int next = pick_next();
    if (next < 0 || next == cur) return;    /* personne d'autre : on continue */
    int prev = cur;
    cur = next;
    switch_context(&tasks[prev].rsp, tasks[next].rsp);
}

void sched_run(void) {
    /* tourne tant qu'une tâche (hors contexte principal) est prête */
    for (;;) {
        bool any = false;
        for (int i = 1; i < n_tasks; i++)
            if (tasks[i].state == TASK_READY) { any = true; break; }
        if (!any) return;
        sched_yield();
    }
}
