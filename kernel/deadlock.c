/*
 * Stage 2 extension: deadlock detection.
 *
 * A wait-for graph has one node per process. If process A waits for a mutex
 * owned by B, add an edge A -> B. A DFS back-edge proves that the graph has a
 * cycle, which is the graph form of a possible deadlock.
 */
#include "deadlock.h"
#include "mutex.h"
#include "process.h"

extern uint32_t mutex_registry_count(void);
extern mutex_t *mutex_registry_get(uint32_t index);

static int dfs(uint32_t node, uint8_t *visiting, uint8_t *done)
{
    uint32_t i, j;
    const pcb_t *p = process_table();
    uint32_t count = mutex_registry_count();

    if (visiting[node]) return 1;
    if (done[node]) return 0;
    visiting[node] = 1;

    for (i = 0; i < count; i++) {
        mutex_t *m = mutex_registry_get(i);
        if (!m || !m->owner) continue;
        for (j = 0; j < m->waiter_count; j++) {
            uint32_t w;
            if (!m->waiters[j]) continue;
            for (w = 0; w < MAX_PROCESSES; w++)
                if (&p[w] == m->waiters[j]) break;
            if (w >= MAX_PROCESSES) continue;
            if (m->owner == &p[node] && dfs(w, visiting, done)) return 1;
        }
    }

    visiting[node] = 0;
    done[node] = 1;
    return 0;
}

int deadlock_detect(void)
{
    uint8_t visiting[MAX_PROCESSES];
    uint8_t done[MAX_PROCESSES];
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) { visiting[i] = 0; done[i] = 0; }

    /* Follow waiter -> owner edges, so build DFS from each active PCB. */
    for (i = 0; i < MAX_PROCESSES; i++) {
        const pcb_t *p = process_table();
        uint32_t mindex, j;
        if (p[i].state == PROC_TERMINATED) continue;
        if (visiting[i] || done[i]) continue;
        visiting[i] = 1;
        for (mindex = 0; mindex < mutex_registry_count(); mindex++) {
            mutex_t *m = mutex_registry_get(mindex);
            if (!m || !m->owner) continue;
            for (j = 0; j < m->waiter_count; j++) {
                if (m->waiters[j] == &p[i]) {
                    uint32_t owner;
                    for (owner = 0; owner < MAX_PROCESSES; owner++)
                        if (&p[owner] == m->owner) break;
                    if (owner < MAX_PROCESSES && dfs(owner, visiting, done)) return 1;
                }
            }
        }
        visiting[i] = 0;
        done[i] = 1;
    }
    return 0;
}

/*
 * Pure graph test: this validates the DFS algorithm without creating a real
 * kernel deadlock that would leave scheduler threads blocked after the demo.
 */
int deadlock_demo(void)
{
    uint8_t graph[3][3] = {
        {0, 1, 0},
        {0, 0, 1},
        {1, 0, 0}
    };
    uint8_t state[3] = {0, 0, 0};
    uint32_t stack[3];
    uint32_t sp = 0;
    uint32_t start, next;

    for (start = 0; start < 3; start++) {
        if (state[start] != 0) continue;
        stack[sp++] = start;
        while (sp) {
            uint32_t node = stack[sp - 1];
            state[node] = 1;
            for (next = 0; next < 3; next++) {
                if (!graph[node][next]) continue;
                if (state[next] == 1) return 1;
                if (state[next] == 0) { stack[sp++] = next; break; }
            }
            if (next == 3) { state[node] = 2; sp--; }
        }
    }
    return 0;
}
