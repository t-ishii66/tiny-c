#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,
    NODE_CHAR_LIT,        /* 'a' */
    NODE_STRING_LIT,      /* "hello" */
    NODE_BINARY,
    NODE_UNARY,           /* - ! &(addr) *(deref) */
    NODE_IDENT,
    NODE_ASSIGN,
    NODE_VAR_DECL,        /* local: type IDENT = init; or type IDENT[N]; */
    NODE_GLOBAL_VAR_DECL, /* global: type IDENT; or type IDENT[N]; */
    NODE_INDEX,           /* a[i] */
    NODE_RETURN,
    NODE_EXPR_STMT,
    NODE_IF,
    NODE_WHILE,
    NODE_CALL,
    NODE_FUNC_DEF,
    NODE_BLOCK,
    NODE_PROGRAM,
} NodeKind;

/* Operator codes for multi-character operators */
enum {
    OP_LE = 256,
    OP_GE,
    OP_EQ,
    OP_NE,
};

/* Type — simple enough for tiny-c (1-level pointers, 1-D arrays).
   - is_pointer: 1 if "T *" pointer
   - is_array:   1 if "T[N]" array
   - base_size:  1 (char) or 4 (int)
   - array_size: number of elements (only when is_array)
*/
typedef struct Type Type;
struct Type {
    int is_pointer;
    int is_array;
    int base_size;
    int array_size;
};

typedef struct Node Node;
typedef struct NodeList NodeList;
typedef struct LVar LVar;   /* defined in codegen.c; AST keeps a back-pointer */

struct NodeList {
    Node *node;
    NodeList *next;
};

struct Node {
    NodeKind kind;
    int int_val;
    int op;
    Node *lhs, *rhs;
    Node *operand;
    char *name;
    Node *body;
    Node *expr;
    Node *cond;
    Node *then_body;
    Node *else_body;
    NodeList *stmts;
    NodeList *params;
    NodeList *args;
    Type *type;        /* NODE_VAR_DECL, NODE_GLOBAL_VAR_DECL, NODE_IDENT (param) */
    char *str_val;     /* NODE_STRING_LIT */
    int str_len;       /* NODE_STRING_LIT (number of chars including \0) */
    LVar *lvar;        /* NODE_VAR_DECL & param NODE_IDENT: back-pointer set by codegen */
};

/* Type constructors */
Type *type_int(void);          /* int */
Type *type_char(void);         /* char */
Type *type_ptr(int base_size); /* int* or char* */
Type *type_array(int base_size, int n); /* int[n] or char[n] */
int type_size(Type *t);        /* total bytes occupied */
int elem_size(Type *t);        /* element size for ptr/array; same as size for scalar */

/* Constructors */
Node *new_int_lit(int val);
Node *new_char_lit(int val);
Node *new_string_lit(char *str, int len);
Node *new_binary(int op, Node *lhs, Node *rhs);
Node *new_unary(int op, Node *operand);
Node *new_ident(char *name);
Node *new_assign(Node *lhs, Node *rhs);
Node *new_var_decl(char *name, Type *type, Node *init);
Node *new_global_var_decl(char *name, Type *type);
Node *new_index(Node *base, Node *idx);
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);
Node *new_if(Node *cond, Node *then_body, Node *else_body);
Node *new_while(Node *cond, Node *body);
Node *new_call(char *name, NodeList *args);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, NodeList *params, Node *body);
Node *new_program(NodeList *funcs);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);

#endif
