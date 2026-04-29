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
        return;
    case NODE_UNARY:
        gen_expr(node->operand);
        fprintf(out, "  negl %%eax\n");
        return;
    case NODE_BINARY:
        /* Evaluate rhs first, push it; then lhs goes in %eax. */
        gen_expr(node->rhs);
        fprintf(out, "  pushq %%rax\n");
        gen_expr(node->lhs);
        fprintf(out, "  popq %%rcx\n");
        /* Now: %eax = lhs, %ecx = rhs */
        switch (node->op) {
        case '+': fprintf(out, "  addl %%ecx, %%eax\n"); return;
        case '-': fprintf(out, "  subl %%ecx, %%eax\n"); return;
        case '*': fprintf(out, "  imull %%ecx, %%eax\n"); return;
        case '/':
            fprintf(out, "  cdq\n");
            fprintf(out, "  idivl %%ecx\n");
            return;
        case '%':
            fprintf(out, "  cdq\n");
            fprintf(out, "  idivl %%ecx\n");
            fprintf(out, "  movl %%edx, %%eax\n");
            return;
        }
        fprintf(stderr, "unknown binary op: %c\n", node->op);
        exit(1);
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
