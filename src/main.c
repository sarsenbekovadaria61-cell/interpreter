#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lexer.h"

static void print_lexeme(const char *text) {
    putchar('"');
    for (size_t i = 0; text[i] != '\0'; i++) {
        switch (text[i]) {
            case '\n': fputs("\\n", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '"': fputs("\\\"", stdout); break;
            default: putchar((unsigned char)text[i]); break;
        }
    }
    putchar('"');
}

static void dump_tokens(const TokenList *list) {
    for (size_t i = 0; i < list->count; i++) {
        printf("Token %zu: type=%s, lexeme=", i, token_kind_name(list->tokens[i].kind));
        print_lexeme(list->tokens[i].lexeme);
        putchar('\n');
    }
}

static int report_error(const TokenList *list) {
    fprintf(stderr, "mysh: lexer: %s\n", list->error);
    return list->status == LEXER_NO_MEMORY ? 1 : 2;
}

int main(int argc, char *argv[]) {
    int dump_only = 0;
    const char *command = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            dump_only = 1;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
        } else {
            fprintf(stderr, "mysh: usage: mysh [--dump-tokens] [-c command]\n");
            return 2;
        }
    }

    if (command != NULL) {
        TokenList tokens = lexer_tokenize(command);
        int result = 0;
        if (tokens.status == LEXER_OK) {
            dump_tokens(&tokens);
        } else {
            result = report_error(&tokens);
        }
        token_list_free(&tokens);
        if (fflush(stdout) == EOF) {
            fprintf(stderr, "mysh: stdout: %s\n", strerror(errno));
            return 1;
        }
        return result;
    }

    int interactive = isatty(STDIN_FILENO) && !dump_only;
    char *line = NULL;
    size_t line_capacity = 0;
    char *input = NULL;
    size_t input_length = 0;
    int result = 0;

    while (1) {
        if (interactive) {
            fputs(input_length == 0 ? "mysh> " : "> ", stdout);
            if (fflush(stdout) == EOF) {
                fprintf(stderr, "mysh: stdout: %s\n", strerror(errno));
                result = 1;
                break;
            }
        }
        errno = 0;
        ssize_t length = getline(&line, &line_capacity, stdin);
        if (length < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            if (!feof(stdin)) {
                fprintf(stderr, "mysh: stdin: %s\n", strerror(errno));
                result = 1;
            } else if (input_length != 0) {
                fprintf(stderr, "mysh: lexer: unexpected end of input\n");
                result = 2;
            }
            if (interactive) {
                putchar('\n');
            }
            break;
        }
        if (memchr(line, '\0', (size_t)length) != NULL) {
            fprintf(stderr, "mysh: lexer: null byte in input\n");
            result = 2;
            free(input);
            input = NULL;
            input_length = 0;
            if (!interactive) {
                break;
            }
            continue;
        }
        if ((size_t)length > SIZE_MAX - input_length - 1) {
            fprintf(stderr, "mysh: input: command is too long\n");
            result = 1;
            break;
        }
        char *grown = realloc(input, input_length + (size_t)length + 1);
        if (grown == NULL) {
            fprintf(stderr, "mysh: input: not enough memory\n");
            result = 1;
            break;
        }
        input = grown;
        memcpy(input + input_length, line, (size_t)length + 1);
        input_length += (size_t)length;

        
        if (interactive && (strcmp(input, "exit\n") == 0 || strcmp(input, "exit") == 0)) {
            break;
        }

        TokenList tokens = lexer_tokenize(input);
        if (tokens.status == LEXER_INCOMPLETE) {
            token_list_free(&tokens);
            continue;
        }
        if (tokens.status != LEXER_OK) {
            result = report_error(&tokens);
        } else {
            dump_tokens(&tokens);
            result = 0;
        }
        int fatal = tokens.status == LEXER_NO_MEMORY;
        token_list_free(&tokens);
        free(input);
        input = NULL;
        input_length = 0;
        if (fatal || (result != 0 && !interactive)) {
            break;
        }
    }

    free(input);
    free(line);
    if (fflush(stdout) == EOF) {
        fprintf(stderr, "mysh: stdout: %s\n", strerror(errno));
        return 1;
    }
    return result;
}
