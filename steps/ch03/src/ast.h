#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,    /* integer literal: 42 */
    NODE_BINARY,     /* binary op: lhs op rhs */
    NODE_UNARY,      /* unary op: op operand */
    NODE_IDENT,      /* identifier reference: x */
    NODE_ASSIGN,     /* assignment: lhs = rhs */
    NODE_VAR_DECL,   /* local variable declaration: int x = init; */
    NODE_RETURN,     /* return statement */
    NODE_EXPR_STMT,  /* expression statement: expr ; */
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

    int int_val;       /* NODE_INT_LIT */
    char op;           /* NODE_BINARY, NODE_UNARY */
    Node *lhs;         /* NODE_BINARY, NODE_ASSIGN: left */
    Node *rhs;         /* NODE_BINARY, NODE_ASSIGN: right */
    Node *operand;     /* NODE_UNARY */
    char *name;        /* NODE_IDENT, NODE_VAR_DECL, NODE_FUNC_DEF */
    Node *body;        /* NODE_FUNC_DEF */
    Node *expr;        /* NODE_RETURN, NODE_EXPR_STMT, NODE_VAR_DECL (initializer) */
    NodeList *stmts;   /* NODE_BLOCK */
};

/* Constructors */
Node *new_int_lit(int val);
Node *new_binary(char op, Node *lhs, Node *rhs);
Node *new_unary(char op, Node *operand);
Node *new_ident(char *name);
Node *new_assign(Node *lhs, Node *rhs);
Node *new_var_decl(char *name, Node *init);
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

/* List operations */
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug output */
void print_ast(Node *node, int indent);

#endif
