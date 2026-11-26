#include "builtins.h"
#include "util.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int is_builtin(const char *s) {
    const char *names[]={"cd","pwd","echo","help","exit","jobs","fg","bg","kill","wait",NULL};
    for (size_t i=0;names[i];++i) if (!strcmp(s,names[i])) return 1;
    return 0;
}
static int number(const char *s, long *n) {
    char *end; errno=0; *n=strtol(s,&end,10);
    return *s && !*end && !errno;
}
static int signal_number(const char *s) {
    long n;
    if (number(s,&n)) return n>=0 && n<=INT_MAX?(int)n:-1;
    if (!strncmp(s,"SIG",3)) s+=3;
    const struct { const char *name; int sig; } names[]={
        {"HUP",SIGHUP},{"INT",SIGINT},{"QUIT",SIGQUIT},{"ILL",SIGILL},{"ABRT",SIGABRT},
        {"FPE",SIGFPE},{"KILL",SIGKILL},{"SEGV",SIGSEGV},{"PIPE",SIGPIPE},
        {"ALRM",SIGALRM},{"TERM",SIGTERM},{"USR1",SIGUSR1},{"USR2",SIGUSR2},
        {"CHLD",SIGCHLD},{"CONT",SIGCONT},{"STOP",SIGSTOP},{"TSTP",SIGTSTP},
        {"TTIN",SIGTTIN},{"TTOU",SIGTTOU},{"BUS",SIGBUS},{"TRAP",SIGTRAP},
        {"URG",SIGURG},{"XCPU",SIGXCPU},{"XFSZ",SIGXFSZ},{"VTALRM",SIGVTALRM},
        {"PROF",SIGPROF},{"WINCH",SIGWINCH},{"IO",SIGIO},{"SYS",SIGSYS}};
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(s,names[i].name)) return names[i].sig;
    return -1;
}
static int kill_builtin(Shell *sh, char *const *v, size_t n) {
    size_t i=1; int sig=SIGTERM, result=0;
    if (i<n && !strcmp(v[i],"-s")) {
        if (++i==n) { error("kill","signal required"); return 2; }
        sig=signal_number(v[i++]);
    } else if (i<n && v[i][0]=='-' && strcmp(v[i],"--")) sig=signal_number(v[i++]+1);
    if (i<n && !strcmp(v[i],"--")) ++i;
    if (sig<0 || i==n) { error("kill","invalid signal or missing target"); return 2; }
    for (;i<n;++i) {
        pid_t target;
        if (v[i][0]=='%') {
            Job *j=jobs_find(&sh->jobs,v[i]);
            if (!j || job_state(j)==DONE) { error("kill","no running job"); result=1; continue; }
            target=-j->pgid;
        } else {
            long value;
            if (!number(v[i],&value) || value<=0 || value>INT_MAX) {
                error("kill","expected positive PID or job specifier"); result=1; continue;
            }
            target=(pid_t)value;
        }
        if (kill(target,sig)<0) { syserr("kill"); result=1; }
    }
    return result;
}
int builtin(Shell *sh, char *const *v, size_t n) {
    const char *name=v[0];
    if (!strcmp(name,"cd")) {
        if (n>2) { error(name,"too many arguments"); return 1; }
        const char *path=n==2?v[1]:getenv("HOME");
        if (!path) { error(name,"HOME is not set"); return 1; }
        if (!strcmp(path,"-")) { error(name,"cd - is unsupported"); return 2; }
        if (chdir(path)<0) { syserr(name); return 1; }
        return 0;
    }
    if (!strcmp(name,"pwd")) {
        if (n!=1) { error(name,"arguments are unsupported"); return 2; }
        size_t size=256; char *buf=xcalloc(size,1);
        while (!getcwd(buf,size)) {
            if (errno!=ERANGE) { syserr(name); free(buf); return 1; }
            size*=2; buf=xrealloc(buf,size);
        }
        int r=puts(buf); free(buf); return r==EOF?1:0;
    }
    if (!strcmp(name,"echo")) {
        size_t start=n>1 && !strcmp(v[1],"-n")?2:1;
        for (size_t i=start;i<n;++i) {
            if (i>start && putchar(' ')==EOF) return 1;
            if (fputs(v[i],stdout)==EOF) return 1;
        }
        return start==1 && putchar('\n')==EOF?1:0;
    }
    if (!strcmp(name,"help")) {
        if (n!=1) { error(name,"arguments are unsupported"); return 2; }
        return puts("cd [directory]   change directory\npwd              print working directory\n"
                    "echo [-n] ...    print arguments\nhelp             show this help\n"
                    "exit [status]    leave shell\njobs             list jobs\n"
                    "fg [%n]          foreground job\nbg [%n]          resume background job\n"
                    "kill [-s signal | -signal] pid|%n ...\nwait [pid|%n]    wait for children")<0?1:0;
    }
    if (!strcmp(name,"exit")) {
        long value=sh->status;
        if (n>1 && !number(v[1],&value)) { error(name,"numeric argument required"); sh->exiting=1; return 2; }
        if (n>2) { error(name,"too many arguments"); return 1; }
        jobs_poll(&sh->jobs,0);
        if (sh->jobs.interactive && jobs_stopped(&sh->jobs) && !sh->exit_warned) {
            error(name,"there are stopped jobs; repeat exit to leave"); sh->exit_warned=1; return 1;
        }
        sh->exiting=1; return (int)((unsigned long)value & 255UL);
    }
    if (!strcmp(name,"jobs")) {
        if (n!=1) { error(name,"arguments are unsupported"); return 2; }
        jobs_print(&sh->jobs); return 0;
    }
    if (!strcmp(name,"kill")) return kill_builtin(sh,v,n);
    if (!strcmp(name,"fg") || !strcmp(name,"bg")) {
        if (!sh->jobs.interactive) { error(name,"job control is disabled"); return 1; }
        if (n>2 || (n==2 && v[1][0]!='%')) { error(name,"expected a job specifier"); return 2; }
        jobs_poll(&sh->jobs,0);
        return job_continue(&sh->jobs,jobs_find(&sh->jobs,n==2?v[1]:NULL),!strcmp(name,"fg"));
    }
    if (!strcmp(name,"wait")) {
        if (n>2) { error(name,"too many arguments"); return 2; }
        if (n==1) return jobs_wait_all(&sh->jobs);
        Job *j=jobs_find(&sh->jobs,v[1]);
        if (!j) { error(name,"not a child or known job"); return 127; }
        return job_wait(&sh->jobs,j,0);
    }
    return 127;
}
