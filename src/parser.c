#include "parser.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const Tokens *t; size_t i; int failed; unsigned depth; } Parser;
static TokenKind peek(Parser *p) { return p->t->v[p->i].kind; }
static Node *node(NodeKind k, Node *a, Node *b) {
    Node *n = xcalloc(1, sizeof(*n)); n->kind = k; n->left = a; n->right = b; return n;
}
void node_free(Node *n) {
    if (!n) return;
    node_free(n->left); node_free(n->right);
    for (size_t i = 0; i < n->argc; ++i) free(n->argv[i]);
    free(n->argv);
    while (n->redirs) { Redir *r = n->redirs; n->redirs = r->next; free(r->path); free(r); }
    free(n);
}
static int isredir(TokenKind k) { return k == T_IN || k == T_OUT || k == T_APPEND; }
static void redir(Parser *p, Node *n) {
    TokenKind k = peek(p); ++p->i;
    if (peek(p) != T_WORD) { p->failed = 1; return; }
    const char *path = p->t->v[p->i++].text;
    if (!strncmp(path, "/dev/tcp/", 9) || !strncmp(path, "/dev/udp/", 9)) p->failed = 1;
    Redir *r = xcalloc(1, sizeof(*r)); r->kind = k; r->path = xstrdup(path);
    Redir **tail = &n->redirs; while (*tail) tail = &(*tail)->next; *tail = r;
}
static Node *list(Parser *p);
static int reserved(const Token *t) {
    const char *words[] = {"if","then","else","elif","fi","while","until","do","done",
                          "for","in","case","esac","function","!",
                          "export","set","unset","alias","unalias","trap",
                          "source",".","eval","exec","return","break","continue",NULL};
    if (t->quoted) return 0;
    for (size_t i = 0; words[i]; ++i) if (!strcmp(t->text, words[i])) return 1;
    return 0;
}
static Node *command(Parser *p) {
    if (++p->depth > 256) { p->failed = 1; --p->depth; return NULL; }
    Node *n;
    if (peek(p) == T_LP) {
        ++p->i; n = node(N_SUB, list(p), NULL);
        if (peek(p) != T_RP) p->failed = 1; else ++p->i;
        while (!p->failed && isredir(peek(p))) redir(p, n);
    } else {
        n = node(N_SIMPLE, NULL, NULL);
        while (!p->failed && (peek(p) == T_WORD || isredir(peek(p)))) {
            if (isredir(peek(p))) { redir(p, n); continue; }
            const Token *t = &p->t->v[p->i++];
            if (!n->argc && (reserved(t) || t->assignment)) p->failed = 1;
            n->argv = xrealloc(n->argv, (n->argc + 2) * sizeof(char *));
            n->argv[n->argc++] = xstrdup(t->text); n->argv[n->argc] = NULL;
        }
        if (!n->argc) p->failed = 1;
    }
    --p->depth; return n;
}
static Node *pipeline(Parser *p) {
    Node *n = command(p);
    while (!p->failed && peek(p) == T_PIPE) { ++p->i; n = node(N_PIPE, n, command(p)); }
    return n;
}
static Node *and_or(Parser *p) {
    Node *n = pipeline(p);
    while (!p->failed && (peek(p) == T_AND || peek(p) == T_OR)) {
        TokenKind k = peek(p); ++p->i;
        n = node(k == T_AND ? N_AND : N_OR, n, pipeline(p));
    }
    return n;
}
static Node *list(Parser *p) {
    Node *result = NULL;
    do {
        Node *element = and_or(p);
        TokenKind k = peek(p);
        if (k == T_BG) element = node(N_ASYNC, element, NULL);
        result = result ? node(N_LIST, result, element) : element;
        if (p->failed || (k != T_SEMI && k != T_BG)) break;
        ++p->i;
        if (peek(p) == T_END || peek(p) == T_RP) break;
    } while (!p->failed);
    return result;
}
Node *parse(const Tokens *t, int *status) {
    Parser p = {t, 0, 0, 0}; *status = 0;
    if (peek(&p) == T_END) return NULL;
    Node *n = list(&p);
    if (p.failed || peek(&p) != T_END) {
        error("syntax", "invalid or unsupported command"); node_free(n); *status = 2; return NULL;
    }
    return n;
}
void node_dump(const Node *n, int depth) {
    if (!n) return;
    static const char *names[] = {"SIMPLE","SUBSHELL","PIPE","AND","OR","LIST","ASYNC"};
    printf("%*s%s", depth * 2, "", names[n->kind]);
    for (size_t i = 0; i < n->argc; ++i) printf(" [%s]", n->argv[i]);
    for (Redir *r = n->redirs; r; r = r->next)
        printf(" %s [%s]", r->kind == T_IN ? "<" : r->kind == T_OUT ? ">" : ">>", r->path);
    putchar('\n'); node_dump(n->left, depth + 1); node_dump(n->right, depth + 1);
}
static void append(char **s, const char *text) {
    size_t a = strlen(*s), b = strlen(text); *s = xrealloc(*s, a + b + 1); memcpy(*s+a, text, b+1);
}
static void append_word(char **s, const char *word) {
    if (*word && !strpbrk(word," \t\n|&;()<>'\"\\$`*?[]{}#")) { append(s,word); return; }
    append(s,"'");
    for (const char *p=word;*p;++p) {
        if (*p=='\'') append(s,"'\\''");
        else { char text[2]={*p,0}; append(s,text); }
    }
    append(s,"'");
}
char *node_label(const Node *n) {
    char *s = xstrdup(""); if (!n) return s;
    if (n->kind == N_SIMPLE) {
        for (size_t i = 0; i < n->argc; ++i) { if (i) append(&s," "); append_word(&s,n->argv[i]); }
    } else {
        char *a = node_label(n->left), *b = node_label(n->right);
        if (n->kind == N_SUB) append(&s,"(");
        append(&s,a);
        const char *op = n->kind == N_PIPE ? " | " : n->kind == N_AND ? " && " :
            n->kind == N_OR ? " || " : n->kind == N_LIST ? "; " : n->kind == N_ASYNC ? " &" : ")";
        append(&s,op); append(&s,b); free(a); free(b);
    }
    for (Redir *r=n->redirs;r;r=r->next) {
        append(&s,r->kind==T_IN?" < ":r->kind==T_OUT?" > ":" >> ");
        append_word(&s,r->path);
    }
    return s;
}
