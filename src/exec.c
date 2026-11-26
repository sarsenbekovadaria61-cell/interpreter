#include "exec.h"
#include "builtins.h"
#include "redir.h"
#include "signals.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

static int external(char *const *argv) {
    if (!argv[0][0]) { error("command","empty command name"); return 127; }
    if (strchr(argv[0],'/')) {
        execve(argv[0],argv,environ);
        int e=errno; syserr(argv[0]); return e==ENOENT || e==ENOTDIR?127:126;
    }
    const char *path=getenv("PATH"); if (!path) path="/bin:/usr/bin";
    int failure=ENOENT;
    for (const char *p=path;;) {
        const char *end=strchr(p,':'); size_t len=end?(size_t)(end-p):strlen(p);
        char *file=xcalloc(len+strlen(argv[0])+3,1);
        if (len) { memcpy(file,p,len); file[len]='/'; strcpy(file+len+1,argv[0]); }
        else strcpy(file,argv[0]);
        execve(file,argv,environ); int e=errno; free(file);
        if (e!=ENOENT && e!=ENOTDIR) failure=e;
        if (e!=ENOENT && e!=ENOTDIR && e!=EACCES) break;
        if (!end) break;
        p=end+1;
    }
    errno=failure; syserr(argv[0]); return failure==ENOENT || failure==ENOTDIR?127:126;
}
static size_t pipeline_count(const Node *n) {
    return n->kind==N_PIPE?pipeline_count(n->left)+pipeline_count(n->right):1;
}
static void flatten(const Node *n, const Node **parts, size_t *i) {
    if (n->kind==N_PIPE) { flatten(n->left,parts,i); flatten(n->right,parts,i); }
    else parts[(*i)++]=n;
}
static int set_group(pid_t pid, pid_t group) {
    if (setpgid(pid,group)<0 && errno!=EACCES && errno!=ESRCH) { syserr("setpgid"); return -1; }
    return 0;
}
static void child_run(Shell *sh, const Node *n) {
    int result;
    if (redirs_apply(n->redirs)<0) result=1;
    else if (n->kind==N_SIMPLE) result=is_builtin(n->argv[0])?builtin(sh,n->argv,n->argc):external(n->argv);
    else if (n->kind==N_SUB) result=execute(sh,n->left);
    else result=execute(sh,n);
    if (fflush(stdout)==EOF) result=1;
    jobs_dispose(&sh->jobs,1);
    if (close_fd(STDIN_FILENO)<0) result=1;
    if (close_fd(STDOUT_FILENO)<0) result=1;
    _exit(result);
}
static int launch(Shell *sh, const Node *n, int bg) {
    size_t count=pipeline_count(n), idx=0, started=0;
    const Node **parts=xcalloc(count,sizeof(*parts)); flatten(n,parts,&idx);
    pid_t *pids=xcalloc(count,sizeof(*pids));
    pid_t group=sh->group;
    int previous=-1, gate[2];
    if (pipe(gate)<0) { syserr("pipe"); free(parts); free(pids); return 1; }
    if (fflush(NULL)==EOF) { syserr("fflush"); close_fd(gate[0]); close_fd(gate[1]); free(parts); free(pids); return 1; }
    int failed=0;
    for (size_t i=0;i<count;++i) {
        int channel[2]={-1,-1};
        if (i+1<count && pipe(channel)<0) { syserr("pipe"); failed=1; break; }
        pid_t pid=fork();
        if (pid<0) { syserr("fork"); close_fd(channel[0]); close_fd(channel[1]); failed=1; break; }
        if (pid==0) {
            pid_t child_group=group?group:getpid();
            if (setpgid(0,child_group)<0) { syserr("setpgid"); _exit(126); }
            if (signals_child()<0) _exit(126);
            close_fd(gate[1]);
            char byte; ssize_t r;
            do { r=read(gate[0],&byte,1); } while (r<0 && errno==EINTR);
            if (r<0) { syserr("read"); _exit(126); }
            close_fd(gate[0]);
            if (previous>=0 && duplicate(previous,STDIN_FILENO)<0) _exit(126);
            if (channel[1]>=0 && duplicate(channel[1],STDOUT_FILENO)<0) _exit(126);
            close_fd(previous); close_fd(channel[0]); close_fd(channel[1]);
            if (bg && !sh->jobs.interactive && i==0 && previous<0) {
                int fd=open("/dev/null",O_RDONLY);
                if (fd<0) { syserr("/dev/null"); _exit(126); }
                if (duplicate(fd,STDIN_FILENO)<0) _exit(126);
                if (fd!=STDIN_FILENO) close_fd(fd);
            }
            jobs_child_reset(&sh->jobs); sh->group=child_group;
            const Node *part=parts[i];
            free(pids); free(parts);
            child_run(sh,part);
        }
        if (!group) group=pid;
        pids[started++]=pid;
        if (set_group(pid,group)<0) failed=1;
        close_fd(previous); close_fd(channel[1]); previous=channel[0];
        if (failed) break;
    }
    close_fd(previous); close_fd(gate[0]);
    if (!failed && !bg && sh->jobs.interactive && terminal_group(sh->jobs.tty,group)<0) failed=1;
    close_fd(gate[1]);
    if (failed) {
        for (size_t i=0;i<started;++i) if (kill(pids[i],SIGKILL)<0 && errno!=ESRCH) syserr("kill");
        for (size_t i=0;i<started;++i) {
            int s; pid_t r; do { r=waitpid(pids[i],&s,0); } while (r<0 && errno==EINTR);
            if (r<0 && errno!=ECHILD) syserr("waitpid");
        }
        if (sh->jobs.interactive) (void)terminal_group(sh->jobs.tty,sh->jobs.shell_pgid);
        free(parts); free(pids); return 1;
    }
    char *label=node_label(n);
    Job *job=job_add(&sh->jobs,group,pids,count,label,bg);
    free(label); free(parts); free(pids);
    if (bg) {
        if (sh->jobs.interactive) printf("[%d] %ld\n",job->id,(long)job->pgid);
        return 0;
    }
    return job_wait(&sh->jobs,job,1);
}
int execute(Shell *sh, const Node *n) {
    if (!n || sh->exiting) return sh->status;
    int r=sh->status;
    switch (n->kind) {
    case N_LIST:
        (void)execute(sh,n->left);
        r=sh->exiting?sh->status:execute(sh,n->right); break;
    case N_AND:
        r=execute(sh,n->left);
        if (!r && !sh->exiting) r=execute(sh,n->right);
        break;
    case N_OR:
        r=execute(sh,n->left);
        if (r && !sh->exiting) r=execute(sh,n->right);
        break;
    case N_ASYNC:
        r=launch(sh,n->left,1); break;
    case N_SIMPLE:
        if (is_builtin(n->argv[0])) {
            SavedFDs saved;
            if (!n->redirs) {
                r=builtin(sh,n->argv,n->argc);

                if (fflush(stdout)==EOF) { syserr("stdout"); r=1; }
            }
            else if (redirs_save(&saved)<0) r=1;
            else {
                r=redirs_apply(n->redirs)<0?1:builtin(sh,n->argv,n->argc);
                if (redirs_restore(&saved)<0) r=1;
            }
            break;
        }
    case N_PIPE: case N_SUB: r=launch(sh,n,0); break;
    }
    sh->status=r; return r;
}
