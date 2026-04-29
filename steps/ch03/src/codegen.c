#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;

/* Symbol table for local variables */
typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;     /* positive; used as -offset(%rbp) */
    LVar *next;
};

static LVar *locals;
static int frame_size;

static int add_local(char *name) {
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    frame_size += 8;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v->offset;
}

static int find_local(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v->offset;
    fprintf(stderr, "undeclared variable: %s\n", name);
    exit(1);
}

/* Generate code for an expression — result goes into %eax */
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_IDENT: {
        int off = find_local(node->name);
        fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
        return;
    }
    case NODE_ASSIGN: {
        if (node->lhs->kind != NODE_IDENT) {
            fprintf(stderr, "lhs of '=' must be a variable\n");
            exit(1);
        }
        int off = find_local(node->lhs->name);
        gen_expr(node->rhs);
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
        return;
    }
    case NODE_UNARY:
        gen_expr(node->operand);
        fprintf(out, "  negl %%eax\n");
        return;
    case NODE_BINARY:
        gen_expr(node->rhs);
        fprintf(out, "  pushq %%rax\n");
        gen_expr(node->lhs);
        fprintf(out, "  popq %%rcx\n");
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
        return;
    case NODE_VAR_DECL: {
        int off = add_local(node->name);
        fprintf(out, "  subq $8, %%rsp\n");
        gen_expr(node->expr);
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
        return;
    }
    case NODE_EXPR_STMT:
        gen_expr(node->expr);
        return;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        return;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}

/* Generate code for the entire program */
void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;
    frame_size = 0;

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
