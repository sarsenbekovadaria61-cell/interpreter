#include "jobs.h"
#include "signals.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
int terminal_group(int tty, pid_t pgid) {
    int r; do { r = tcsetpgrp(tty, pgid); } while (r < 0 && errno == EINTR);
    if (r < 0) syserr("tcsetpgrp");
    return r;
}
int jobs_init(Jobs *j, int interactive) {
    *j = (Jobs){0}; j->interactive = interactive; j->tty = -1;
    if (!interactive) return signals_shell(0);
    do { j->tty = dup(STDIN_FILENO); } while (j->tty < 0 && errno == EINTR);
    if (j->tty < 0) { syserr("dup terminal"); return -1; }
    if (fcntl(j->tty,F_SETFD,FD_CLOEXEC)<0) { syserr("fcntl terminal"); close_fd(j->tty); return -1; }
    pid_t fg;
    while ((fg = tcgetpgrp(j->tty)) != getpgrp()) {
        if (fg < 0) { syserr("tcgetpgrp"); return -1; }
        if (kill(-getpgrp(), SIGTTIN) < 0) { syserr("kill"); return -1; }
    }
    if (signals_shell(1) < 0) return -1;
    j->shell_pgid = getpid();
    if (setpgid(0, j->shell_pgid) < 0 && !(errno == EPERM && getpgrp() == getpid())) {
        syserr("setpgid"); return -1;
    }
    if (terminal_group(j->tty, j->shell_pgid) < 0) return -1;
    if (tcgetattr(j->tty, &j->modes) < 0) { syserr("tcgetattr"); return -1; }
    return 0;
}
int status_code(int s) {
    if (WIFEXITED(s)) return WEXITSTATUS(s);
    if (WIFSIGNALED(s)) return 128 + WTERMSIG(s);
    if (WIFSTOPPED(s)) return 128 + WSTOPSIG(s);
    return 0;
}
JobState job_state(const Job *j) {
    int stopped = 0;
    for (size_t i=0; i<j->count; ++i) {
        if (j->processes[i].state == RUNNING) return RUNNING;
        if (j->processes[i].state == STOPPED) stopped = 1;
    }
    return stopped ? STOPPED : DONE;
}
static void update(Jobs *j, pid_t pid, int s) {
    for (Job *x=j->head; x; x=x->next) for (size_t i=0; i<x->count; ++i)
        if (x->processes[i].pid == pid) {
            JobState before = job_state(x);
            x->processes[i].status = s;
            x->processes[i].state = WIFSTOPPED(s) ? STOPPED : WIFCONTINUED(s) ? RUNNING : DONE;
            if (job_state(x) != before) x->notified = 0;
            if (job_state(x) == STOPPED && before != STOPPED) x->order = ++j->clock;
            return;
        }
}
Job *job_add(Jobs *j, pid_t pgid, const pid_t *pids, size_t n, const char *label, int bg) {
    for (Job *x=j->head,*next;x;x=next) {
        next=x->next;
        if (job_state(x)==DONE && x->notified) job_remove(j,x);
    }
    int id = 1;
    for (;;) {
        int used = 0; for (Job *x=j->head; x; x=x->next) if (x->id == id) used = 1;
        if (!used) break;
        ++id;
    }
    Job *x = xcalloc(1,sizeof(*x)); x->id=id; x->pgid=pgid; x->count=n; x->background=bg;
    x->label=xstrdup(label); x->processes=xcalloc(n,sizeof(Process));
    for (size_t i=0; i<n; ++i) x->processes[i]=(Process){pids[i],RUNNING,0};
    if (bg) x->order=++j->clock;
    x->next=j->head; j->head=x; return x;
}
void job_remove(Jobs *j, Job *x) {
    Job **p=&j->head; while (*p && *p != x) p=&(*p)->next;
    if (*p) *p=x->next;
    free(x->processes); free(x->label); free(x);
}
static Job *ranked(Jobs *j, int previous) {
    Job *first=NULL,*second=NULL;
    for (Job *x=j->head; x; x=x->next) if (job_state(x)!=DONE && x->order) {
        if (!first || x->order>first->order) { second=first; first=x; }
        else if (!second || x->order>second->order) second=x;
    }
    return previous ? second : first;
}
static void print_job(Jobs *j, Job *x) {
    const char *names[]={"Running","Stopped","Done"};
    char marker=x==ranked(j,0)?'+':x==ranked(j,1)?'-':' ';
    printf("[%d]%c  %-22s %s%s\n",x->id,marker,names[job_state(x)],x->label,x->background?" &":"");
}
void jobs_poll(Jobs *j, int notify) {
    (void)signals_pending();
    for (;;) {
        int s; pid_t pid=waitpid(-1,&s,WNOHANG|WUNTRACED|WCONTINUED);
        if (pid>0) update(j,pid,s);
        else if (pid<0 && errno==EINTR) continue;
        else { if (pid<0 && errno!=ECHILD) syserr("waitpid"); break; }
    }
    for (Job *x=j->head; x; x=x->next) if (notify && !x->notified && job_state(x)!=RUNNING) {
        print_job(j,x); x->notified=1;
    }
}
void jobs_print(Jobs *j) {
    jobs_poll(j,0);
    for (Job *x=j->head,*next; x; x=next) { next=x->next; print_job(j,x); if (job_state(x)==DONE) job_remove(j,x); }
}
Job *jobs_find(Jobs *j, const char *spec) {
    if (!spec || !strcmp(spec,"%+") || !strcmp(spec,"%%")) return ranked(j,0);
    if (!strcmp(spec,"%-")) return ranked(j,1);
    char *end; errno=0; long n=strtol(spec+(spec[0]=='%'),&end,10);
    if (errno || *end || end==spec+(spec[0]=='%') || n<=0) return NULL;
    for (Job *x=j->head; x; x=x->next) {
        if (spec[0]=='%' && x->id==n) return x;
        if (spec[0]!='%') for (size_t i=0;i<x->count;++i) if (x->processes[i].pid==n) return x;
    }
    return NULL;
}
static int modes_set(int tty, const struct termios *m) {
    int r; do { r=tcsetattr(tty,TCSADRAIN,m); } while (r<0 && errno==EINTR);
    if (r<0) syserr("tcsetattr");
    return r;
}
int job_wait(Jobs *j, Job *x, int foreground) {
    if (foreground && j->interactive && terminal_group(j->tty,x->pgid)<0) return 1;
    while (job_state(x)==RUNNING) {
        int s; pid_t pid=waitpid(-x->pgid,&s,WUNTRACED|WCONTINUED);
        if (pid>0) update(j,pid,s);
        else if (pid<0 && errno==EINTR) continue;
        else { if (errno!=ECHILD) syserr("waitpid"); break; }
    }
    if (foreground && j->interactive) {
        if (job_state(x)==STOPPED) {
            if (tcgetattr(j->tty,&x->modes)==0) x->has_modes=1; else syserr("tcgetattr");
        }
        if (terminal_group(j->tty,j->shell_pgid)<0) return 1;
        if (modes_set(j->tty,&j->modes)<0) return 1;
    }
    int result=status_code(x->processes[x->count-1].status);
    if (job_state(x)==DONE) job_remove(j,x);
    else { x->background=1; x->order=++j->clock; }
    return result;
}
int job_continue(Jobs *j, Job *x, int foreground) {
    if (!x) { error(foreground?"fg":"bg","no such job"); return 1; }
    if (foreground) {
        printf("%s\n",x->label); fflush(stdout);
        if (terminal_group(j->tty,x->pgid)<0) return 1;
        if (x->has_modes && modes_set(j->tty,&x->modes)<0) return 1;
    }
    if (kill(-x->pgid,SIGCONT)<0) { syserr("SIGCONT");
        if (foreground) (void)terminal_group(j->tty,j->shell_pgid);
        return 1;
    }
    for (size_t i=0;i<x->count;++i) if (x->processes[i].state==STOPPED) x->processes[i].state=RUNNING;
    x->background=!foreground; x->order=++j->clock; x->notified=0;
    if (foreground) return job_wait(j,x,1);
    print_job(j,x); return 0;
}
int jobs_stopped(const Jobs *j) {
    for (Job *x=j->head;x;x=x->next) if (job_state(x)==STOPPED) return 1;
    return 0;
}
int jobs_wait_all(Jobs *j) {
    for (Job *x=j->head,*next;x;x=next) { next=x->next; (void)job_wait(j,x,0); }
    return 0;
}
void jobs_dispose(Jobs *j, int terminate) {
    while (j->head) {
        Job *x=j->head;
        if (terminate && job_state(x)!=DONE) {
            if (x->pgid != getpgrp()) {
                if (kill(-x->pgid,SIGKILL)<0 && errno!=ESRCH) syserr("kill");
            } else {
                for (size_t i=0;i<x->count;++i) if (x->processes[i].state!=DONE)
                    if (kill(x->processes[i].pid,SIGKILL)<0 && errno!=ESRCH) syserr("kill");
            }
            for (size_t i=0;i<x->count;++i) if (x->processes[i].state!=DONE) {
                int s; pid_t r; do { r=waitpid(x->processes[i].pid,&s,0); } while (r<0 && errno==EINTR);
                if (r<0 && errno!=ECHILD) syserr("waitpid");
            }
        }
        job_remove(j,x);
    }
    if (j->tty >= 0) { close_fd(j->tty); j->tty = -1; }
}
void jobs_child_reset(Jobs *j) {
    jobs_dispose(j,0); j->interactive=0; j->clock=0;
}
