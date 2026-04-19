#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yyparse(void);
extern Node *parse_result;
extern FILE *yyin;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: tinyc [--dump-ast] <file.c>\n");
        return 1;
    }

    int dump_ast = 0;
    char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-ast") == 0)
            dump_ast = 1;
        else
            filename = argv[i];
    }

    if (!filename) {
        fprintf(stderr, "Usage: tinyc [--dump-ast] <file.c>\n");
        return 1;
    }

    yyin = fopen(filename, "r");
    if (!yyin) {
        perror(filename);
        return 1;
    }

    yyparse();
    fclose(yyin);

    if (dump_ast) {
        print_ast(parse_result, 0);
    } else {
        codegen(parse_result, stdout);
    }

    return 0;
}
