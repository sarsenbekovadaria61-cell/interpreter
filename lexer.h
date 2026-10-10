#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,         
    TOKEN_REDIRECT_IN,  
    TOKEN_REDIRECT_OUT, 
    TOKEN_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    char* lexeme; 
} Token;

typedef struct {
    Token* tokens;
    size_t count;
    size_t capacity;
} TokenList;


TokenList lexer_tokenize(const char* input);


void token_list_free(TokenList* list);

#endif