# 04 — Complete files and build

We lay out the complete ch04 files with their diffs against ch03, then build and run factorial and branching examples.

## 1. lexer.l — new keywords and operators

Add three keywords (`if`/`else`/`while`), four multi-character operators, and three single-character symbols to the ch03 lexer.

```flex
%{
#include "ast.h"
#include "parser.tab.h"
%}

%option noyywrap

%%
"int"       { return INT; }
"return"    { return RETURN; }
"if"        { return IF; }                          /* added */
"else"      { return ELSE; }                        /* added */
"while"     { return WHILE; }                       /* added */
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
"=="        { return EQ_OP; }                       /* added */
"!="        { return NE_OP; }                       /* added */
"<="        { return LE_OP; }                       /* added */
">="        { return GE_OP; }                       /* added */
"+"         { return '+'; }
"-"         { return '-'; }
"*"         { return '*'; }
"/"         { return '/'; }
"%"         { return '%'; }
"="         { return '='; }
"<"         { return '<'; }                         /* added */
">"         { return '>'; }                         /* added */
"!"         { return '!'; }                         /* added */
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

The multi-character tokens (`==`, etc.) can sit alongside the single-character versions (`=`, etc.). Longest-match in flex makes `==` win. The order doesn't change meaning here, but pairing them visually is tidier.

## 2. parser.y — hierarchy and statement additions

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
%token INT RETURN IF ELSE WHILE                     /* added: IF ELSE WHILE */
%token EQ_OP NE_OP LE_OP GE_OP                      /* added */

%type <node> func_def stmt expr assign equality relational add_expr mul_expr unary primary
%type <list> stmts

%expect 1                                            /* added: dangling-else */

%%

program     : func_def          { program = $1; } ;

func_def    : INT IDENT '(' ')' '{' stmts '}'
                                { $$ = new_func_def($2, new_block($6)); } ;

stmts       : /* empty */       { $$ = NULL; }
            | stmt stmts        { $$ = new_node_list($1, $2); } ;

stmt
    : RETURN expr ';'                       { $$ = new_return($2); }
    | INT IDENT '=' expr ';'                { $$ = new_var_decl($2, $4); }
    | expr ';'                              { $$ = new_expr_stmt($1); }
    | '{' stmts '}'                         { $$ = new_block($2); }       /* added */
    | IF '(' expr ')' stmt                  { $$ = new_if($3, $5, NULL); }/* added */
    | IF '(' expr ')' stmt ELSE stmt        { $$ = new_if($3, $5, $7); }  /* added */
    | WHILE '(' expr ')' stmt               { $$ = new_while($3, $5); }   /* added */
    ;

expr        : assign            { $$ = $1; } ;

assign
    : equality                              { $$ = $1; }
    | equality '=' assign                   { $$ = new_assign($1, $3); }
    ;

equality                                                              /* added */
    : relational                            { $$ = $1; }              /* added */
    | equality EQ_OP relational             { $$ = new_binary(OP_EQ, $1, $3); } /* added */
    | equality NE_OP relational             { $$ = new_binary(OP_NE, $1, $3); } /* added */
    ;                                                                 /* added */

relational                                                            /* added */
    : add_expr                              { $$ = $1; }              /* added */
    | relational '<' add_expr               { $$ = new_binary('<', $1, $3); }   /* added */
    | relational LE_OP add_expr             { $$ = new_binary(OP_LE, $1, $3); } /* added */
    | relational '>' add_expr               { $$ = new_binary('>', $1, $3); }   /* added */
    | relational GE_OP add_expr             { $$ = new_binary(OP_GE, $1, $3); } /* added */
    ;                                                                 /* added */

add_expr    : ...   /* same as ch02 */
mul_expr    : ...
unary
    : primary                               { $$ = $1; }
    | '-' unary                             { $$ = new_unary('-', $2); }
    | '!' unary                             { $$ = new_unary('!', $2); } /* added */
    ;

primary     : ...   /* same as ch03 */

%%
```

The full version is in `steps/ch04/src/parser.y`. `%expect 1` declares "exactly one shift/reduce expected" — for the dangling-else.

## 3. ast.h — new node kinds and widened op

```c
typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_IDENT,
    NODE_ASSIGN,
    NODE_VAR_DECL,
    NODE_RETURN,
    NODE_EXPR_STMT,
    NODE_IF,           /* added */
    NODE_WHILE,        /* added */
    NODE_FUNC_DEF,
    NODE_BLOCK,
} NodeKind;

/* Operator codes — single-char operators use ASCII, multi-char use 256+ */
enum {
    OP_LE = 256,    /* <= */
    OP_GE,          /* >= */
    OP_EQ,          /* == */
    OP_NE,          /* != */
};

struct Node {
    NodeKind kind;
    int int_val;
    int op;            /* changed: char → int */
    Node *lhs, *rhs;
    Node *operand;
    char *name;
    Node *body;
    Node *expr;
    Node *cond;        /* added: NODE_IF, NODE_WHILE */
    Node *then_body;   /* added: NODE_IF */
    Node *else_body;   /* added: NODE_IF */
    NodeList *stmts;
};

Node *new_int_lit(int val);
Node *new_binary(int op, Node *lhs, Node *rhs);     /* changed: int op */
Node *new_unary(int op, Node *operand);             /* changed: int op */
Node *new_ident(char *name);
Node *new_assign(Node *lhs, Node *rhs);
Node *new_var_decl(char *name, Node *init);
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);
Node *new_if(Node *cond, Node *then_body, Node *else_body); /* added */
Node *new_while(Node *cond, Node *body);                    /* added */
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);
NodeList *new_node_list(Node *node, NodeList *next);
void print_ast(Node *node, int indent);
```

## 4. ast.c — constructors + print_ast extended

Add `new_if` and `new_while`. Add `NODE_IF`/`NODE_WHILE` to `print_ast`. To stringify multi-character operators, introduce a small helper `op_str`.

```c
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
```

The `BINARY` and `UNARY` cases in `print_ast` use `op_str(node->op)` for stringification.

```c
case NODE_BINARY:
    printf("BINARY %s\n", op_str(node->op));                              /* changed: %c → %s */
    print_ast(node->lhs, level + 1);
    print_ast(node->rhs, level + 1);
    break;
case NODE_UNARY:
    printf("UNARY %s\n", op_str(node->op));                               /* changed: %c → %s */
    print_ast(node->operand, level + 1);
    break;
case NODE_IF:                                                             /* added */
    printf("IF\n");                                                       /* added */
    print_ast(node->cond, level + 1);                                     /* added */
    print_ast(node->then_body, level + 1);                                /* added */
    if (node->else_body) print_ast(node->else_body, level + 1);           /* added */
    break;                                                                /* added */
case NODE_WHILE:                                                          /* added */
    printf("WHILE\n");                                                    /* added */
    print_ast(node->cond, level + 1);                                     /* added */
    print_ast(node->body, level + 1);                                     /* added */
    break;                                                                /* added */
```

The full version is at `steps/ch04/src/ast.c`.

## 5. codegen.c — comparison, negation, if, while

Add `'!'` and six comparison cases to `gen_expr`. Add `NODE_IF` and `NODE_WHILE` to `gen_stmt`. Add a static `label_count`, a `new_label` issuer, and an `emit_compare` helper for the three-instruction comparison.

```c
static int label_count;
static int new_label(void) { return label_count++; }

static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}
```

In the `codegen` function, add `label_count = 0;` reset.

```c
void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;
    frame_size = 0;
    label_count = 0;             /* added */
    /* ... same as ch03 from here ... */
}
```

`gen_expr` additions:

```c
case NODE_UNARY:
    gen_expr(node->operand);
    switch (node->op) {
    case '-':
        fprintf(out, "  negl %%eax\n");
        return;
    case '!':                                       /* added */
        fprintf(out, "  cmpl $0, %%eax\n");         /* added */
        fprintf(out, "  sete %%al\n");              /* added */
        fprintf(out, "  movzbl %%al, %%eax\n");     /* added */
        return;                                     /* added */
    }
    ...

case NODE_BINARY:
    gen_expr(node->rhs);
    fprintf(out, "  pushq %%rax\n");
    gen_expr(node->lhs);
    fprintf(out, "  popq %%rcx\n");
    switch (node->op) {
    /* +-*/% (same as ch02) */
    case '<':   emit_compare("setl");  return;     /* added */
    case OP_LE: emit_compare("setle"); return;     /* added */
    case '>':   emit_compare("setg");  return;     /* added */
    case OP_GE: emit_compare("setge"); return;     /* added */
    case OP_EQ: emit_compare("sete");  return;     /* added */
    case OP_NE: emit_compare("setne"); return;     /* added */
    }
    ...
```

`gen_stmt` additions:

```c
case NODE_IF: {
    int n = new_label();
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    if (node->else_body) {
        fprintf(out, "  je .Lelse_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, "  jmp .Lendif_%d\n", n);
        fprintf(out, ".Lelse_%d:\n", n);
        gen_stmt(node->else_body);
        fprintf(out, ".Lendif_%d:\n", n);
    } else {
        fprintf(out, "  je .Lendif_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, ".Lendif_%d:\n", n);
    }
    return;
}
case NODE_WHILE: {
    int n = new_label();
    fprintf(out, ".Lbegin_%d:\n", n);
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    fprintf(out, "  je .Lendwhile_%d\n", n);
    gen_stmt(node->body);
    fprintf(out, "  jmp .Lbegin_%d\n", n);
    fprintf(out, ".Lendwhile_%d:\n", n);
    return;
}
```

Full file at `steps/ch04/src/codegen.c`.

## 6. main.c / Makefile — no changes

## 7. Build & run

```bash
$ cd steps/ch04
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

Thanks to `%expect 1`, no warning about the dangling-else conflict.

Factorial:

```bash
$ cat > test.c <<'EOF'
int main() {
    int n = 5;
    int result = 1;
    while (n > 1) {
        result = result * n;
        n = n - 1;
    }
    return result;
}
EOF

$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
120
```

if-else:

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 10;
    if (x == 10) {
        return 100;
    } else {
        return 0;
    }
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
100
```

`!` and a compound condition:

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 3;
    if (!(x < 5)) {
        return 1;
    }
    return 2;
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
2
```

`x = 3 < 5` is true; `!(true)` is false. So we don't enter the then-body, and `return 2` returns 2.

Dangling-else:

```bash
$ cat > test.c <<'EOF'
int main() {
    int x = 5;
    int y = 0;
    if (x > 0)
        if (x > 10)
            y = 1;
        else
            y = 2;
    return y;
}
EOF
$ ./tinyc --dump-ast test.c
```

Looking at the AST, you can see the `else` binds to **the inner `if (x > 10)`**:

```
IF
  BINARY >
    IDENT x
    INT_LIT 0
  IF                  ← another if in the then-part, with the else inside it
    BINARY >
      IDENT x
      INT_LIT 10
    EXPR_STMT (y = 1)  ← then
    EXPR_STMT (y = 2)  ← else (of the inner if)
```

With `x = 5`, the outer `if (x > 0)` is true → proceed inward. The inner `if (x > 10)` is false → else `y = 2`. So `return y` gives 2.

```bash
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
2
```

As expected.

## 8. Reading the factorial assembly

Input (repeated):

```c
int main() {
    int n = 5;
    int result = 1;
    while (n > 1) {
        result = result * n;
        n = n - 1;
    }
    return result;
}
```

Output:

```
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $8, %rsp
  movl $5, %eax
  movl %eax, -8(%rbp)            # n = 5
  subq $8, %rsp
  movl $1, %eax
  movl %eax, -16(%rbp)           # result = 1
.Lbegin_0:                       # ─── top of while ───
  movl $1, %eax                  # cond: evaluate n > 1
  pushq %rax
  movl -8(%rbp), %eax
  popq %rcx
  cmpl %ecx, %eax                # n - 1
  setg %al                       # n > 1
  movzbl %al, %eax
  cmpl $0, %eax                  # exit if result is 0
  je .Lendwhile_0
  movl -8(%rbp), %eax            # body: result = result * n
  pushq %rax
  movl -16(%rbp), %eax
  popq %rcx
  imull %ecx, %eax
  movl %eax, -16(%rbp)
  movl $1, %eax                  #       n = n - 1
  pushq %rax
  movl -8(%rbp), %eax
  popq %rcx
  subl %ecx, %eax
  movl %eax, -8(%rbp)
  jmp .Lbegin_0                  # back to the top
.Lendwhile_0:                    # ─── end of while ───
  movl -16(%rbp), %eax           # return result
  leave
  ret
```

From `.Lbegin_0:` to `.Lendwhile_0:` is one while. Evaluate condition → `je` to test for exit → body → `jmp` back to the top — the canonical shape.

Five passes of the loop leave `1 * 5 * 4 * 3 * 2 = 120` in `%eax`, which `return` hands back.

## 9. Next

Chapter 5 adds **function definitions and calls**. Multiple functions, parameters, calls to external functions. The new theme is the **System V AMD64 ABI** — the calling convention that passes arguments in registers and keeps the stack aligned to 16 bytes.
