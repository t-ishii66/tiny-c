#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_INT_LIT;
    n->int_val = val;
    return n;
}

Node *new_return(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_RETURN;
    n->expr = expr;
    return n;
}

Node *new_block(NodeList *stmts) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_BLOCK;
    n->stmts = stmts;
    return n;
}

Node *new_func_def(char *name, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_FUNC_DEF;
    n->name = name;
    n->body = body;
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

void print_ast(Node *node, int level) {
    if (!node) return;
    indent(level);
    switch (node->kind) {
    case NODE_INT_LIT:
        printf("INT_LIT %d\n", node->int_val);
        break;
    case NODE_RETURN:
        printf("RETURN\n");
        print_ast(node->expr, level + 1);
        break;
    case NODE_FUNC_DEF:
        printf("FUNC_DEF %s\n", node->name);
        print_ast(node->body, level + 1);
        break;
    case NODE_BLOCK:
        printf("BLOCK\n");
        for (NodeList *l = node->stmts; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    }
}
