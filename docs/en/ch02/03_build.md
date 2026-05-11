# 03 — Complete files and build

Let's lay out the full ch02 source alongside its diff against ch01, then build, run, and trace the generated assembly.

## 1. lexer.l — 5 tokens added

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

Just five lines different from ch01: tokens for `+ - * / %`.

## 2. parser.y — introduce the expression hierarchy

```yacc
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;  /* parser result */
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

%type <node> func_def stmt expr add_expr mul_expr unary primary
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
    : RETURN expr ';'   { $$ = new_return($2); }
    ;

expr
    : add_expr          { $$ = $1; }                            /* changed: in ch01 this was : INT_LIT */
    ;

add_expr                                                        /* added */
    : mul_expr                  { $$ = $1; }                    /* added */
    | add_expr '+' mul_expr     { $$ = new_binary('+', $1, $3); }/* added */
    | add_expr '-' mul_expr     { $$ = new_binary('-', $1, $3); }/* added */
    ;                                                           /* added */

mul_expr                                                        /* added */
    : unary                     { $$ = $1; }                    /* added */
    | mul_expr '*' unary        { $$ = new_binary('*', $1, $3); }/* added */
    | mul_expr '/' unary        { $$ = new_binary('/', $1, $3); }/* added */
    | mul_expr '%' unary        { $$ = new_binary('%', $1, $3); }/* added */
    ;                                                           /* added */

unary                                                           /* added */
    : primary                   { $$ = $1; }                    /* added */
    | '-' unary                 { $$ = new_unary('-', $2); }    /* added */
    ;                                                           /* added */

primary                                                         /* added */
    : INT_LIT                   { $$ = new_int_lit($1); }       /* added */
    | '(' expr ')'              { $$ = $2; }                    /* added */
    ;                                                           /* added */

%%
```

In ch01 we had a single line `expr : INT_LIT`. In ch02 we expand it into five levels. We also add `add_expr mul_expr unary primary` to the `%type` declarations.

## 3. ast.h — 2 new node kinds

```c
#ifndef AST_H
#define AST_H

typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,     /* added: lhs op rhs */
    NODE_UNARY,      /* added: op operand */
    NODE_RETURN,
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

    int int_val;       /* NODE_INT_LIT */
    char op;           /* NODE_BINARY, NODE_UNARY: added */
    Node *lhs;         /* NODE_BINARY: added */
    Node *rhs;         /* NODE_BINARY: added */
    Node *operand;     /* NODE_UNARY:  added */
    char *name;        /* NODE_FUNC_DEF */
    Node *body;        /* NODE_FUNC_DEF */
    Node *expr;        /* NODE_RETURN */
    NodeList *stmts;   /* NODE_BLOCK */
};

Node *new_int_lit(int val);
Node *new_binary(char op, Node *lhs, Node *rhs);   /* added */
Node *new_unary(char op, Node *operand);           /* added */
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);

#endif
```

Two new entries in `NodeKind`, four new fields, two new constructor declarations. Since the `Node` struct is just "all fields for all kinds laid out flat," we add a new kind by simply adding fields.

## 4. ast.c — two new constructors + print_ast extended

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_INT_LIT;
    n->int_val = val;
    return n;
}

Node *new_binary(char op, Node *lhs, Node *rhs) {                /* added */
    Node *n = calloc(1, sizeof(Node));                           /* added */
    n->kind = NODE_BINARY;                                       /* added */
    n->op = op;                                                  /* added */
    n->lhs = lhs;                                                /* added */
    n->rhs = rhs;                                                /* added */
    return n;                                                    /* added */
}                                                                /* added */

Node *new_unary(char op, Node *operand) {                        /* added */
    Node *n = calloc(1, sizeof(Node));                           /* added */
    n->kind = NODE_UNARY;                                        /* added */
    n->op = op;                                                  /* added */
    n->operand = operand;                                        /* added */
    return n;                                                    /* added */
}                                                                /* added */

Node *new_return(Node *expr) { /* same as ch01 */ }
Node *new_block(NodeList *stmts) { /* same as ch01 */ }
Node *new_func_def(char *name, Node *body) { /* same as ch01 */ }
NodeList *new_node_list(Node *node, NodeList *next) { /* same as ch01 */ }

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
    case NODE_BINARY:                                            /* added */
        printf("BINARY %c\n", node->op);                         /* added */
        print_ast(node->lhs, level + 1);                         /* added */
        print_ast(node->rhs, level + 1);                         /* added */
        break;                                                   /* added */
    case NODE_UNARY:                                             /* added */
        printf("UNARY %c\n", node->op);                          /* added */
        print_ast(node->operand, level + 1);                     /* added */
        break;                                                   /* added */
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
```

The real source omits nothing (see `steps/ch02/src/ast.c`). Here, to make the diff stand out, the parts identical to ch01 are elided.

## 5. codegen.c — 2 cases added to gen_expr

`gen_stmt` and `codegen` are identical to ch01. Only `gen_expr` grows.

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;

static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_UNARY:                                                  /* added */
        gen_expr(node->operand);                                      /* added */
        fprintf(out, "  negl %%eax\n");                               /* added */
        return;                                                       /* added */
    case NODE_BINARY:                                                 /* added */
        gen_expr(node->rhs);                                          /* added */
        fprintf(out, "  pushq %%rax\n");                              /* added */
        gen_expr(node->lhs);                                          /* added */
        fprintf(out, "  popq %%rcx\n");                               /* added */
        switch (node->op) {                                           /* added */
        case '+': fprintf(out, "  addl %%ecx, %%eax\n"); return;      /* added */
        case '-': fprintf(out, "  subl %%ecx, %%eax\n"); return;      /* added */
        case '*': fprintf(out, "  imull %%ecx, %%eax\n"); return;     /* added */
        case '/':                                                     /* added */
            fprintf(out, "  cdq\n");                                  /* added */
            fprintf(out, "  idivl %%ecx\n");                          /* added */
            return;                                                   /* added */
        case '%':                                                     /* added */
            fprintf(out, "  cdq\n");                                  /* added */
            fprintf(out, "  idivl %%ecx\n");                          /* added */
            fprintf(out, "  movl %%edx, %%eax\n");                    /* added */
            return;                                                   /* added */
        }
        fprintf(stderr, "unknown binary op: %c\n", node->op);         /* added */
        exit(1);                                                      /* added */
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

static void gen_stmt(Node *node) { /* same as ch01 */ }
void codegen(Node *prog, FILE *output) { /* same as ch01 */ }
```

## 6. main.c / Makefile / codegen.h — no changes

We use the ones from ch01 unchanged.

## 7. Build & run

```bash
$ cd steps/ch02
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

Built.

```bash
$ cat > test.c <<'EOF'
int main() {
    return 2 + 3 * 4;
}
EOF

$ ./tinyc --dump-ast test.c
FUNC_DEF main
  BLOCK
    RETURN
      BINARY +
        INT_LIT 2
        BINARY *
          INT_LIT 3
          INT_LIT 4

$ ./tinyc test.c > test.s
$ gcc -o test test.s
$ ./test ; echo $?
14
```

It works. `return 2 + 3 * 4;` really does return `14`.

Other examples to try:

```bash
$ echo 'int main() { return (10 - 3) * 2 + 100 / 4 % 7; }' > test.c
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
18
# Order of evaluation:
#   (10-3)        = 7
#   7 * 2         = 14
#   100 / 4       = 25
#   25 % 7        = 4    (% is left-associative, so (100/4)%7)
#   14 + 4        = 18
```

Checked with `--dump-ast`, you'll see a `*` clump on the left of `+` and a `%` clump on the right, each standing on its own. The grammar's layering shows up directly in the shape of the AST.

## 8. Tracing the generated assembly

Read the assembly emitted for `return 2 + 3 * 4;` from top to bottom.

```
  .text
  .globl main
main:
  pushq %rbp           # prologue
  movq %rsp, %rbp
  movl $4, %eax        # ── expression evaluation starts here ──
  pushq %rax           # save 4 on the stack (rhs of BINARY *)
  movl $3, %eax        # eax = 3 (lhs of BINARY *)
  popq %rcx            # ecx = 4 (the rhs we saved)
  imull %ecx, %eax     # eax = 3 * 4 = 12   ← result of BINARY *
  pushq %rax           # save 12 on the stack (rhs of BINARY +)
  movl $2, %eax        # eax = 2 (lhs of BINARY +)
  popq %rcx            # ecx = 12 (the rhs we saved)
  addl %ecx, %eax      # eax = 2 + 12 = 14  ← result of BINARY +
  leave                # epilogue
  ret                  # the 14 in eax becomes main's return value
```

The stack moves like this (left = older, right = newer):

```
time 0: []
time 1: pushq %rax (=4)         → [4]
time 2: popq  %rcx               → []         (rcx=4, used in the computation)
time 3: pushq %rax (=12)         → [12]
time 4: popq  %rcx               → []         (rcx=12)
```

Every pair balances perfectly. By the time `leave` runs, the stack is back to where it was right after `pushq %rbp`.

The AST-to-assembly correspondence is also clean:

```
BINARY +                       <-- the final addl
├─ INT_LIT 2                   <-- "movl $2, %eax"   (second one)
└─ BINARY *                    <-- the inner imull
   ├─ INT_LIT 3                <-- "movl $3, %eax"
   └─ INT_LIT 4                <-- "movl $4, %eax"   (first one)
```

Leaves (`INT_LIT`) become `movl $N, %eax`; inner nodes (`BINARY`) become their operation instructions. The instructions come out in **postorder** traversal.

## 9. Next

Chapter 3 adds **variables**. Expressions like `int x = 1; int y = 2; return x + y;` start working. We make room for variables on the stack frame and put in the machinery to turn names into addresses.
