# 04 — 完全形とビルド

ch04 の各ファイルの完全形を、ch03 との差分とともに並べる。最後に実際にビルドして階乗と分岐を動かす。

## 1. lexer.l — 新キーワードと演算子

ch03 のものに、`if`/`else`/`while` のキーワード3つ、複数文字演算子4つ、1文字記号3つを追加する。

```flex
%{
#include "ast.h"
#include "parser.tab.h"
%}

%option noyywrap

%%
"int"       { return INT; }
"return"    { return RETURN; }
"if"        { return IF; }                          /* 追加 */
"else"      { return ELSE; }                        /* 追加 */
"while"     { return WHILE; }                       /* 追加 */
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
"=="        { return EQ_OP; }                       /* 追加 */
"!="        { return NE_OP; }                       /* 追加 */
"<="        { return LE_OP; }                       /* 追加 */
">="        { return GE_OP; }                       /* 追加 */
"+"         { return '+'; }
"-"         { return '-'; }
"*"         { return '*'; }
"/"         { return '/'; }
"%"         { return '%'; }
"="         { return '='; }
"<"         { return '<'; }                         /* 追加 */
">"         { return '>'; }                         /* 追加 */
"!"         { return '!'; }                         /* 追加 */
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

複数文字トークン（`==` など）は1文字版（`=` など）と並べて書ける。flex の最長一致で `==` のほうが優先される。順序は意味を持たないが、整理上ペアで並べると見やすい。

## 2. parser.y — 階層と文の追加

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
%token INT RETURN IF ELSE WHILE                     /* 追加: IF ELSE WHILE */
%token EQ_OP NE_OP LE_OP GE_OP                      /* 追加 */

%type <node> func_def stmt expr assign equality relational add_expr mul_expr unary primary
%type <list> stmts

%expect 1                                            /* 追加: dangling-else */

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
    | '{' stmts '}'                         { $$ = new_block($2); }       /* 追加 */
    | IF '(' expr ')' stmt                  { $$ = new_if($3, $5, NULL); }/* 追加 */
    | IF '(' expr ')' stmt ELSE stmt        { $$ = new_if($3, $5, $7); }  /* 追加 */
    | WHILE '(' expr ')' stmt               { $$ = new_while($3, $5); }   /* 追加 */
    ;

expr        : assign            { $$ = $1; } ;

assign
    : equality                              { $$ = $1; }
    | equality '=' assign                   { $$ = new_assign($1, $3); }
    ;

equality                                                              /* 追加 */
    : relational                            { $$ = $1; }              /* 追加 */
    | equality EQ_OP relational             { $$ = new_binary(OP_EQ, $1, $3); } /* 追加 */
    | equality NE_OP relational             { $$ = new_binary(OP_NE, $1, $3); } /* 追加 */
    ;                                                                 /* 追加 */

relational                                                            /* 追加 */
    : add_expr                              { $$ = $1; }              /* 追加 */
    | relational '<' add_expr               { $$ = new_binary('<', $1, $3); }   /* 追加 */
    | relational LE_OP add_expr             { $$ = new_binary(OP_LE, $1, $3); } /* 追加 */
    | relational '>' add_expr               { $$ = new_binary('>', $1, $3); }   /* 追加 */
    | relational GE_OP add_expr             { $$ = new_binary(OP_GE, $1, $3); } /* 追加 */
    ;                                                                 /* 追加 */

add_expr    : ...   /* ch02 と同じ */
mul_expr    : ...
unary
    : primary                               { $$ = $1; }
    | '-' unary                             { $$ = new_unary('-', $2); }
    | '!' unary                             { $$ = new_unary('!', $2); } /* 追加 */
    ;

primary     : ...   /* ch03 と同じ */

%%
```

完全版は `steps/ch04/src/parser.y` を参照。`%expect 1` は dangling-else の shift/reduce を「想定通り 1 個」と宣言するもの。

## 3. ast.h — ノード型とop拡張

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
    NODE_IF,           /* 追加 */
    NODE_WHILE,        /* 追加 */
    NODE_FUNC_DEF,
    NODE_BLOCK,
} NodeKind;

/* 演算子コード — 1文字オペレータは ASCII、複数文字は 256 以降 */
enum {
    OP_LE = 256,    /* <= */
    OP_GE,          /* >= */
    OP_EQ,          /* == */
    OP_NE,          /* != */
};

struct Node {
    NodeKind kind;
    int int_val;
    int op;            /* 変更: char → int */
    Node *lhs, *rhs;
    Node *operand;
    char *name;
    Node *body;
    Node *expr;
    Node *cond;        /* 追加: NODE_IF, NODE_WHILE */
    Node *then_body;   /* 追加: NODE_IF */
    Node *else_body;   /* 追加: NODE_IF */
    NodeList *stmts;
};

Node *new_int_lit(int val);
Node *new_binary(int op, Node *lhs, Node *rhs);     /* 変更: int op */
Node *new_unary(int op, Node *operand);             /* 変更: int op */
Node *new_ident(char *name);
Node *new_assign(Node *lhs, Node *rhs);
Node *new_var_decl(char *name, Node *init);
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);
Node *new_if(Node *cond, Node *then_body, Node *else_body); /* 追加 */
Node *new_while(Node *cond, Node *body);                    /* 追加 */
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);
NodeList *new_node_list(Node *node, NodeList *next);
void print_ast(Node *node, int indent);
```

## 4. ast.c — コンストラクタ + print_ast 拡張

`new_if` と `new_while` を追加。`print_ast` に `NODE_IF` `NODE_WHILE` を追加。複数文字演算子の文字列化のために小さなヘルパー `op_str` を入れる。

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

`print_ast` の `BINARY` `UNARY` 部分は `op_str(node->op)` で文字列化。

```c
case NODE_BINARY:
    printf("BINARY %s\n", op_str(node->op));                              /* 変更: %c → %s */
    print_ast(node->lhs, level + 1);
    print_ast(node->rhs, level + 1);
    break;
case NODE_UNARY:
    printf("UNARY %s\n", op_str(node->op));                               /* 変更: %c → %s */
    print_ast(node->operand, level + 1);
    break;
case NODE_IF:                                                             /* 追加 */
    printf("IF\n");                                                       /* 追加 */
    print_ast(node->cond, level + 1);                                     /* 追加 */
    print_ast(node->then_body, level + 1);                                /* 追加 */
    if (node->else_body) print_ast(node->else_body, level + 1);           /* 追加 */
    break;                                                                /* 追加 */
case NODE_WHILE:                                                          /* 追加 */
    printf("WHILE\n");                                                    /* 追加 */
    print_ast(node->cond, level + 1);                                     /* 追加 */
    print_ast(node->body, level + 1);                                     /* 追加 */
    break;                                                                /* 追加 */
```

完全版は `steps/ch04/src/ast.c`。

## 5. codegen.c — 比較・否定・if・while

`gen_expr` に `'!'`、6個の比較演算ケースを追加。`gen_stmt` に `NODE_IF`、`NODE_WHILE` を追加。`label_count` という静的変数とラベル発行関数 `new_label`、比較3命令を出すヘルパー `emit_compare`。

```c
static int label_count;
static int new_label(void) { return label_count++; }

static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}
```

`codegen` 関数本体に `label_count = 0;` のリセットを追加。

```c
void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;
    frame_size = 0;
    label_count = 0;             /* 追加 */
    /* ... 以下 ch03 と同じ ... */
}
```

`gen_expr` の追加分:

```c
case NODE_UNARY:
    gen_expr(node->operand);
    switch (node->op) {
    case '-':
        fprintf(out, "  negl %%eax\n");
        return;
    case '!':                                       /* 追加 */
        fprintf(out, "  cmpl $0, %%eax\n");         /* 追加 */
        fprintf(out, "  sete %%al\n");              /* 追加 */
        fprintf(out, "  movzbl %%al, %%eax\n");     /* 追加 */
        return;                                     /* 追加 */
    }
    ...

case NODE_BINARY:
    gen_expr(node->rhs);
    fprintf(out, "  pushq %%rax\n");
    gen_expr(node->lhs);
    fprintf(out, "  popq %%rcx\n");
    switch (node->op) {
    /* +-*/% (ch02 と同じ) */
    case '<':   emit_compare("setl");  return;     /* 追加 */
    case OP_LE: emit_compare("setle"); return;     /* 追加 */
    case '>':   emit_compare("setg");  return;     /* 追加 */
    case OP_GE: emit_compare("setge"); return;     /* 追加 */
    case OP_EQ: emit_compare("sete");  return;     /* 追加 */
    case OP_NE: emit_compare("setne"); return;     /* 追加 */
    }
    ...
```

`gen_stmt` の追加分:

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

完全版は `steps/ch04/src/codegen.c`。

## 6. main.c / Makefile — 変更なし

## 7. ビルドして動かす

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

`%expect 1` のおかげで dangling-else の競合警告は出ない。

階乗:

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

`!` と複合条件:

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

`x = 3 < 5` は真。`!(真)` は偽。だから then には入らず、`return 2` で 2 が返る。

dangling-else:

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

AST を見ると、`else` が **内側の `if (x > 10)`** に bind しているのが分かる:

```
IF
  BINARY >
    IDENT x
    INT_LIT 0
  IF                  ← then 部にもう一つ if、その中に else が入る
    BINARY >
      IDENT x
      INT_LIT 10
    EXPR_STMT (y = 1)  ← then
    EXPR_STMT (y = 2)  ← else (内側の if の else)
```

`x = 5` のとき、外側の `if (x > 0)` は真 → 内側に進む。内側の `if (x > 10)` は偽 → else 部 `y = 2`。だから `return y` で 2。

```bash
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
2
```

期待通り。

## 8. 階乗の生成アセンブリを読む

入力（再掲）:

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

生成アセンブリ:

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
.Lbegin_0:                       # ─── while 先頭 ───
  movl $1, %eax                  # cond: n > 1 を評価
  pushq %rax
  movl -8(%rbp), %eax
  popq %rcx
  cmpl %ecx, %eax                # n - 1
  setg %al                       # n > 1
  movzbl %al, %eax
  cmpl $0, %eax                  # 結果が 0 なら脱出
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
  jmp .Lbegin_0                  # 先頭へ戻る
.Lendwhile_0:                    # ─── while 終端 ───
  movl -16(%rbp), %eax           # return result
  leave
  ret
```

`.Lbegin_0:` から `.Lendwhile_0:` までが1つの while。条件評価 → `je` で脱出判定 → 本体 → `jmp` で先頭へ戻る、の典型形。

5回ループして `1 * 5 * 4 * 3 * 2 = 120` が `%eax` に残り、`return` で返される。

## 9. 次へ

第5章では **関数の定義と呼び出し** を加える。複数の関数、引数、外部関数呼び出し。新しいテーマは **System V AMD64 ABI** ── レジスタで引数を渡し、16バイトでスタックを揃える呼び出し規約だ。
