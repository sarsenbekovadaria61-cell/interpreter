#include "lexer.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


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

enum { INITIAL_CAPACITY = 8 };

static int token_list_append(TokenList *list, TokenKind kind,
                             const char *text, size_t length) {
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
    list->tokens[list->count++] = (Token){.kind = kind, .text = lexeme};
    return 1;
}

static void token_list_free(TokenList *list) {
    if (list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->tokens[i].text);
    }
    free(list->tokens);
    *list = (TokenList){0};
}

static int is_separator(char c) {
    return c != '\0' && strchr(" \t\r\n|&;()<>", c) != NULL;
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

static TokenList lexer_tokenize(const char *input) {
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
        if (c == ' ' || c == '\t' || c == '\r') {
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
            if (c == '\n' && (list.count == 0 ||
                list.tokens[list.count - 1].kind == T_SEMI ||
                list.tokens[list.count - 1].kind == T_BG)) {
                pos++;
                continue;
            }
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
                case '|': kind = next == '|' ? T_OR : T_PIPE; break;
                case '&': kind = next == '&' ? T_AND : T_BG; break;
                case '>': kind = next == '>' ? T_APPEND : T_OUT; break;
                case '<': kind = T_IN; break;
                case ';': kind = T_SEMI; break;
                case '(': kind = T_LP; break;
                case ')': kind = T_RP; break;
                default: kind = T_SEMI; break;
            }
            if (kind == T_OR || kind == T_AND || kind == T_APPEND) {
                length = 2;
            }
            const char *operator_text = c == '\n' ? ";" : text + pos;
            if (!token_list_append(&list, kind, operator_text, length)) {
                goto no_memory;
            }
            pos += length;
            continue;
        }

        size_t length = 0;
        int plain_digits = 1;
        int quoted = 0;
        int assignment = 0;
        while (text[pos] != '\0' && !is_separator(text[pos])) {
            c = text[pos++];
            if (c == '\'' || c == '"') {
                char quote = c;
                quoted = 1;
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
                quoted = 1;
                plain_digits = 0;
                if (text[pos] == '\0') {
                    list.status = LEXER_INCOMPLETE;
                    list.error = "missing character after backslash";
                    goto fail;
                }
                word[length++] = text[pos++];
            } else {
                if (c == '=' && length > 0 && !quoted) {
                    int name = isalpha((unsigned char)word[0]) || word[0] == '_';
                    for (size_t i = 1; i < length; i++) {
                        if (!isalnum((unsigned char)word[i]) && word[i] != '_') {
                            name = 0;
                        }
                    }
                    if (name) {
                        assignment = 1;
                    }
                }
                if (strchr("$`*?[]{}", c) != NULL || (c == '~' && length == 0)) {
                    list.status = LEXER_ERROR;
                    list.error = "unsupported shell construct";
                    goto fail;
                }
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
        if (!token_list_append(&list, T_WORD, word, length)) {
            goto no_memory;
        }
        list.tokens[list.count - 1].quoted = quoted;
        list.tokens[list.count - 1].assignment = assignment;
    }
    if (!token_list_append(&list, T_END, "", 0)) {
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

static const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case T_WORD: return "WORD";
        case T_PIPE: return "PIPE";
        case T_IN: return "IN";
        case T_OUT: return "OUT";
        case T_END: return "END";
        case T_BG: return "BG";
        case T_SEMI: return "SEMI";
        case T_AND: return "AND";
        case T_OR: return "OR";
        case T_LP: return "LP";
        case T_RP: return "RP";
        case T_APPEND: return "APPEND";
    }
    return "UNKNOWN";
}


int lex(const char *input, Tokens *out) {
    *out = (Tokens){0};
    TokenList list = lexer_tokenize(input);
    if (list.status == LEXER_OK) {
        out->v = list.tokens;
        out->n = list.count;
        return 0;
    }
    if (list.status == LEXER_INCOMPLETE) {
        return 1;
    }
    if (list.status == LEXER_NO_MEMORY) {
        error("lexer", list.error);
        exit(1); 
    }
    error("syntax", list.error);
    return 2;
}

void tokens_free(Tokens *tokens) {
    if (tokens == NULL) {
        return;
    }
    TokenList list = {.tokens = tokens->v, .count = tokens->n};
    token_list_free(&list);
    *tokens = (Tokens){0};
}

void tokens_dump(const Tokens *tokens) {
    for (size_t i = 0; i < tokens->n; i++) {
        printf("%s\t[%s]\n", token_kind_name(tokens->v[i].kind), tokens->v[i].text);
    }
}
