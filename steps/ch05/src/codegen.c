#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;
static int label_count;
static int stack_offset;   /* current rsp delta in 8-byte units from aligned base */

/* Symbol table for local variables (and parameters) */
typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;     /* positive; used as -offset(%rbp) */
    LVar *next;
};

static LVar *locals;
static int frame_size;

/* System V AMD64 ABI: first 6 integer args go in these registers */
static const char *arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8",  "%r9"};
static const char *arg_regs32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};

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

static void emit_push(void) {
    fprintf(out, "  pushq %%rax\n");
    stack_offset++;
}

static void emit_pop(const char *reg) {
    fprintf(out, "  popq %s\n", reg);
    stack_offset--;
}

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

/* Pre-pass: walk the function body and register every variable
   declaration in the symbol table. After this, the total frame size
   is known and codegen can emit the prologue with a single subq. */
static void collect_locals(Node *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_VAR_DECL:
        add_local(node->name);
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            collect_locals(l->node);
        break;
    case NODE_IF:
        collect_locals(node->then_body);
        collect_locals(node->else_body);
        break;
    case NODE_WHILE:
        collect_locals(node->body);
        break;
    default:
        break;
    }
}

/* Push args in reverse order (deepest call pushes the LAST arg first),
   so that after recursion returns, the FIRST arg sits on top of stack.
   Returns the total number of args. */
static int push_args(NodeList *l) {
    if (!l) return 0;
    int n = push_args(l->next);
    gen_expr(l->node);
    emit_push();
    return n + 1;
}

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
        emit_push();
        gen_expr(node->lhs);
        emit_pop("%rcx");
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
    case NODE_CALL: {
        /* Stack must be 16-byte aligned at the call instruction.
           If the current rsp is 8-misaligned, pad with subq $8. */
        int pad = (stack_offset % 2) != 0;
        if (pad) {
            fprintf(out, "  subq $8, %%rsp\n");
            stack_offset++;
        }

        int n_args = push_args(node->args);
        if (n_args > 6) {
            fprintf(stderr, "too many arguments (max 6): %s\n", node->name);
            exit(1);
        }
        for (int i = 0; i < n_args; i++)
            emit_pop(arg_regs64[i]);

        /* Variadic ABI: %al = number of XMM regs used (0 — we don't pass floats) */
        fprintf(out, "  movl $0, %%eax\n");
        fprintf(out, "  call %s\n", node->name);

        if (pad) {
            fprintf(out, "  addq $8, %%rsp\n");
            stack_offset--;
        }
        return;
    }
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
        /* Pre-pass already added this name; just initialize its slot. */
        int off = find_local(node->name);
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

static void gen_func(Node *fn) {
    /* Reset per-function state */
    locals = NULL;
    frame_size = 0;
    stack_offset = 0;

    /* Phase 1: register parameters and local variables */
    int n_params = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        add_local(l->node->name);
        n_params++;
    }
    if (n_params > 6) {
        fprintf(stderr, "too many parameters (max 6): %s\n", fn->name);
        exit(1);
    }
    collect_locals(fn->body);

    /* Round up frame size to a multiple of 16 for ABI alignment */
    int aligned_frame = (frame_size + 15) & ~15;

    /* Phase 2: emit prologue */
    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    if (aligned_frame > 0)
        fprintf(out, "  subq $%d, %%rsp\n", aligned_frame);

    /* Spill register arguments to their stack slots */
    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        int off = find_local(l->node->name);
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);
        i++;
    }

    /* Phase 3: emit body */
    gen_stmt(fn->body);

    /* Implicit "return 0" if the function falls off the end */
    fprintf(out, "  movl $0, %%eax\n");
    fprintf(out, "  leave\n");
    fprintf(out, "  ret\n");
}

void codegen(Node *prog, FILE *output) {
    out = output;
    label_count = 0;

    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)
        gen_func(l->node);
}
