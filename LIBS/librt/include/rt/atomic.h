/* NEXUS-OS librt — barrières mémoire & primitives runtime (usage interne noeud/IPC).
 * S'appuie sur les builtins __atomic (portables hôte + freestanding). */
#pragma once

#include "kc/types.h"

/* Barrières C (ordonnancement compilateur + CPU selon le modèle x86 TSO). */
static inline void rt_barrier(void)  { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void rt_acquire(void)  { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
static inline void rt_release(void)  { __atomic_thread_fence(__ATOMIC_RELEASE); }

/* Barrières matérielles explicites. */
static inline void rt_sfence(void) { __asm__ volatile("sfence" ::: "memory"); }
static inline void rt_lfence(void) { __asm__ volatile("lfence" ::: "memory"); }
static inline void rt_mfence(void) { __asm__ volatile("mfence" ::: "memory"); }

static inline void rt_pause(void)  { __asm__ volatile("pause" ::: "memory"); }
