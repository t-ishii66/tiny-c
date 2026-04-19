#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

Node *new_node(NodeKind kind) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = kind;
    return n;
}

Node *new_int_lit(int val) {
    Node *n = new_node(ND_INT_LIT);
    n->int_val = val;
    return n;
}

Node *new_char_lit(char val) {
    Node *n = new_node(ND_CHAR_LIT);
    n->char_val = val;
    return n;
}

Node *new_string_lit(char *str) {
    Node *n = new_node(ND_STRING_LIT);
    n->name = str;
    return n;
}

Node *new_ident(char *name) {
    Node *n = new_node(ND_IDENT);
    n->name = name;
    return n;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *n = new_node(kind);
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

Node *new_unary(NodeKind kind, Node *operand) {
    Node *n = new_node(kind);
    n->lhs = operand;
    return n;
}

Node *new_call(char *name, NodeList *args) {
    Node *n = new_node(ND_CALL);
    n->name = name;
    n->children = args;
    return n;
}

NodeList *new_node_list(Node *node, NodeList *next) {
    NodeList *l = calloc(1, sizeof(NodeList));
    l->node = node;
    l->next = next;
    return l;
}

/* Helper: print indentation */
static void indent(int level) {
    for (int i = 0; i < level; i++)
        printf("  ");
}

static const char *type_name(Type t) {
    switch (t) {
    case TY_INT:      return "int";
    case TY_CHAR:     return "char";
    case TY_VOID:     return "void";
    case TY_PTR_INT:  return "int*";
    case TY_PTR_CHAR: return "char*";
    default:          return "?";
    }
}

void print_ast(Node *node, int indent_level) {
    if (!node) return;

    indent(indent_level);

    switch (node->kind) {
    case ND_INT_LIT:
        printf("IntLit(%d)\n", node->int_val);
        break;
    case ND_CHAR_LIT:
        printf("CharLit('%c')\n", node->char_val);
        break;
    case ND_STRING_LIT:
        printf("StringLit(\"%s\")\n", node->name);
        break;
    case ND_IDENT:
        printf("Ident(%s)\n", node->name);
        break;
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: case ND_GT: case ND_GE: {
        const char *ops[] = {
            [ND_ADD] = "+", [ND_SUB] = "-", [ND_MUL] = "*", [ND_DIV] = "/",
            [ND_MOD] = "%", [ND_EQ] = "==", [ND_NE] = "!=",
            [ND_LT] = "<", [ND_LE] = "<=", [ND_GT] = ">", [ND_GE] = ">=",
        };
        printf("BinOp(%s)\n", ops[node->kind]);
        print_ast(node->lhs, indent_level + 1);
        print_ast(node->rhs, indent_level + 1);
        break;
    }
    case ND_ASSIGN:
        printf("Assign\n");
        print_ast(node->lhs, indent_level + 1);
        print_ast(node->rhs, indent_level + 1);
        break;
    case ND_NEG:
        printf("Neg\n");
        print_ast(node->lhs, indent_level + 1);
        break;
    case ND_NOT:
        printf("Not\n");
        print_ast(node->lhs, indent_level + 1);
        break;
    case ND_DEREF:
        printf("Deref\n");
        print_ast(node->lhs, indent_level + 1);
        break;
    case ND_ADDR:
        printf("Addr\n");
        print_ast(node->lhs, indent_level + 1);
        break;
    case ND_INDEX:
        printf("Index\n");
        print_ast(node->lhs, indent_level + 1);
        print_ast(node->rhs, indent_level + 1);
        break;
    case ND_CALL:
        printf("Call(%s)\n", node->name);
        for (NodeList *l = node->children; l; l = l->next)
            print_ast(l->node, indent_level + 1);
        break;
    case ND_RETURN:
        printf("Return\n");
        if (node->lhs)
            print_ast(node->lhs, indent_level + 1);
        break;
    case ND_IF:
        printf("If\n");
        indent(indent_level + 1); printf("[cond]\n");
        print_ast(node->lhs, indent_level + 2);
        indent(indent_level + 1); printf("[then]\n");
        print_ast(node->rhs, indent_level + 2);
        if (node->extra) {
            indent(indent_level + 1); printf("[else]\n");
            print_ast(node->extra, indent_level + 2);
        }
        break;
    case ND_WHILE:
        printf("While\n");
        indent(indent_level + 1); printf("[cond]\n");
        print_ast(node->lhs, indent_level + 2);
        indent(indent_level + 1); printf("[body]\n");
        print_ast(node->rhs, indent_level + 2);
        break;
    case ND_BLOCK:
        printf("Block\n");
        for (NodeList *l = node->children; l; l = l->next)
            print_ast(l->node, indent_level + 1);
        break;
    case ND_EXPR_STMT:
        printf("ExprStmt\n");
        print_ast(node->lhs, indent_level + 1);
        break;
    case ND_VAR_DECL:
        printf("VarDecl(%s : %s)\n", node->name, type_name(node->type));
        if (node->lhs) {
            indent(indent_level + 1); printf("[init]\n");
            print_ast(node->lhs, indent_level + 2);
        }
        break;
    case ND_ARRAY_DECL:
        printf("ArrayDecl(%s : %s[%d])\n", node->name, type_name(node->type), node->array_size);
        break;
    case ND_FUNC_DEF:
        printf("FuncDef(%s : %s)\n", node->name, type_name(node->type));
        indent(indent_level + 1); printf("[params]\n");
        for (NodeList *l = node->children; l; l = l->next)
            print_ast(l->node, indent_level + 2);
        indent(indent_level + 1); printf("[body]\n");
        print_ast(node->body, indent_level + 2);
        break;
    case ND_PARAM:
        printf("Param(%s : %s)\n", node->name, type_name(node->type));
        break;
    case ND_PROGRAM:
        printf("Program\n");
        for (NodeList *l = node->children; l; l = l->next)
            print_ast(l->node, indent_level + 1);
        break;
    }
}
