#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_INT_LIT;
    n->int_val = val;
    return n;
}

Node *new_binary(int op, Node *lhs, Node *rhs) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_BINARY;
    n->op = op;
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

Node *new_unary(int op, Node *operand) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_UNARY;
    n->op = op;
    n->operand = operand;
    return n;
}

Node *new_ident(char *name) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_IDENT;
    n->name = name;
    return n;
}

Node *new_assign(Node *lhs, Node *rhs) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_ASSIGN;
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

Node *new_var_decl(char *name, Node *init) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_VAR_DECL;
    n->name = name;
    n->expr = init;
    return n;
}

Node *new_return(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_RETURN;
    n->expr = expr;
    return n;
}

Node *new_expr_stmt(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_EXPR_STMT;
    n->expr = expr;
    return n;
}

Node *new_if(Node *cond, Node *then_body, Node *else_body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_IF;
    n->cond = cond;
    n->then_body = then_body;
    n->else_body = else_body;
    return n;
}

Node *new_while(Node *cond, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_WHILE;
    n->cond = cond;
    n->body = body;
    return n;
}

Node *new_call(char *name, NodeList *args) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_CALL;
    n->name = name;
    n->args = args;
    return n;
}

Node *new_block(NodeList *stmts) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_BLOCK;
    n->stmts = stmts;
    return n;
}

Node *new_func_def(char *name, NodeList *params, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_FUNC_DEF;
    n->name = name;
    n->params = params;
    n->body = body;
    return n;
}

Node *new_program(NodeList *funcs) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_PROGRAM;
    n->stmts = funcs;
    return n;
}

NodeList *new_node_list(Node *node, NodeList *next) {
    NodeList *l = calloc(1, sizeof(NodeList));
    l->node = node;
    l->next = next;
    return l;
}

static void indent(int level) {
    for (int i = 0; i < level; i++) printf("  ");
}

static const char *op_str(int op) {
    static char buf[4];
    switch (op) {
    case OP_LE: return "<=";
    case OP_GE: return ">=";
    case OP_EQ: return "==";
    case OP_NE: return "!=";
    default:
        buf[0] = (char)op;
        buf[1] = '\0';
        return buf;
    }
}

void print_ast(Node *node, int level) {
    if (!node) return;
    indent(level);
    switch (node->kind) {
    case NODE_INT_LIT:
        printf("INT_LIT %d\n", node->int_val);
        break;
    case NODE_BINARY:
        printf("BINARY %s\n", op_str(node->op));
        print_ast(node->lhs, level + 1);
        print_ast(node->rhs, level + 1);
        break;
    case NODE_UNARY:
        printf("UNARY %s\n", op_str(node->op));
        print_ast(node->operand, level + 1);
        break;
    case NODE_IDENT:
        printf("IDENT %s\n", node->name);
        break;
    case NODE_ASSIGN:
        printf("ASSIGN\n");
        print_ast(node->lhs, level + 1);
        print_ast(node->rhs, level + 1);
        break;
    case NODE_VAR_DECL:
        printf("VAR_DECL %s\n", node->name);
        print_ast(node->expr, level + 1);
        break;
    case NODE_RETURN:
        printf("RETURN\n");
        print_ast(node->expr, level + 1);
        break;
    case NODE_EXPR_STMT:
        printf("EXPR_STMT\n");
        print_ast(node->expr, level + 1);
        break;
    case NODE_IF:
        printf("IF\n");
        print_ast(node->cond, level + 1);
        print_ast(node->then_body, level + 1);
        if (node->else_body) print_ast(node->else_body, level + 1);
        break;
    case NODE_WHILE:
        printf("WHILE\n");
        print_ast(node->cond, level + 1);
        print_ast(node->body, level + 1);
        break;
    case NODE_CALL:
        printf("CALL %s\n", node->name);
        for (NodeList *l = node->args; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    case NODE_FUNC_DEF:
        printf("FUNC_DEF %s\n", node->name);
        for (NodeList *l = node->params; l; l = l->next) {
            indent(level + 1);
            printf("PARAM %s\n", l->node->name);
        }
        print_ast(node->body, level + 1);
        break;
    case NODE_BLOCK:
        printf("BLOCK\n");
        for (NodeList *l = node->stmts; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    case NODE_PROGRAM:
        printf("PROGRAM\n");
        for (NodeList *l = node->stmts; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    }
}
