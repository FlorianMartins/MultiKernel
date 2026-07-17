/* NEXUS-OS Node-L — ordonnanceur coopératif minimal (round-robin). */
#pragma once

#include "kc/types.h"

#define SCHED_MAX_TASKS 8
#define SCHED_STACK_SIZE 8192

typedef void (*task_fn)(void);

void sched_init(void);
int  sched_spawn(task_fn fn);     /* crée une tâche ; renvoie son index */
void sched_yield(void);           /* cède la main à la tâche suivante prête */
void sched_run(void);             /* boucle jusqu'à ce que toutes les tâches créées finissent */
