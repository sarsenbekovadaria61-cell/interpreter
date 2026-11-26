#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) { syserr("calloc"); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t s) {
    void *q = realloc(p, s);
    if (!q) { syserr("realloc"); exit(1); }
    return q;
}
char *xstrdup(const char *s) {
    char *p = xcalloc(strlen(s) + 1, 1);
    strcpy(p, s);
    return p;
}
void error(const char *what, const char *message) {
    fprintf(stderr, "mysh: %s: %s\n", what, message);
}
void syserr(const char *what) { error(what, strerror(errno)); }
int close_fd(int fd) {
    if (fd < 0) return 0;
    if (close(fd) < 0 && errno != EINTR) { syserr("close"); return -1; }
    return 0;
}
int duplicate(int from, int to) {
    int r;
    do { r = dup2(from, to); } while (r < 0 && errno == EINTR);
    if (r < 0) syserr("dup2");
    return r;
}
