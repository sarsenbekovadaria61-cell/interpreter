#ifndef EXEC_H
#define EXEC_H
#include "jobs.h"
#include "parser.h"
typedef struct {
    Jobs jobs;
    int status, exiting, exit_warned;
    pid_t group;
} Shell;
int execute(Shell *sh, const Node *node);
#endif
