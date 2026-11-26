#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t s);
char *xstrdup(const char *s);
void syserr(const char *what);
void error(const char *what, const char *message);
int close_fd(int fd);
int duplicate(int from, int to);
#endif
