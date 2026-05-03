# 03 — 完全形とビルド

ch02 のソース全体を ch01 との差分とともに並べる。ビルドして動かし、生成アセンブリを追う。

## 1. lexer.l — トークン5つ追加

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

ch01 との差分は5行だけ。`+ - * / %` のトークンを追加。1文字記号なので bison 流に文字そのものをトークン番号とする。

## 2. parser.y — 式の階層を導入

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
    : add_expr          { $$ = $1; }                            /* 変更: ch01 は : INT_LIT */
    ;

add_expr                                                        /* 追加 */
    : mul_expr                  { $$ = $1; }                    /* 追加 */
    | add_expr '+' mul_expr     { $$ = new_binary('+', $1, $3); }/* 追加 */
    | add_expr '-' mul_expr     { $$ = new_binary('-', $1, $3); }/* 追加 */
    ;                                                           /* 追加 */

mul_expr                                                        /* 追加 */
    : unary                     { $$ = $1; }                    /* 追加 */
    | mul_expr '*' unary        { $$ = new_binary('*', $1, $3); }/* 追加 */
    | mul_expr '/' unary        { $$ = new_binary('/', $1, $3); }/* 追加 */
    | mul_expr '%' unary        { $$ = new_binary('%', $1, $3); }/* 追加 */
    ;                                                           /* 追加 */

unary                                                           /* 追加 */
    : primary                   { $$ = $1; }                    /* 追加 */
    | '-' unary                 { $$ = new_unary('-', $2); }    /* 追加 */
    ;                                                           /* 追加 */

primary                                                         /* 追加 */
    : INT_LIT                   { $$ = new_int_lit($1); }       /* 追加 */
    | '(' expr ')'              { $$ = $2; }                    /* 追加 */
    ;                                                           /* 追加 */

%%
```

ch01 では `expr : INT_LIT` の1行だけだった。ch02 ではここを5階層に拡張。`%type` 宣言にも `add_expr mul_expr unary primary` を加える。

## 3. ast.h — ノード型2つ追加

```c
#ifndef AST_H
#define AST_H

typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,     /* 追加: lhs op rhs */
    NODE_UNARY,      /* 追加: op operand */
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
    char op;           /* NODE_BINARY, NODE_UNARY: 追加 */
    Node *lhs;         /* NODE_BINARY: 追加 */
    Node *rhs;         /* NODE_BINARY: 追加 */
    Node *operand;     /* NODE_UNARY:  追加 */
    char *name;        /* NODE_FUNC_DEF */
    Node *body;        /* NODE_FUNC_DEF */
    Node *expr;        /* NODE_RETURN */
    NodeList *stmts;   /* NODE_BLOCK */
};

Node *new_int_lit(int val);
Node *new_binary(char op, Node *lhs, Node *rhs);   /* 追加 */
Node *new_unary(char op, Node *operand);           /* 追加 */
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);

#endif
```

`NodeKind` に2つ、フィールドに4つ、コンストラクタ宣言に2つ。`Node` 構造体は素朴に「全種類のフィールドを並べたもの」なので、新しいノード種別を増やすだけ。

## 4. ast.c — コンストラクタ2つ + print_ast 拡張

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

Node *new_binary(char op, Node *lhs, Node *rhs) {                /* 追加 */
    Node *n = calloc(1, sizeof(Node));                           /* 追加 */
    n->kind = NODE_BINARY;                                       /* 追加 */
    n->op = op;                                                  /* 追加 */
    n->lhs = lhs;                                                /* 追加 */
    n->rhs = rhs;                                                /* 追加 */
    return n;                                                    /* 追加 */
}                                                                /* 追加 */

Node *new_unary(char op, Node *operand) {                        /* 追加 */
    Node *n = calloc(1, sizeof(Node));                           /* 追加 */
    n->kind = NODE_UNARY;                                        /* 追加 */
    n->op = op;                                                  /* 追加 */
    n->operand = operand;                                        /* 追加 */
    return n;                                                    /* 追加 */
}                                                                /* 追加 */

Node *new_return(Node *expr) { /* ch01 と同じ */ }
Node *new_block(NodeList *stmts) { /* ch01 と同じ */ }
Node *new_func_def(char *name, Node *body) { /* ch01 と同じ */ }
NodeList *new_node_list(Node *node, NodeList *next) { /* ch01 と同じ */ }

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
    case NODE_BINARY:                                            /* 追加 */
        printf("BINARY %c\n", node->op);                         /* 追加 */
        print_ast(node->lhs, level + 1);                         /* 追加 */
        print_ast(node->rhs, level + 1);                         /* 追加 */
        break;                                                   /* 追加 */
    case NODE_UNARY:                                             /* 追加 */
        printf("UNARY %c\n", node->op);                          /* 追加 */
        print_ast(node->operand, level + 1);                     /* 追加 */
        break;                                                   /* 追加 */
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

実際のソースは省略していない（`steps/ch02/src/ast.c` を参照）。ここでは差分が見えるよう、ch01 と同じ部分は省いた。

## 5. codegen.c — gen_expr に2ケース追加

`gen_stmt` と `codegen` 関数は ch01 と同じ。`gen_expr` だけが太る。

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
    case NODE_UNARY:                                                  /* 追加 */
        gen_expr(node->operand);                                      /* 追加 */
        fprintf(out, "  negl %%eax\n");                               /* 追加 */
        return;                                                       /* 追加 */
    case NODE_BINARY:                                                 /* 追加 */
        gen_expr(node->rhs);                                          /* 追加 */
        fprintf(out, "  pushq %%rax\n");                              /* 追加 */
        gen_expr(node->lhs);                                          /* 追加 */
        fprintf(out, "  popq %%rcx\n");                               /* 追加 */
        switch (node->op) {                                           /* 追加 */
        case '+': fprintf(out, "  addl %%ecx, %%eax\n"); return;      /* 追加 */
        case '-': fprintf(out, "  subl %%ecx, %%eax\n"); return;      /* 追加 */
        case '*': fprintf(out, "  imull %%ecx, %%eax\n"); return;     /* 追加 */
        case '/':                                                     /* 追加 */
            fprintf(out, "  cdq\n");                                  /* 追加 */
            fprintf(out, "  idivl %%ecx\n");                          /* 追加 */
            return;                                                   /* 追加 */
        case '%':                                                     /* 追加 */
            fprintf(out, "  cdq\n");                                  /* 追加 */
            fprintf(out, "  idivl %%ecx\n");                          /* 追加 */
            fprintf(out, "  movl %%edx, %%eax\n");                    /* 追加 */
            return;                                                   /* 追加 */
        }
        fprintf(stderr, "unknown binary op: %c\n", node->op);         /* 追加 */
        exit(1);                                                      /* 追加 */
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

static void gen_stmt(Node *node) { /* ch01 と同じ */ }
void codegen(Node *prog, FILE *output) { /* ch01 と同じ */ }
```

## 6. main.c / Makefile / codegen.h — 変更なし

ch01 のものをそのまま使う。

## 7. ビルドして動かす

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

無事ビルド完了。

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

動いた。`return 2 + 3 * 4;` が確かに `14` を返す。

ほかにも試せる。

```bash
$ echo 'int main() { return (10 - 3) * 2 + 100 / 4 % 7; }' > test.c
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
18
# 計算順:
#   (10-3)        = 7
#   7 * 2         = 14
#   100 / 4       = 25
#   25 % 7        = 4    (% は左結合なので (100/4)%7)
#   14 + 4        = 18
```

`--dump-ast` で確かめると、`+` の左に `*` の塊が、右に `%` の塊が、それぞれ独立して座っている。文法の階層がそのまま AST の形に出る。

## 8. 生成アセンブリを追う

`return 2 + 3 * 4;` の生成アセンブリを上から読む。

```
  .text
  .globl main
main:
  pushq %rbp           # プロローグ
  movq %rsp, %rbp
  movl $4, %eax        # ── ここから式の評価 ──
  pushq %rax           # スタックに 4 を退避 (BINARY * の rhs)
  movl $3, %eax        # eax = 3 (BINARY * の lhs)
  popq %rcx            # ecx = 4 (退避していた rhs)
  imull %ecx, %eax     # eax = 3 * 4 = 12   ← BINARY * の結果
  pushq %rax           # スタックに 12 を退避 (BINARY + の rhs)
  movl $2, %eax        # eax = 2 (BINARY + の lhs)
  popq %rcx            # ecx = 12 (退避していた rhs)
  addl %ecx, %eax      # eax = 2 + 12 = 14  ← BINARY + の結果
  leave                # エピローグ
  ret                  # eax の 14 が main の戻り値
```

スタックの動きはこう変わる（左が古い、右が新しい）。

```
時刻 0: []
時刻 1: pushq %rax (=4)         → [4]
時刻 2: popq  %rcx               → []         (rcx=4, 内部計算で使う)
時刻 3: pushq %rax (=12)         → [12]
時刻 4: popq  %rcx               → []         (rcx=12)
```

各ペアが完璧に釣り合っている。`leave` する時点でスタックは `pushq %rbp` 直後の状態に戻っている。

AST と生成アセンブリの対応を見てもいい。

```
BINARY +                       <-- 一番最後の addl に対応
├─ INT_LIT 2                   <-- "movl $2, %eax"   (2回目)
└─ BINARY *                    <-- 内側の imull
   ├─ INT_LIT 3                <-- "movl $3, %eax"
   └─ INT_LIT 4                <-- "movl $4, %eax"   (1回目)
```

葉 (`INT_LIT`) は `movl $N, %eax`、内部ノード (`BINARY`) はそのオペレーション命令。木の **後行順** (postorder) で命令が並ぶ ── これが再帰的な式の codegen の典型形だ。

## 9. 次へ

第3章では **変数** が加わる。`int x = 1; int y = 2; return x + y;` のような式が書けるようになる。スタックフレーム上に変数の場所を取り、名前をアドレスに変換する仕組みが入る。
