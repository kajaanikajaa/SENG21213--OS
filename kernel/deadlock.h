#ifndef DEADLOCK_H
#define DEADLOCK_H

/* Return non-zero when the registered mutex wait-for graph contains a cycle. */
int deadlock_detect(void);

/* Deterministic DFS demonstration used by the Stage 2 shell command. */
int deadlock_demo(void);

#endif
