#include "lexer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { INITIAL_CAPACITY = 8 };

static int token_list_append(TokenList *list, TokenKind kind, const char *text, size_t length) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : INITIAL_CAPACITY;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(Token)) {
            return 0;
        }
        Token *tokens = realloc(list->tokens, capacity * sizeof(Token));
        if (tokens == NULL) {
            return 0;
        }
        list->tokens = tokens;
        list->capacity = capacity;
    }

    char *lexeme = malloc(length + 1);
    if (lexeme == NULL) {
        return 0;
    }
    memcpy(lexeme, text, length);
    lexeme[length] = '\0';
    list->tokens[list->count++] = (Token){kind, lexeme};
    return 1;
}

void token_list_free(TokenList *list) {
    if (list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->tokens[i].lexeme);
    }
    free(list->tokens);
    *list = (TokenList){0};
}

static int is_separator(char c) {
    return c != '\0' && strchr(" \t\n|&;()<>", c) != NULL;
}


static char *join_lines(const char *input, LexerStatus *status) {
    char *text = malloc(strlen(input) + 1);
    if (text == NULL) {
        *status = LEXER_NO_MEMORY;
        return NULL;
    }
    size_t out = 0;
    char quote = '\0';
    int word_start = 1;
    int comment = 0;

    for (size_t i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        if (comment) {
            text[out++] = c;
            if (c == '\n') {
                comment = 0;
                word_start = 1;
            }
            continue;
        }
        if (c == '\\' && quote != '\'') {
            char next = input[i + 1];
            if (next == '\n') {
                i++;
                if (input[i + 1] == '\0') {
                    *status = LEXER_INCOMPLETE;
                }
                continue;
            }
            if (next != '\0' && (quote == '\0' || next == '"' || next == '\\')) {
                text[out++] = c;
                text[out++] = input[++i];
                word_start = 0;
                continue;
            }
        }
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            word_start = 0;
        } else if (c == '#' && word_start) {
            comment = 1;
        } else {
            word_start = is_separator(c);
        }
        text[out++] = c;
    }
    text[out] = '\0';
    return text;
}

TokenList lexer_tokenize(const char *input) {
    TokenList list = {0};
    char *word = NULL;
    char *text = NULL;
    if (input == NULL) {
        list.status = LEXER_ERROR;
        list.error = "null input";
        return list;
    }

    text = join_lines(input, &list.status);
    if (text == NULL) {
        goto no_memory;
    }
    if (list.status == LEXER_INCOMPLETE) {
        list.error = "unfinished line continuation";
        goto fail;
    }
    word = malloc(strlen(text) + 1);
    if (word == NULL) {
        goto no_memory;
    }

    size_t pos = 0;
    while (text[pos] != '\0') {
        char c = text[pos];
        if (c == ' ' || c == '\t') {
            pos++;
            continue;
        }
        if (c == '#') {
            while (text[pos] != '\0' && text[pos] != '\n') {
                pos++;
            }
            continue;
        }
        if (is_separator(c)) {
            TokenKind kind;
            size_t length = 1;
            char next = text[pos + 1];
            if ((c == '<' && (next == '<' || next == '>')) ||
                (c == '>' && (next == '&' || next == '|')) ||
                (c == '<' && next == '&') || (c == '&' && next == '>') ||
                (c == '|' && next == '&') || (c == ';' && (next == ';' || next == '&')) ||
                (c == '>' && next == '>' && text[pos + 2] == '>')) {
                list.status = LEXER_ERROR;
                list.error = "unsupported operator";
                goto fail;
            }
            switch (c) {
                case '|': kind = next == '|' ? TOKEN_OR : TOKEN_PIPE; break;
                case '&': kind = next == '&' ? TOKEN_AND : TOKEN_BACKGROUND; break;
                case '>': kind = next == '>' ? TOKEN_REDIRECT_APPEND : TOKEN_REDIRECT_OUT; break;
                case '<': kind = TOKEN_REDIRECT_IN; break;
                case ';': kind = TOKEN_SEMICOLON; break;
                case '(': kind = TOKEN_LPAREN; break;
                case ')': kind = TOKEN_RPAREN; break;
                default: kind = TOKEN_NEWLINE; break;
            }
            if (kind == TOKEN_OR || kind == TOKEN_AND || kind == TOKEN_REDIRECT_APPEND) {
                length = 2;
            }
            if (!token_list_append(&list, kind, text + pos, length)) {
                goto no_memory;
            }
            pos += length;
            continue;
        }

        size_t length = 0;
        int plain_digits = 1;
        while (text[pos] != '\0' && !is_separator(text[pos])) {
            c = text[pos++];
            if (c == '\'' || c == '"') {
                char quote = c;
                plain_digits = 0;
                while (text[pos] != '\0' && text[pos] != quote) {
                    if (quote == '"' && text[pos] == '\\' &&
                        (text[pos + 1] == '"' || text[pos + 1] == '\\')) {
                        pos++;
                    }
                    word[length++] = text[pos++];
                }
                if (text[pos] == '\0') {
                    list.status = LEXER_INCOMPLETE;
                    list.error = "unclosed quote";
                    goto fail;
                }
                pos++;
            } else if (c == '\\') {
                plain_digits = 0;
                if (text[pos] == '\0') {
                    list.status = LEXER_INCOMPLETE;
                    list.error = "missing character after backslash";
                    goto fail;
                }
                word[length++] = text[pos++];
            } else {
                if (c < '0' || c > '9') {
                    plain_digits = 0;
                }
                word[length++] = c;
            }
        }
        if (plain_digits && length > 0 && (text[pos] == '<' || text[pos] == '>')) {
            list.status = LEXER_ERROR;
            list.error = "file descriptor redirection is not supported";
            goto fail;
        }
        if (!token_list_append(&list, TOKEN_WORD, word, length)) {
            goto no_memory;
        }
    }
    if (!token_list_append(&list, TOKEN_EOF, "", 0)) {
        goto no_memory;
    }
    free(word);
    free(text);
    return list;

no_memory:
    list.status = LEXER_NO_MEMORY;
    list.error = "not enough memory";
fail: {
    LexerStatus status = list.status;
    const char *error = list.error;
    free(word);
    free(text);
    token_list_free(&list);
    list.status = status;
    list.error = error;
    return list;
}
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOKEN_WORD: return "WORD";
        case TOKEN_PIPE: return "PIPE";
        case TOKEN_REDIRECT_IN: return "REDIRECT_IN";
        case TOKEN_REDIRECT_OUT: return "REDIRECT_OUT";
        case TOKEN_EOF: return "EOF";
        case TOKEN_BACKGROUND: return "BACKGROUND";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_REDIRECT_APPEND: return "REDIRECT_APPEND";
        case TOKEN_NEWLINE: return "NEWLINE";
    }
    return "UNKNOWN";
}
