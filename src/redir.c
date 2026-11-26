#include "redir.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
int redirs_apply(const Redir *r) {
    for (;r;r=r->next) {
        int flags=r->kind==T_IN?O_RDONLY:O_WRONLY|O_CREAT|(r->kind==T_OUT?O_TRUNC:O_APPEND);
        int fd; do { fd=open(r->path,flags,0666); } while (fd<0 && errno==EINTR);
        if (fd<0) { syserr(r->path); return -1; }
        int target=r->kind==T_IN?STDIN_FILENO:STDOUT_FILENO;
        if (fd!=target) {
            int result=duplicate(fd,target); int closed=close_fd(fd);
            if (result<0 || closed<0) return -1;
        }
    }
    return 0;
}
static int save(int fd) {
    int r; do { r=dup(fd); } while (r<0 && errno==EINTR);
    if (r<0) syserr("dup");
    return r;
}
int redirs_save(SavedFDs *s) {
    if (fflush(stdout)==EOF) { syserr("stdout"); clearerr(stdout); return -1; }
    s->input=save(STDIN_FILENO); s->output=-1;
    if (s->input<0) return -1;
    s->output=save(STDOUT_FILENO);
    if (s->output<0) { close_fd(s->input); return -1; }
    return 0;
}
int redirs_restore(SavedFDs *s) {
    int result=0;
    if (fflush(stdout)==EOF) { syserr("stdout"); result=-1; }
    if (duplicate(s->input,STDIN_FILENO)<0) result=-1;
    if (duplicate(s->output,STDOUT_FILENO)<0) result=-1;
    if (close_fd(s->input)<0) result=-1;
    if (close_fd(s->output)<0) result=-1;
    clearerr(stdout); return result;
}
