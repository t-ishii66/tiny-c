#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "optimize.h"

/* ============================================================
 * AST-level optimization: constant folding + algebraic simplification
 * ============================================================ */

static int is_int_lit(Node *n)        { return n && n->kind == NODE_INT_LIT; }
static int is_int_lit_val(Node *n, int v) { return is_int_lit(n) && n->int_val == v; }

/* Recursively optimize: returns the optimized node (may be same or new). */
Node *optimize_ast(Node *node) {
    if (!node) return NULL;

    switch (node->kind) {
    case NODE_BINARY: {
        node->lhs = optimize_ast(node->lhs);
        node->rhs = optimize_ast(node->rhs);
        Node *L = node->lhs, *R = node->rhs;

        /* Algebraic simplification (when one side is a specific constant). */
        switch (node->op) {
        case '+':
            if (is_int_lit_val(L, 0)) return R;       /* 0 + x → x */
            if (is_int_lit_val(R, 0)) return L;       /* x + 0 → x */
            break;
        case '-':
            if (is_int_lit_val(R, 0)) return L;       /* x - 0 → x */
            break;
        case '*':
            if (is_int_lit_val(L, 1)) return R;       /* 1 * x → x */
            if (is_int_lit_val(R, 1)) return L;       /* x * 1 → x */
            if (is_int_lit_val(L, 0) || is_int_lit_val(R, 0))
                return new_int_lit(0);                 /* 0 * x → 0, x * 0 → 0 */
            break;
        case '/':
            if (is_int_lit_val(R, 1)) return L;       /* x / 1 → x */
            break;
        }

        /* Constant folding (when both children are INT_LIT). */
        if (is_int_lit(L) && is_int_lit(R)) {
            int a = L->int_val, b = R->int_val, r = 0;
            switch (node->op) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            case '*': r = a * b; break;
            case '/': if (b == 0) return node; r = a / b; break;
            case '%': if (b == 0) return node; r = a % b; break;
            case '<':   r = (a <  b); break;
            case '>':   r = (a >  b); break;
            case OP_LE: r = (a <= b); break;
            case OP_GE: r = (a >= b); break;
            case OP_EQ: r = (a == b); break;
            case OP_NE: r = (a != b); break;
            default: return node;
            }
            return new_int_lit(r);
        }
        return node;
    }
    case NODE_UNARY: {
        node->operand = optimize_ast(node->operand);
        Node *X = node->operand;
        if (is_int_lit(X)) {
            switch (node->op) {
            case '-': return new_int_lit(-X->int_val);
            case '!': return new_int_lit(!X->int_val);
            }
        }
        return node;
    }
    case NODE_ASSIGN:
        node->rhs = optimize_ast(node->rhs);
        /* lhs is an lvalue; do not fold it. */
        return node;
    case NODE_INDEX:
        node->rhs = optimize_ast(node->rhs);
        return node;
    case NODE_RETURN:
    case NODE_EXPR_STMT:
    case NODE_VAR_DECL:
        node->expr = optimize_ast(node->expr);
        return node;
    case NODE_IF:
        node->cond = optimize_ast(node->cond);
        node->then_body = optimize_ast(node->then_body);
        node->else_body = optimize_ast(node->else_body);
        return node;
    case NODE_WHILE:
        node->cond = optimize_ast(node->cond);
        node->body = optimize_ast(node->body);
        return node;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    case NODE_CALL:
        for (NodeList *l = node->args; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    case NODE_FUNC_DEF:
        node->body = optimize_ast(node->body);
        return node;
    case NODE_PROGRAM:
        for (NodeList *l = node->stmts; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    default:
        return node;
    }
}

/* ============================================================
 * Peephole optimization: emitted-asm level
 * ============================================================ */

#define MAX_LINES 65536
static char *lines[MAX_LINES];
static int n_lines;

static void split_lines(const char *buf, int len) {
    n_lines = 0;
    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', end - p);
        int linelen = nl ? (nl - p + 1) : (end - p);
        char *line = malloc(linelen + 1);
        memcpy(line, p, linelen);
        line[linelen] = '\0';
        lines[n_lines++] = line;
        if (!nl) break;
        p = nl + 1;
    }
}

/* Is this line an executable instruction?
   - Instruction: starts with whitespace, first non-blank char is not '.'
                  (e.g. "  pushq %rax")
   - Not an instruction: label (no leading whitespace, e.g. "main:" ".LS0:"),
                         directive ("  .text" "  .section ..." "  .byte ..."),
                         or blank line. */
static int is_instr_line(const char *line) {
    if (!line) return 0;
    if (line[0] != ' ' && line[0] != '\t') return 0;  /* label */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '.') return 0;                          /* directive */
    if (*p == '\n' || *p == '\0') return 0;           /* blank line */
    return 1;
}

/* Peephole patterns:
   1) pushq %rax; popq %rax     → erase both
   2) pushq %rax; popq REG      → movq %rax, REG */
static void peephole_pushpop(void) {
    for (int i = 0; i + 1 < n_lines; i++) {
        if (!lines[i] || !lines[i+1]) continue;
        if (strcmp(lines[i], "  pushq %rax\n") != 0) continue;

        /* pushq %rax; popq %rax → erase */
        if (strcmp(lines[i+1], "  popq %rax\n") == 0) {
            free(lines[i]);  lines[i]   = NULL;
            free(lines[i+1]); lines[i+1] = NULL;
            i++;
            continue;
        }
        /* pushq %rax; popq REG → movq %rax, REG */
        if (strncmp(lines[i+1], "  popq ", 7) == 0) {
            char reg[16];
            if (sscanf(lines[i+1] + 7, "%15s", reg) == 1) {
                free(lines[i]);
                free(lines[i+1]);
                char *combined = malloc(64);
                snprintf(combined, 64, "  movq %%rax, %s\n", reg);
                lines[i] = combined;
                lines[i+1] = NULL;
                i++;
                continue;
            }
        }
    }
}

/* Peephole pattern:
   3) instructions from just after ret up to the next label are dead → erase */
static void peephole_dead_after_ret(void) {
    int dead = 0;
    for (int i = 0; i < n_lines; i++) {
        if (!lines[i]) continue;
        if (!dead) {
            if (strcmp(lines[i], "  ret\n") == 0) dead = 1;
            continue;
        }
        /* in dead state */
        if (!is_instr_line(lines[i])) {
            /* reached a label or section directive → leave dead state */
            dead = 0;
            continue;
        }
        /* instruction line → erase */
        free(lines[i]);
        lines[i] = NULL;
    }
}

void peephole(const char *in_buf, int in_len, FILE *out) {
    split_lines(in_buf, in_len);
    /* These patterns are order-independent. */
    peephole_pushpop();
    peephole_dead_after_ret();
    for (int i = 0; i < n_lines; i++) {
        if (lines[i]) fputs(lines[i], out);
    }
    /* free lines */
    for (int i = 0; i < n_lines; i++)
        if (lines[i]) free(lines[i]);
    n_lines = 0;
}
