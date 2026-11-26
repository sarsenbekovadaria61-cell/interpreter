#ifndef JOBS_H
#define JOBS_H
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>
typedef enum { RUNNING, STOPPED, DONE } JobState;
typedef struct { pid_t pid; JobState state; int status; } Process;
typedef struct Job {
    int id, background, notified;
    unsigned long order;
    pid_t pgid;
    Process *processes;
    size_t count;
    char *label;
    struct termios modes;
    int has_modes;
    struct Job *next;
} Job;
typedef struct {
    Job *head;
    unsigned long clock;
    int interactive, tty;
    pid_t shell_pgid;
    struct termios modes;
} Jobs;
int jobs_init(Jobs *j, int interactive);
Job *job_add(Jobs *j, pid_t pgid, const pid_t *pids, size_t n, const char *label, int bg);
JobState job_state(const Job *j);
void jobs_poll(Jobs *j, int notify);
void jobs_print(Jobs *j);
Job *jobs_find(Jobs *j, const char *spec);
void job_remove(Jobs *j, Job *job);
int job_wait(Jobs *j, Job *job, int foreground);
int job_continue(Jobs *j, Job *job, int foreground);
int jobs_stopped(const Jobs *j);
int jobs_wait_all(Jobs *j);
void jobs_dispose(Jobs *j, int terminate);
void jobs_child_reset(Jobs *j);
int terminal_group(int tty, pid_t pgid);
int status_code(int status);
#endif
