#ifndef LEXER_H
#define LEXER_H
#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,         
    TOKEN_REDIRECT_IN,  
    TOKEN_REDIRECT_OUT, 
    TOKEN_EOF,
    TOKEN_BACKGROUND,
    TOKEN_SEMICOLON,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_REDIRECT_APPEND,
    TOKEN_NEWLINE
} TokenKind;

typedef struct {
    TokenKind kind;
    char *lexeme;
} Token;

typedef enum {
    LEXER_OK,
    LEXER_INCOMPLETE,
    LEXER_ERROR,
    LEXER_NO_MEMORY
} LexerStatus;

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
    LexerStatus status;
    const char *error;  
} TokenList;


TokenList lexer_tokenize(const char *input);
void token_list_free(TokenList *list);
const char *token_kind_name(TokenKind kind);


#endif