#ifndef REDIR_H
#define REDIR_H
#include "parser.h"
typedef struct { int input, output; } SavedFDs;
int redirs_apply(const Redir *r);
int redirs_save(SavedFDs *s);
int redirs_restore(SavedFDs *s);
#endif
