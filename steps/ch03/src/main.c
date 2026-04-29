#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yyparse(void);
extern FILE *yyin;
extern Node *program;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: tinyc [--dump-ast] <file>\n");
        return 1;
    }

    int dump_ast = 0;
    char *filename = argv[1];

    if (strcmp(argv[1], "--dump-ast") == 0) {
        dump_ast = 1;
        if (argc < 3) { fprintf(stderr, "usage: tinyc --dump-ast <file>\n"); return 1; }
        filename = argv[2];
    }

    yyin = fopen(filename, "r");
    if (!yyin) { perror(filename); return 1; }

    yyparse();
    fclose(yyin);

    if (dump_ast) {
        print_ast(program, 0);
    } else {
        codegen(program, stdout);
    }
    return 0;
}
