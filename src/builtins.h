#ifndef BUILTINS_H
#define BUILTINS_H
#include "exec.h"
int is_builtin(const char *name);
int builtin(Shell *sh, char *const *argv, size_t argc);
#endif

