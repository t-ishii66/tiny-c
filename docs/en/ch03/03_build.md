# 03 — Complete files and build

We lay out the complete ch03 files with their diffs against ch02, then build and run.

## 1. lexer.l — one line added for `=`

```flex
%{
#include "ast.h"
#include "parser.tab.h"
%}

%option noyywrap

%%
"int"       { return INT; }
"return"    { return RETURN; }
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
"+"         { return '+'; }
"-"         { return '-'; }
"*"         { return '*'; }
"/"         { return '/'; }
"%"         { return '%'; }
"="         { return '='; }                       /* added */
"("         { return '('; }
")"         { return ')'; }
"{"         { return '{'; }
"}"         { return '}'; }
";"         { return ';'; }
[ \t\n]+    { /* skip whitespace */ }
"//".*      { /* skip line comments */ }
.           { fprintf(stderr, "unknown char: %c\n", *yytext); exit(1); }
%%
```

## 2. parser.y — statements, assignment, identifiers

```yacc
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;
%}

%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}

%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN

%type <node> func_def stmt expr assign add_expr mul_expr unary primary
%type <list> stmts

%%

program
    : func_def          { program = $1; }
    ;

func_def
    : INT IDENT '(' ')' '{' stmts '}'
                        { $$ = new_func_def($2, new_block($6)); }
    ;

stmts
    : /* empty */       { $$ = NULL; }
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;

stmt
    : RETURN expr ';'              { $$ = new_return($2); }
    | INT IDENT '=' expr ';'       { $$ = new_var_decl($2, $4); }   /* added */
    | expr ';'                     { $$ = new_expr_stmt($1); }      /* added */
    ;

expr
    : assign                       { $$ = $1; }                /* level added */
    ;

assign                                                         /* added */
    : add_expr                     { $$ = $1; }                /* added */
    | add_expr '=' assign          { $$ = new_assign($1, $3); }/* added */
    ;                                                          /* added */

add_expr
    : mul_expr                     { $$ = $1; }
    | add_expr '+' mul_expr        { $$ = new_binary('+', $1, $3); }
    | add_expr '-' mul_expr        { $$ = new_binary('-', $1, $3); }
    ;

mul_expr
    : unary                        { $$ = $1; }
    | mul_expr '*' unary           { $$ = new_binary('*', $1, $3); }
    | mul_expr '/' unary           { $$ = new_binary('/', $1, $3); }
    | mul_expr '%' unary           { $$ = new_binary('%', $1, $3); }
    ;

unary
    : primary                      { $$ = $1; }
    | '-' unary                    { $$ = new_unary('-', $2); }
    ;

primary
    : INT_LIT                      { $$ = new_int_lit($1); }
    | IDENT                        { $$ = new_ident($1); }    /* added */
    | '(' expr ')'                 { $$ = $2; }
    ;

%%
```

## 3. ast.h — four new node kinds

```c
#ifndef AST_H
#define AST_H

typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_IDENT,        /* added */
    NODE_ASSIGN,       /* added */
    NODE_VAR_DECL,     /* added */
    NODE_RETURN,
    NODE_EXPR_STMT,    /* added */
    NODE_FUNC_DEF,
    NODE_BLOCK,
} NodeKind;

typedef struct Node Node;
typedef struct NodeList NodeList;

struct NodeList {
    Node *node;
    NodeList *next;
};

struct Node {
    NodeKind kind;

    int int_val;
    char op;
    Node *lhs;
    Node *rhs;
    Node *operand;
    char *name;        /* NODE_IDENT, NODE_VAR_DECL, NODE_FUNC_DEF */
    Node *body;
    Node *expr;        /* NODE_RETURN, NODE_EXPR_STMT, NODE_VAR_DECL (initializer) */
    NodeList *stmts;
};

Node *new_int_lit(int val);
Node *new_binary(char op, Node *lhs, Node *rhs);
Node *new_unary(char op, Node *operand);
Node *new_ident(char *name);                /* added */
Node *new_assign(Node *lhs, Node *rhs);     /* added */
Node *new_var_decl(char *name, Node *init); /* added */
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);            /* added */
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);

#endif
```

## 4. ast.c — four new constructors + print_ast extended

The new constructors follow the same plain pattern as before: `calloc(1, sizeof(Node))` and fill in the fields.

```c
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

Node *new_expr_stmt(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_EXPR_STMT;
    n->expr = expr;
    return n;
}
```

Add four `switch` cases to `print_ast` too — same shape as the others: print the name and recurse into children.

```c
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
case NODE_EXPR_STMT:
    printf("EXPR_STMT\n");
    print_ast(node->expr, level + 1);
    break;
```

The full version lives in `steps/ch03/src/ast.c`.

## 5. codegen.c — symbol table and four new node cases

To ch02 we add the symbol table (`LVar`, `add_local`, `find_local`), `NODE_IDENT` and `NODE_ASSIGN` in `gen_expr`, and `NODE_VAR_DECL` and `NODE_EXPR_STMT` in `gen_stmt`.

Additions at the top of the file:

```c
#include <string.h>

typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;
    LVar *next;
};

static LVar *locals;
static int frame_size;

static int add_local(char *name) {
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    frame_size += 8;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v->offset;
}

static int find_local(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v->offset;
    fprintf(stderr, "undeclared variable: %s\n", name);
    exit(1);
}
```

The `codegen` function resets the table to accumulate variables freshly:

```c
void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;          /* added: reset the table */
    frame_size = 0;         /* added */

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

Additions to `gen_expr`:

```c
case NODE_IDENT: {
    int off = find_local(node->name);
    fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
    return;
}
case NODE_ASSIGN: {
    if (node->lhs->kind != NODE_IDENT) {
        fprintf(stderr, "lhs of '=' must be a variable\n");
        exit(1);
    }
    int off = find_local(node->lhs->name);
    gen_expr(node->rhs);
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
```

Additions to `gen_stmt`:

```c
case NODE_VAR_DECL: {
    int off = add_local(node->name);
    fprintf(out, "  subq $8, %%rsp\n");
    gen_expr(node->expr);
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
case NODE_EXPR_STMT:
    gen_expr(node->expr);
    return;
```

The full file is at `steps/ch03/src/codegen.c`.

## 6. main.c / Makefile / codegen.h — no changes

Same as ch02.

## 7. Build & run

```bash
$ cd steps/ch03
$ make
mkdir -p build
gcc -Wall -g -Isrc -Ibuild -c -o build/ast.o src/ast.c
gcc -Wall -g -Isrc -Ibuild -c -o build/codegen.o src/codegen.c
gcc -Wall -g -Isrc -Ibuild -c -o build/main.o src/main.c
bison -d -o build/parser.tab.c src/parser.y
gcc -Wall -g -Isrc -Ibuild -c -o build/parser.tab.o build/parser.tab.c
flex -o build/lex.yy.c src/lexer.l
gcc -Wall -g -Isrc -Ibuild -Wno-unused-function -c -o build/lex.yy.o build/lex.yy.c
gcc -o tinyc build/ast.o build/codegen.o build/main.o build/parser.tab.o build/lex.yy.o
```

bison reports no conflicts.

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 1;
    int y = 2;
    return x + y;
}
EOF

$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
3
```

`x + y = 1 + 2 = 3`. As expected.

Try assignment too:

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 10;
    x = x + 5;
    return x;
}
EOF

$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
15
```

Chained assignment:

```bash
$ cat > test.c <<'EOF'
int main() {
    int a = 0;
    int b = 0;
    a = b = 7;
    return a + b;
}
EOF

$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
14
```

Using an undeclared variable is an error:

```bash
$ echo 'int main() { return z; }' | ./tinyc /dev/stdin > /dev/null
undeclared variable: z
```

So is redeclaring one:

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 1;
    int x = 2;
    return x;
}
EOF
$ ./tinyc test.c > /dev/null
redeclared variable: x
```

## 8. Tracing the generated assembly

Input:

```c
int main() {
    int x = 1;
    int y = 2;
    return x + y;
}
```

Output:

```
  .text
  .globl main
main:
  pushq %rbp           # ─── prologue ───
  movq %rsp, %rbp
  subq $8, %rsp        # ─── int x = 1; ───
  movl $1, %eax
  movl %eax, -8(%rbp)  #   store 1 into x's slot (-8)
  subq $8, %rsp        # ─── int y = 2; ───
  movl $2, %eax
  movl %eax, -16(%rbp) #   store 2 into y's slot (-16)
  movl -16(%rbp), %eax # ─── return x + y; ───
                       #   read rhs (= y) first
  pushq %rax           #   save y's value
  movl -8(%rbp), %eax  #   read x
  popq %rcx            #   ecx = y, eax = x
  addl %ecx, %eax      #   eax = x + y = 3
  leave                # ─── epilogue ───
  ret
```

## 9. AST ↔ assembly correspondence

```
FUNC_DEF main                       <-- main: label + prologue + epilogue
  BLOCK                              <-- emit each contained statement in order
    VAR_DECL x                       <-- subq $8, %rsp + movl ...
      INT_LIT 1
    VAR_DECL y                       <-- subq $8, %rsp + movl ...
      INT_LIT 2
    RETURN                           <-- gen_expr + leave + ret
      BINARY +                       <-- stack-based binary op
        IDENT x                      <-- movl -8(%rbp), %eax
        IDENT y                      <-- movl -16(%rbp), %eax
```

Compared to ch02, the only difference is that leaves can be `IDENT` instead of `INT_LIT`. From `gen_expr`'s perspective, `INT_LIT` becomes `movl $constant, %eax` and `IDENT` becomes `movl address, %eax`. Both honor the same contract: "leave the value in `%eax`."

## 10. Next

Chapter 4 adds **branches (`if`/`else`)** and **loops (`while`)**, the comparison operators (`< <= > >= == !=`), and logical negation `!`. The new codegen tools are **labels** and **conditional jumps**.
