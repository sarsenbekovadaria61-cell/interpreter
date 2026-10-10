#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void token_list_init(TokenList* list) {
    list->capacity = 8;
    list->count = 0;
    list->tokens = malloc(list->capacity * sizeof(Token));
}

static void token_list_append(TokenList* list, Token token) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->tokens = realloc(list->tokens, list->capacity * sizeof(Token));
    }
    list->tokens[list->count++] = token;
}

void token_list_free(TokenList* list) {
    if (!list || !list->tokens) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->tokens[i].lexeme);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

TokenList lexer_tokenize(const char* input) {
    TokenList list;
    token_list_init(&list);

    size_t pos = 0;
    size_t len = strlen(input);

    while (pos < len) {
       
        while (pos < len && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n')) {
            pos++;
        }

        if (pos >= len) break;

        char c = input[pos];

       
        if (c == '|') {
            Token t = { .kind = TOKEN_PIPE, .lexeme = strdup("|") };
            token_list_append(&list, t);
            pos++;
        } else if (c == '<') {
            Token t = { .kind = TOKEN_REDIRECT_IN, .lexeme = strdup("<") };
            token_list_append(&list, t);
            pos++;
        } else if (c == '>') {
            Token t = { .kind = TOKEN_REDIRECT_OUT, .lexeme = strdup(">") };
            token_list_append(&list, t);
            pos++;
        } else {
            
            size_t start = pos;
            while (pos < len && !isspace((unsigned char)input[pos]) &&
                   input[pos] != '|' && input[pos] != '<' && input[pos] != '>') {
                pos++;
            }

            size_t word_len = pos - start;
            char* word = malloc(word_len + 1);
            strncpy(word, input + start, word_len);
            word[word_len] = '\0';

            Token t = { .kind = TOKEN_WORD, .lexeme = word };
            token_list_append(&list, t);
        }
    }

  
    Token eof_token = { .kind = TOKEN_EOF, .lexeme = strdup("") };
    token_list_append(&list, eof_token);

    return list;
}