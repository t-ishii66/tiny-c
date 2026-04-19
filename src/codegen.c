#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;
static int label_count = 0;

/* --- Variable tracking --- */

typedef struct Var {
    char *name;
    int offset;       /* rbp-relative offset (negative for locals) */
    Type type;
    int is_array;
    int array_size;
    int is_global;
    struct Var *next;
} Var;

static Var *locals;
static Var *globals;
static int stack_offset;

/* --- String literal pool --- */

typedef struct StrLit {
    char *value;
    int id;
    struct StrLit *next;
} StrLit;

static StrLit *string_pool;
static int string_count;

/* --- Helpers --- */

static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(out, "    ");
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    va_end(ap);
}

static void emit_label(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    fprintf(out, ":\n");
    va_end(ap);
}

static int new_label(void) {
    return label_count++;
}

static int is_ptr(Type t) {
    return t == TY_PTR_INT || t == TY_PTR_CHAR;
}

static Var *find_var(const char *name) {
    for (Var *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    for (Var *v = globals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

static Var *add_local(const char *name, Type type, int is_array, int array_size) {
    Var *v = calloc(1, sizeof(Var));
    v->name = strdup(name);
    v->type = type;
    v->is_array = is_array;
    v->array_size = array_size;

    if (is_array) {
        int elem = (type == TY_CHAR) ? 1 : 4;
        int total = (elem * array_size + 7) & ~7;
        stack_offset -= total;
    } else {
        stack_offset -= 8;
    }
    v->offset = stack_offset;

    v->next = locals;
    locals = v;
    return v;
}

static int add_string(const char *value) {
    StrLit *s = calloc(1, sizeof(StrLit));
    s->value = strdup(value);
    s->id = string_count++;
    s->next = string_pool;
    string_pool = s;
    return s->id;
}

/* Count stack space needed for locals in a statement tree */
static int count_locals(Node *node) {
    if (!node) return 0;
    int n = 0;
    switch (node->kind) {
    case ND_VAR_DECL:
        return 8;
    case ND_ARRAY_DECL: {
        int elem = (node->type == TY_CHAR) ? 1 : 4;
        return (elem * node->array_size + 7) & ~7;
    }
    case ND_BLOCK:
        for (NodeList *l = node->children; l; l = l->next)
            n += count_locals(l->node);
        return n;
    case ND_IF:
        n = count_locals(node->rhs);
        if (node->extra) n += count_locals(node->extra);
        return n;
    case ND_WHILE:
        return count_locals(node->rhs);
    default:
        return 0;
    }
}

/* Infer the type of an expression (best-effort) */
static Type expr_type(Node *node) {
    switch (node->kind) {
    case ND_INT_LIT:    return TY_INT;
    case ND_CHAR_LIT:   return TY_CHAR;
    case ND_STRING_LIT: return TY_PTR_CHAR;
    case ND_IDENT: {
        Var *v = find_var(node->name);
        if (!v) return TY_INT;
        if (v->is_array) {
            /* array decays to pointer */
            return (v->type == TY_CHAR) ? TY_PTR_CHAR : TY_PTR_INT;
        }
        return v->type;
    }
    case ND_DEREF: {
        Type inner = expr_type(node->lhs);
        if (inner == TY_PTR_INT)  return TY_INT;
        if (inner == TY_PTR_CHAR) return TY_CHAR;
        return TY_INT;
    }
    case ND_ADDR: {
        Type inner = expr_type(node->lhs);
        if (inner == TY_INT)  return TY_PTR_INT;
        if (inner == TY_CHAR) return TY_PTR_CHAR;
        return TY_PTR_INT;
    }
    case ND_INDEX: {
        Type base = expr_type(node->lhs);
        if (base == TY_PTR_CHAR) return TY_CHAR;
        return TY_INT;
    }
    case ND_ASSIGN:
        return expr_type(node->lhs);
    default:
        return TY_INT;
    }
}

/* Forward declarations */
static void gen_expr(Node *node);
static void gen_stmt(Node *node);

/* Generate address of an lvalue into %rax */
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_IDENT: {
        Var *v = find_var(node->name);
        if (!v) {
            fprintf(stderr, "codegen: undefined variable '%s'\n", node->name);
            exit(1);
        }
        if (v->is_global)
            emit("leaq %s(%%rip), %%rax", v->name);
        else
            emit("leaq %d(%%rbp), %%rax", v->offset);
        break;
    }
    case ND_DEREF:
        /* *p as lvalue: address is the pointer value itself */
        gen_expr(node->lhs);
        break;
    case ND_INDEX: {
        /* a[i]: base + i * elem_size */
        Var *v = NULL;
        if (node->lhs->kind == ND_IDENT)
            v = find_var(node->lhs->name);

        /* get base address: array decays to addr, pointer gives value */
        gen_expr(node->lhs);
        emit("pushq %%rax");

        /* index */
        gen_expr(node->rhs);
        emit("cltq");

        int esz = (v && (v->type == TY_CHAR || v->type == TY_PTR_CHAR)) ? 1 : 4;
        if (esz != 1)
            emit("imulq $%d, %%rax, %%rax", esz);

        emit("popq %%rcx");
        emit("addq %%rcx, %%rax");
        break;
    }
    default:
        fprintf(stderr, "codegen: not an lvalue (kind=%d)\n", node->kind);
        exit(1);
    }
}

/* Store %eax/%rax to address in %rcx */
static void gen_store(Type type) {
    if (is_ptr(type))
        emit("movq %%rax, (%%rcx)");
    else if (type == TY_CHAR)
        emit("movb %%al, (%%rcx)");
    else
        emit("movl %%eax, (%%rcx)");
}

/* --- Expressions --- */

static void gen_expr(Node *node) {
    switch (node->kind) {
    case ND_INT_LIT:
        emit("movl $%d, %%eax", node->int_val);
        break;

    case ND_CHAR_LIT:
        emit("movl $%d, %%eax", (int)(unsigned char)node->char_val);
        break;

    case ND_STRING_LIT: {
        int id = add_string(node->name);
        emit("leaq .LC%d(%%rip), %%rax", id);
        break;
    }

    case ND_IDENT: {
        Var *v = find_var(node->name);
        if (!v) {
            fprintf(stderr, "codegen: undefined variable '%s'\n", node->name);
            exit(1);
        }
        if (v->is_array) {
            /* array decays to pointer (address of first element) */
            gen_addr(node);
        } else {
            gen_addr(node);
            if (is_ptr(v->type))
                emit("movq (%%rax), %%rax");
            else if (v->type == TY_CHAR)
                emit("movsbl (%%rax), %%eax");
            else
                emit("movl (%%rax), %%eax");
        }
        break;
    }

    case ND_ASSIGN: {
        gen_expr(node->rhs);
        emit("pushq %%rax");
        gen_addr(node->lhs);
        emit("movq %%rax, %%rcx");
        emit("popq %%rax");
        gen_store(expr_type(node->lhs));
        break;
    }

    /* Binary arithmetic */
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD: {
        gen_expr(node->lhs);
        emit("pushq %%rax");
        gen_expr(node->rhs);
        emit("movl %%eax, %%ecx");   /* rhs -> ecx */
        emit("popq %%rax");          /* lhs -> eax */

        switch (node->kind) {
        case ND_ADD: emit("addl %%ecx, %%eax"); break;
        case ND_SUB: emit("subl %%ecx, %%eax"); break;
        case ND_MUL: emit("imull %%ecx, %%eax"); break;
        case ND_DIV:
            emit("cltd");
            emit("idivl %%ecx");
            break;
        case ND_MOD:
            emit("cltd");
            emit("idivl %%ecx");
            emit("movl %%edx, %%eax");
            break;
        default: break;
        }
        break;
    }

    /* Comparisons */
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: case ND_GT: case ND_GE: {
        gen_expr(node->lhs);
        emit("pushq %%rax");
        gen_expr(node->rhs);
        emit("movl %%eax, %%ecx");
        emit("popq %%rax");
        emit("cmpl %%ecx, %%eax");

        switch (node->kind) {
        case ND_EQ: emit("sete %%al"); break;
        case ND_NE: emit("setne %%al"); break;
        case ND_LT: emit("setl %%al"); break;
        case ND_LE: emit("setle %%al"); break;
        case ND_GT: emit("setg %%al"); break;
        case ND_GE: emit("setge %%al"); break;
        default: break;
        }
        emit("movzbl %%al, %%eax");
        break;
    }

    /* Unary operators */
    case ND_NEG:
        gen_expr(node->lhs);
        emit("negl %%eax");
        break;

    case ND_NOT:
        gen_expr(node->lhs);
        emit("cmpl $0, %%eax");
        emit("sete %%al");
        emit("movzbl %%al, %%eax");
        break;

    case ND_ADDR:
        gen_addr(node->lhs);
        break;

    case ND_DEREF: {
        gen_expr(node->lhs);
        Type inner = expr_type(node->lhs);
        if (inner == TY_PTR_CHAR)
            emit("movsbl (%%rax), %%eax");
        else
            emit("movl (%%rax), %%eax");
        break;
    }

    case ND_INDEX: {
        gen_addr(node);
        Type t = expr_type(node);
        if (t == TY_CHAR)
            emit("movsbl (%%rax), %%eax");
        else
            emit("movl (%%rax), %%eax");
        break;
    }

    case ND_CALL: {
        /* x86-64 SysV ABI: first 6 integer args in rdi,rsi,rdx,rcx,r8,r9 */
        static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

        /* count and evaluate args, push results */
        int nargs = 0;
        for (NodeList *l = node->children; l; l = l->next) {
            gen_expr(l->node);
            emit("pushq %%rax");
            nargs++;
        }
        /* pop into registers (reverse order) */
        for (int i = nargs - 1; i >= 0; i--)
            emit("popq %%%s", regs[i]);

        /* AL = 0 for varargs (number of vector regs) */
        emit("movl $0, %%eax");
        emit("call %s", node->name);
        break;
    }

    default:
        fprintf(stderr, "codegen: unsupported expr kind %d\n", node->kind);
        exit(1);
    }
}

/* --- Statements --- */

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case ND_RETURN:
        if (node->lhs)
            gen_expr(node->lhs);
        emit("leave");
        emit("ret");
        break;

    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        break;

    case ND_BLOCK:
        for (NodeList *l = node->children; l; l = l->next)
            gen_stmt(l->node);
        break;

    case ND_IF: {
        int lelse = new_label();
        int lend  = new_label();
        gen_expr(node->lhs);
        emit("cmpl $0, %%eax");
        if (node->extra) {
            emit("je .L%d", lelse);
            gen_stmt(node->rhs);
            emit("jmp .L%d", lend);
            emit_label(".L%d", lelse);
            gen_stmt(node->extra);
            emit_label(".L%d", lend);
        } else {
            emit("je .L%d", lend);
            gen_stmt(node->rhs);
            emit_label(".L%d", lend);
        }
        break;
    }

    case ND_WHILE: {
        int lbegin = new_label();
        int lend   = new_label();
        emit_label(".L%d", lbegin);
        gen_expr(node->lhs);
        emit("cmpl $0, %%eax");
        emit("je .L%d", lend);
        gen_stmt(node->rhs);
        emit("jmp .L%d", lbegin);
        emit_label(".L%d", lend);
        break;
    }

    case ND_VAR_DECL: {
        Var *v = add_local(node->name, node->type, 0, 0);
        if (node->lhs) {
            gen_expr(node->lhs);
            if (is_ptr(v->type))
                emit("movq %%rax, %d(%%rbp)", v->offset);
            else
                emit("movl %%eax, %d(%%rbp)", v->offset);
        }
        break;
    }

    case ND_ARRAY_DECL:
        add_local(node->name, node->type, 1, node->array_size);
        break;

    default:
        fprintf(stderr, "codegen: unsupported stmt kind %d\n", node->kind);
        exit(1);
    }
}

/* --- Top-level --- */

static void gen_func(Node *node) {
    /* reset per-function state */
    locals = NULL;
    stack_offset = 0;

    /* count params */
    int nparams = 0;
    for (NodeList *l = node->children; l; l = l->next)
        nparams++;

    /* compute stack space: params + locals */
    int space = nparams * 8 + count_locals(node->body);
    space = (space + 15) & ~15;

    /* prologue */
    fprintf(out, "    .globl %s\n", node->name);
    emit_label("%s", node->name);
    emit("pushq %%rbp");
    emit("movq %%rsp, %%rbp");
    if (space > 0)
        emit("subq $%d, %%rsp", space);

    /* store params from registers to stack */
    static const char *param_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int i = 0;
    for (NodeList *l = node->children; l; l = l->next, i++) {
        Node *p = l->node;
        Var *v = add_local(p->name, p->type, 0, 0);
        emit("movq %%%s, %d(%%rbp)", param_regs[i], v->offset);
    }

    /* body */
    gen_stmt(node->body);

    /* implicit return 0 */
    emit("movl $0, %%eax");
    emit("leave");
    emit("ret");
    fprintf(out, "\n");
}

void codegen(Node *program, FILE *output) {
    out = output;
    string_pool = NULL;
    string_count = 0;
    globals = NULL;

    fprintf(out, "    .text\n");

    /* register global variables */
    for (NodeList *l = program->children; l; l = l->next) {
        Node *n = l->node;
        if (n->kind == ND_VAR_DECL) {
            Var *v = calloc(1, sizeof(Var));
            v->name = strdup(n->name);
            v->type = n->type;
            v->is_global = 1;
            v->next = globals;
            globals = v;
        }
    }

    /* generate functions */
    for (NodeList *l = program->children; l; l = l->next) {
        Node *n = l->node;
        if (n->kind == ND_FUNC_DEF)
            gen_func(n);
    }

    /* emit string literals */
    if (string_pool) {
        fprintf(out, "    .section .rodata\n");
        for (int i = 0; i < string_count; i++) {
            for (StrLit *s = string_pool; s; s = s->next) {
                if (s->id == i) {
                    fprintf(out, ".LC%d:\n", i);
                    fprintf(out, "    .string \"%s\"\n", s->value);
                    break;
                }
            }
        }
    }

    /* emit global variables (.bss) */
    if (globals) {
        fprintf(out, "    .bss\n");
        for (Var *v = globals; v; v = v->next) {
            int sz = is_ptr(v->type) ? 8 : 4;
            fprintf(out, "    .globl %s\n", v->name);
            fprintf(out, "    .align %d\n", sz);
            fprintf(out, "%s:\n", v->name);
            fprintf(out, "    .zero %d\n", sz);
        }
    }
}
