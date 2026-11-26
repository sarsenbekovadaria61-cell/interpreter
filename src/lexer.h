#ifndef LEXER_H
#define LEXER_H
#include <stddef.h>
typedef enum { T_END, T_WORD, T_PIPE, T_BG, T_SEMI, T_AND, T_OR,
               T_LP, T_RP, T_IN, T_OUT, T_APPEND } TokenKind;
typedef struct { TokenKind kind; char *text; int quoted, assignment; } Token;
typedef struct { Token *v; size_t n; } Tokens;

int lex(const char *input, Tokens *out);
void tokens_free(Tokens *t);
void tokens_dump(const Tokens *t);
#endif
