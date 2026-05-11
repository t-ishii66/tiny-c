#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;
static int label_count;
static int stack_offset;

/* Symbol tables */
typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;        /* positive; -offset(%rbp) for locals */
    Type *type;
    int is_global;     /* if 1, name is a global symbol; offset unused */
    LVar *next;
};

static LVar *locals;     /* per-function locals (head = most recent decl) */
static LVar *globals;    /* program-wide globals */
static int frame_size;   /* allocated frame depth; monotonic within a function */

/* String literal table — emit them in .rodata */
typedef struct StrLit StrLit;
struct StrLit {
    char *str;
    int len;
    int label;
    StrLit *next;
};
static StrLit *strs;
static int str_count;

static const char *arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8",  "%r9"};
static const char *arg_regs32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};

/* Round up size to multiple of 8 (for stack slot allocation). */
static int round_up_8(int n) { return (n + 7) & ~7; }

static int new_label(void) { return label_count++; }

static void emit_push(void) {
    fprintf(out, "  pushq %%rax\n");
    stack_offset++;
}
static void emit_pop(const char *reg) {
    fprintf(out, "  popq %s\n", reg);
    stack_offset--;
}

/* Allocate a slot for a local; redeclaration is checked against the entire
   per-function locals list (function-level scope, no block-level redeclaration). */
static LVar *add_local(char *name, Type *type) {
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    int sz = round_up_8(type_size(type));
    frame_size += sz;
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    v->type = type;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v;
}

static void add_global(char *name, Type *type) {
    for (LVar *v = globals; v; v = v->next)
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared global: %s\n", name);
            exit(1);
        }
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    v->type = type;
    v->is_global = 1;
    v->next = globals;
    globals = v;
}

/* Look up variable: locals first, then globals. */
static LVar *find_var(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    for (LVar *v = globals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

/* Add a string literal to the table, return its label number. */
static int add_string(char *str, int len) {
    StrLit *s = calloc(1, sizeof(StrLit));
    s->str = str;
    s->len = len;
    s->label = str_count++;
    s->next = strs;
    strs = s;
    return s->label;
}

/* Forward */
static void gen_expr(Node *node);
static void gen_addr(Node *node);
static void gen_stmt(Node *node);

/* Type of an expression — needed for choosing load size and array decay.
   We track variable types in symbol tables; for derived expressions we
   infer from operator structure. */
static Type *expr_type(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
    case NODE_CHAR_LIT:
        return type_int();
    case NODE_STRING_LIT:
        return type_ptr(1);
    case NODE_IDENT: {
        LVar *v = find_var(node->name);
        if (!v) { fprintf(stderr, "undeclared: %s\n", node->name); exit(1); }
        return v->type;
    }
    case NODE_UNARY:
        if (node->op == '&') {
            Type *inner = expr_type(node->operand);
            return type_ptr(inner ? inner->base_size : 4);
        }
        if (node->op == '*') {
            Type *inner = expr_type(node->operand);
            /* result is the pointee scalar */
            Type *t = calloc(1, sizeof(Type));
            t->base_size = inner ? inner->base_size : 4;
            return t;
        }
        return type_int();
    case NODE_INDEX: {
        Type *base = expr_type(node->lhs);
        Type *t = calloc(1, sizeof(Type));
        t->base_size = base ? base->base_size : 4;
        return t;
    }
    case NODE_ASSIGN:
        return expr_type(node->lhs);
    case NODE_BINARY:
    case NODE_CALL:
    default:
        return type_int();
    }
}

/* Load the value at the address currently in %rax, using size `sz`.
   Result placed in %eax (4-byte) or %rax (8-byte). */
static void emit_load(int sz) {
    if (sz == 1)      fprintf(out, "  movsbl (%%rax), %%eax\n");
    else if (sz == 4) fprintf(out, "  movl (%%rax), %%eax\n");
    else              fprintf(out, "  movq (%%rax), %%rax\n");
}

/* Store the value in %eax/%rax to the address in %rcx, using size `sz`. */
static void emit_store(int sz) {
    if (sz == 1)      fprintf(out, "  movb %%al, (%%rcx)\n");
    else if (sz == 4) fprintf(out, "  movl %%eax, (%%rcx)\n");
    else              fprintf(out, "  movq %%rax, (%%rcx)\n");
}

/* Generate the ADDRESS of an lvalue. Result in %rax. */
static void gen_addr(Node *node) {
    switch (node->kind) {
    case NODE_IDENT: {
        LVar *v = find_var(node->name);
        if (!v) { fprintf(stderr, "undeclared: %s\n", node->name); exit(1); }
        if (v->is_global)
            fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);
        else
            fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
        return;
    }
    case NODE_UNARY:
        if (node->op == '*') {
            /* &(*p) == p */
            gen_expr(node->operand);
            return;
        }
        break;
    case NODE_INDEX: {
        /* &a[i] = (base address of a) + i * elem_size */
        Type *t = expr_type(node->lhs);
        int es = t->base_size;
        /* Get base address: for arrays, gen_addr of IDENT (the array storage);
           for pointers, gen_expr of IDENT (the address it holds). */
        Node *base = node->lhs;
        if (base->kind == NODE_IDENT) {
            LVar *v = find_var(base->name);
            if (v && v->type && v->type->is_array)
                gen_addr(base);          /* array decays to its address */
            else
                gen_expr(base);          /* pointer: load its value (address) */
        } else {
            gen_expr(base);
        }
        emit_push();                     /* save base */
        gen_expr(node->rhs);             /* %eax = i */
        fprintf(out, "  movslq %%eax, %%rax\n");          /* sign-extend to 64-bit */
        if (es != 1)
            fprintf(out, "  imulq $%d, %%rax\n", es);     /* %rax = i * elem_size */
        emit_pop("%rcx");                /* %rcx = base */
        fprintf(out, "  addq %%rcx, %%rax\n");            /* %rax = base + i*elem */
        return;
    }
    default: break;
    }
    fprintf(stderr, "not an lvalue\n");
    exit(1);
}

/* Emit cmpl + setcc + movzbl for comparison ops. */
static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}

/* Recursively push args (last to first), so first arg ends up on top. */
static int push_args(NodeList *l) {
    if (!l) return 0;
    int n = push_args(l->next);
    gen_expr(l->node);
    emit_push();
    return n + 1;
}

static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_CHAR_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_STRING_LIT: {
        int n = add_string(node->str_val, node->str_len);
        fprintf(out, "  leaq .LS%d(%%rip), %%rax\n", n);
        return;
    }
    case NODE_IDENT: {
        LVar *v = find_var(node->name);
        if (!v) { fprintf(stderr, "undeclared: %s\n", node->name); exit(1); }
        if (v->type && v->type->is_array) {
            /* Array name decays to its address */
            gen_addr(node);
            return;
        }
        gen_addr(node);
        emit_load(type_size(v->type));
        return;
    }
    case NODE_ASSIGN: {
        gen_addr(node->lhs);
        emit_push();
        gen_expr(node->rhs);
        emit_pop("%rcx");
        Type *t = expr_type(node->lhs);
        int sz = t->is_pointer ? 8 : t->base_size;
        emit_store(sz);
        return;
    }
    case NODE_UNARY:
        if (node->op == '&') {
            gen_addr(node->operand);
            return;
        }
        if (node->op == '*') {
            gen_expr(node->operand);
            Type *t = expr_type(node->operand);
            int sz = t ? t->base_size : 4;
            emit_load(sz);
            return;
        }
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
    case NODE_INDEX: {
        /* a[i] is *(&a[i]) */
        gen_addr(node);
        Type *t = expr_type(node->lhs);
        int sz = t ? t->base_size : 4;
        emit_load(sz);
        return;
    }
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

/* Phase 1: walk the function body and call add_local for every VAR_DECL,
   so all locals have offsets and frame_size is final before the prologue. */
static void collect_locals(Node *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_VAR_DECL:
        add_local(node->name, node->type);
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
    default: break;
    }
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        if (node->expr) gen_expr(node->expr);
        else fprintf(out, "  movl $0, %%eax\n");
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        return;
    case NODE_VAR_DECL: {
        if (!node->expr) return;  /* array decl, no init */
        LVar *v = find_var(node->name);
        fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
        emit_push();
        gen_expr(node->expr);
        emit_pop("%rcx");
        Type *t = v->type;
        int sz = t->is_pointer ? 8 : t->base_size;
        emit_store(sz);
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
    locals = NULL;
    frame_size = 0;
    stack_offset = 0;

    /* Phase 1: register parameters and walk body to allocate all local slots. */
    int n_params = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        add_local(l->node->name, l->node->type);
        n_params++;
    }
    if (n_params > 6) {
        fprintf(stderr, "too many parameters (max 6): %s\n", fn->name);
        exit(1);
    }
    collect_locals(fn->body);

    int aligned_frame = (frame_size + 15) & ~15;

    /* Phase 2: prologue */
    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    if (aligned_frame > 0)
        fprintf(out, "  subq $%d, %%rsp\n", aligned_frame);

    /* Spill register arguments to their slots. */
    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        LVar *v = find_var(l->node->name);
        Type *t = v->type;
        int sz = t->is_pointer ? 8 : 4;  /* int/char param spill as int (4 bytes) */
        if (sz == 8)
            fprintf(out, "  movq %s, -%d(%%rbp)\n", arg_regs64[i], v->offset);
        else
            fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], v->offset);
        i++;
    }

    /* Phase 3: body */
    gen_stmt(fn->body);

    /* Implicit return 0 */
    fprintf(out, "  movl $0, %%eax\n");
    fprintf(out, "  leave\n");
    fprintf(out, "  ret\n");
}

/* Output a string literal as .LSn: with byte data. */
static void emit_string_literal(StrLit *s) {
    fprintf(out, ".LS%d:\n", s->label);
    fprintf(out, "  .byte ");
    for (int i = 0; i < s->len; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%d", (unsigned char)s->str[i]);
    }
    fprintf(out, "\n");
}

void codegen(Node *prog, FILE *output) {
    out = output;
    label_count = 0;
    globals = NULL;
    strs = NULL;
    str_count = 0;

    /* First pass: register all globals (so functions can reference them). */
    for (NodeList *l = prog->stmts; l; l = l->next) {
        Node *n = l->node;
        if (n->kind == NODE_GLOBAL_VAR_DECL)
            add_global(n->name, n->type);
    }

    /* .text — functions */
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next) {
        Node *n = l->node;
        if (n->kind == NODE_FUNC_DEF)
            gen_func(n);
    }

    /* .bss — globals, zero-initialized */
    int has_globals = 0;
    for (NodeList *l = prog->stmts; l; l = l->next) {
        Node *n = l->node;
        if (n->kind != NODE_GLOBAL_VAR_DECL) continue;
        if (!has_globals) { fprintf(out, "  .bss\n"); has_globals = 1; }
        int sz = type_size(n->type);
        fprintf(out, "  .globl %s\n", n->name);
        fprintf(out, "%s:\n", n->name);
        fprintf(out, "  .zero %d\n", sz);
    }

    /* .rodata — string literals */
    if (strs) {
        fprintf(out, "  .section .rodata\n");
        for (StrLit *s = strs; s; s = s->next)
            emit_string_literal(s);
    }
}
