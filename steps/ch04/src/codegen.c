#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;
static int label_count;

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

static int new_label(void) { return label_count++; }

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

/* Emit cmpl + setcc + movzbl for comparison ops; result in %eax (0 or 1). */
static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}

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
        switch (node->op) {
        case '-':
            fprintf(out, "  negl %%eax\n");
            return;
        case '!':
            fprintf(out, "  cmpl $0, %%eax\n");
            fprintf(out, "  sete %%al\n");
            fprintf(out, "  movzbl %%al, %%eax\n");
            return;
        }
        fprintf(stderr, "unknown unary op\n");
        exit(1);
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
        case '<':   emit_compare("setl");  return;
        case OP_LE: emit_compare("setle"); return;
        case '>':   emit_compare("setg");  return;
        case OP_GE: emit_compare("setge"); return;
        case OP_EQ: emit_compare("sete");  return;
        case OP_NE: emit_compare("setne"); return;
        }
        fprintf(stderr, "unknown binary op\n");
        exit(1);
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

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
    case NODE_IF: {
        int n = new_label();
        gen_expr(node->cond);
        fprintf(out, "  cmpl $0, %%eax\n");
        if (node->else_body) {
            fprintf(out, "  je .Lelse_%d\n", n);
            gen_stmt(node->then_body);
            fprintf(out, "  jmp .Lendif_%d\n", n);
            fprintf(out, ".Lelse_%d:\n", n);
            gen_stmt(node->else_body);
            fprintf(out, ".Lendif_%d:\n", n);
        } else {
            fprintf(out, "  je .Lendif_%d\n", n);
            gen_stmt(node->then_body);
            fprintf(out, ".Lendif_%d:\n", n);
        }
        return;
    }
    case NODE_WHILE: {
        int n = new_label();
        fprintf(out, ".Lbegin_%d:\n", n);
        gen_expr(node->cond);
        fprintf(out, "  cmpl $0, %%eax\n");
        fprintf(out, "  je .Lendwhile_%d\n", n);
        gen_stmt(node->body);
        fprintf(out, "  jmp .Lbegin_%d\n", n);
        fprintf(out, ".Lendwhile_%d:\n", n);
        return;
    }
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}

void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;
    frame_size = 0;
    label_count = 0;

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
