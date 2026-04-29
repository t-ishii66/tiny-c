#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_IDENT,
    NODE_ASSIGN,
    NODE_VAR_DECL,
    NODE_RETURN,
    NODE_EXPR_STMT,
    NODE_IF,
    NODE_WHILE,
    NODE_CALL,         /* function call: name(args...) */
    NODE_FUNC_DEF,     /* function definition: name(params) { body } */
    NODE_BLOCK,
    NODE_PROGRAM,      /* top level: list of function definitions */
} NodeKind;

/* Operator codes for multi-character operators */
enum {
    OP_LE = 256,
    OP_GE,
    OP_EQ,
    OP_NE,
};

typedef struct Node Node;
typedef struct NodeList NodeList;

struct NodeList {
    Node *node;
    NodeList *next;
};

struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT */
    int op;            /* NODE_BINARY, NODE_UNARY */
    Node *lhs, *rhs;   /* NODE_BINARY, NODE_ASSIGN */
    Node *operand;     /* NODE_UNARY */
    char *name;        /* NODE_IDENT, NODE_VAR_DECL, NODE_FUNC_DEF, NODE_CALL */
    Node *body;        /* NODE_FUNC_DEF, NODE_WHILE */
    Node *expr;        /* NODE_RETURN, NODE_EXPR_STMT, NODE_VAR_DECL (init) */
    Node *cond;        /* NODE_IF, NODE_WHILE */
    Node *then_body;   /* NODE_IF */
    Node *else_body;   /* NODE_IF */
    NodeList *stmts;   /* NODE_BLOCK, NODE_PROGRAM */
    NodeList *params;  /* NODE_FUNC_DEF: list of NODE_IDENT (parameter names) */
    NodeList *args;    /* NODE_CALL: list of expression nodes */
};

/* Constructors */
Node *new_int_lit(int val);
Node *new_binary(int op, Node *lhs, Node *rhs);
Node *new_unary(int op, Node *operand);
Node *new_ident(char *name);
Node *new_assign(Node *lhs, Node *rhs);
Node *new_var_decl(char *name, Node *init);
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);
Node *new_if(Node *cond, Node *then_body, Node *else_body);
Node *new_while(Node *cond, Node *body);
Node *new_call(char *name, NodeList *args);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, NodeList *params, Node *body);
Node *new_program(NodeList *funcs);

/* List operations */
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug output */
void print_ast(Node *node, int indent);

#endif
