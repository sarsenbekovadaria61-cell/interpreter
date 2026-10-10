#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

int main(void) {
    char line[1024];

    while (1) {
        printf("mysh> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0) {
            break;
        }

        if (strlen(line) > 0) {
    
            TokenList tokens = lexer_tokenize(line);

            printf("--- Токены ---\n");
            for (size_t i = 0; i < tokens.count; i++) {
                printf("Token %zu: type=%d, lexeme=\"%s\"\n", i, (int)tokens.tokens[i].kind, tokens.tokens[i].lexeme);
            }
            printf("--------------\n");

           
            token_list_free(&tokens);
        }
    }

    return 0;
}