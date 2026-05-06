#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"
#include "optimize.h"

extern int yyparse(void);
extern FILE *yyin;
extern Node *program;

static void usage(void) {
    fprintf(stderr,
        "usage: tinyc [--dump-ast] [--no-opt] <file>\n"
        "  --dump-ast   AST を表示して終了\n"
        "  --no-opt     最適化（AST 畳み込み + ピープホール）を無効化\n"
        "               （バックパッチによる単一パス codegen は常に有効）\n");
}

int main(int argc, char **argv) {
    int dump_ast = 0;
    int no_opt = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-ast") == 0) dump_ast = 1;
        else if (strcmp(argv[i], "--no-opt") == 0) no_opt = 1;
        else if (argv[i][0] == '-') { usage(); return 1; }
        else filename = argv[i];
    }
    if (!filename) { usage(); return 1; }

    yyin = fopen(filename, "r");
    if (!yyin) { perror(filename); return 1; }
    yyparse();
    fclose(yyin);

    if (!no_opt) program = optimize_ast(program);

    if (dump_ast) {
        print_ast(program, 0);
        return 0;
    }

    /* Always emit codegen into a memory buffer. Reasons:
       1) prologue's `subq $N, %rsp` is backpatched (codegen needs ftell/fseek)
       2) ピープホール最適化があれば同じバッファ上で適用してから出力
       --no-opt のときは ピープホール段階だけスキップ。 */
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    if (!mem) { perror("open_memstream"); return 1; }
    codegen(program, mem);
    fclose(mem);

    if (no_opt) {
        fputs(buf, stdout);
    } else {
        peephole(buf, (int)len, stdout);
    }
    free(buf);
    return 0;
}
