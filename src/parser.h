
#ifndef PARSER_H
#define PARSER_H
#include "lexer.h"
typedef struct Redir { TokenKind kind; char *path; struct Redir *next; } Redir;
typedef enum { N_SIMPLE, N_SUB, N_PIPE, N_AND, N_OR, N_LIST, N_ASYNC } NodeKind;
typedef struct Node {
    NodeKind kind;
    struct Node *left, *right;
    char **argv;
    size_t argc;
    Redir *redirs;
} Node;
Node *parse(const Tokens *tokens, int *status);
void node_free(Node *n);
void node_dump(const Node *n, int depth);
char *node_label(const Node *n);
#endif
