#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    /* Literals */
    ND_INT_LIT,     /* integer literal */
    ND_CHAR_LIT,    /* character literal */
    ND_STRING_LIT,  /* string literal */
    ND_IDENT,       /* identifier (variable reference) */

    /* Binary operators */
    ND_ADD,         /* + */
    ND_SUB,         /* - */
    ND_MUL,         /* * */
    ND_DIV,         /* / */
    ND_MOD,         /* % */
    ND_EQ,          /* == */
    ND_NE,          /* != */
    ND_LT,          /* < */
    ND_LE,          /* <= */
    ND_GT,          /* > */
    ND_GE,          /* >= */

    /* Unary operators */
    ND_NEG,         /* -x (unary minus) */
    ND_NOT,         /* !x */
    ND_DEREF,       /* *p (dereference) */
    ND_ADDR,        /* &x (address-of) */

    /* Assignment */
    ND_ASSIGN,      /* = */

    /* Array access */
    ND_INDEX,       /* a[i] */

    /* Statements */
    ND_RETURN,      /* return */
    ND_IF,          /* if / if-else */
    ND_WHILE,       /* while */
    ND_BLOCK,       /* { ... } */
    ND_EXPR_STMT,   /* expression statement */

    /* Declarations */
    ND_VAR_DECL,    /* variable declaration */
    ND_ARRAY_DECL,  /* array declaration */
    ND_FUNC_DEF,    /* function definition */
    ND_PARAM,       /* function parameter */

    /* Function call */
    ND_CALL,        /* function call */

    /* Lists (internal) */
    ND_PROGRAM,     /* top-level program */
} NodeKind;

/* Type representation */
typedef enum {
    TY_INT,
    TY_CHAR,
    TY_VOID,
    TY_PTR_INT,     /* int* */
    TY_PTR_CHAR,    /* char* */
} Type;

/* AST node */
typedef struct Node Node;

/* Linked list of nodes */
typedef struct NodeList NodeList;
struct NodeList {
    Node *node;
    NodeList *next;
};

struct Node {
    NodeKind kind;
    Type type;          /* type annotation */

    /* ND_INT_LIT */
    int int_val;

    /* ND_CHAR_LIT */
    char char_val;

    /* ND_STRING_LIT, ND_IDENT, ND_CALL, ND_FUNC_DEF */
    char *name;

    /* Binary op: lhs OP rhs */
    /* ND_ASSIGN: lhs = rhs */
    /* ND_INDEX: lhs[rhs] */
    /* ND_IF: lhs=cond, rhs=then, extra=else */
    /* ND_WHILE: lhs=cond, rhs=body */
    /* ND_RETURN: lhs=expr (NULL for void return) */
    /* ND_NEG/ND_NOT/ND_DEREF/ND_ADDR: lhs=operand */
    /* ND_EXPR_STMT: lhs=expr */
    Node *lhs;
    Node *rhs;
    Node *extra;        /* else branch */

    Node *body;

    /* ND_BLOCK: list of statements */
    /* ND_CALL: list of arguments */
    /* ND_FUNC_DEF: list of params (args), body=body */
    /* ND_PROGRAM: list of top-level definitions */
    NodeList *children;

    /* ND_VAR_DECL: name, type, lhs=initializer (or NULL) */
    /* ND_ARRAY_DECL: name, type, array_size */
    int array_size;
};

/* Constructor functions */
Node *new_node(NodeKind kind);
Node *new_int_lit(int val);
Node *new_char_lit(char val);
Node *new_string_lit(char *str);
Node *new_ident(char *name);
Node *new_binary(NodeKind kind, Node *lhs, Node *rhs);
Node *new_unary(NodeKind kind, Node *operand);
Node *new_call(char *name, NodeList *args);
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug: print AST */
void print_ast(Node *node, int indent);

#endif
