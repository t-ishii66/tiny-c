#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,    /* integer literal: 42 */
    NODE_RETURN,     /* return statement */
    NODE_FUNC_DEF,   /* function definition */
    NODE_BLOCK,      /* { ... } */
} NodeKind;

/* Forward declaration */
typedef struct Node Node;
typedef struct NodeList NodeList;

/* Linked list of nodes */
struct NodeList {
    Node *node;
    NodeList *next;
};

/* AST node */
struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT: value */
    char *name;        /* NODE_FUNC_DEF: function name */
    Node *body;        /* NODE_FUNC_DEF: function body (block) */
    Node *expr;        /* NODE_RETURN: return value */
    NodeList *stmts;   /* NODE_BLOCK: list of statements */
};

/* Constructors */
Node *new_int_lit(int val);
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

/* List operations */
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug output */
void print_ast(Node *node, int indent);

#endif
