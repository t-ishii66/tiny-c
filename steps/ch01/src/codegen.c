#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;

/* Generate code for an expression — result goes into %eax */
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

/* Generate code for a statement */
static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        gen_expr(node->expr);
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        break;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}

/* Generate code for the entire program */
void codegen(Node *prog, FILE *output) {
    out = output;

    /* prog is a single function definition (for now) */
    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
