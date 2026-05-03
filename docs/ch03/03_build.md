# 03 — 完全形とビルド

ch02 との差分を意識しつつ、ch03 の各ファイルの完全形を並べる。最後に実際にビルドして動かす。

## 1. lexer.l — `=` を1行追加

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
"="         { return '='; }                       /* 追加 */
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

## 2. parser.y — 文・代入・識別子

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
    | INT IDENT '=' expr ';'       { $$ = new_var_decl($2, $4); }   /* 追加 */
    | expr ';'                     { $$ = new_expr_stmt($1); }      /* 追加 */
    ;

expr
    : assign                       { $$ = $1; }                /* 階層追加 */
    ;

assign                                                         /* 追加 */
    : add_expr                     { $$ = $1; }                /* 追加 */
    | add_expr '=' assign          { $$ = new_assign($1, $3); }/* 追加 */
    ;                                                          /* 追加 */

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
    | IDENT                        { $$ = new_ident($1); }    /* 追加 */
    | '(' expr ')'                 { $$ = $2; }
    ;

%%
```

## 3. ast.h — ノード型4つ追加

```c
#ifndef AST_H
#define AST_H

typedef enum {
    NODE_INT_LIT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_IDENT,        /* 追加 */
    NODE_ASSIGN,       /* 追加 */
    NODE_VAR_DECL,     /* 追加 */
    NODE_RETURN,
    NODE_EXPR_STMT,    /* 追加 */
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
Node *new_ident(char *name);                /* 追加 */
Node *new_assign(Node *lhs, Node *rhs);     /* 追加 */
Node *new_var_decl(char *name, Node *init); /* 追加 */
Node *new_return(Node *expr);
Node *new_expr_stmt(Node *expr);            /* 追加 */
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);

#endif
```

## 4. ast.c — コンストラクタ4つ + print_ast 拡張

新規コンストラクタは ch02 までと同じ素朴なパターン。`calloc(1, sizeof(Node))` してフィールドを埋めるだけ。

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

`print_ast` の switch にもこの4ケースを足す。形式は他と同じ ── 名前を出して、子を再帰的に出す。

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

完全版は `steps/ch03/src/ast.c` で。

## 5. codegen.c — シンボルテーブルと新ノード4つ

ch02 にシンボルテーブル（`LVar`, `add_local`, `find_local`）を追加し、`gen_expr` に `NODE_IDENT` と `NODE_ASSIGN` を、`gen_stmt` に `NODE_VAR_DECL` と `NODE_EXPR_STMT` を追加。

ファイル冒頭の追加分。

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

`codegen` 関数本体は、変数を累積するためにテーブルをリセットする。

```c
void codegen(Node *prog, FILE *output) {
    out = output;
    locals = NULL;          /* 追加: テーブルをリセット */
    frame_size = 0;         /* 追加 */

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

`gen_expr` の追加分。

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

`gen_stmt` の追加分。

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

完全版は `steps/ch03/src/codegen.c` で。

## 6. main.c / Makefile / codegen.h — 変更なし

ch02 と同じ。

## 7. ビルドして動かす

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

bison は競合警告を出さない。代入の文法は LALR(1) でクリーンに解ける。

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

`x + y = 1 + 2 = 3`。期待通り。

代入も試す。

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

連鎖代入も。

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

未宣言の変数を使うとエラー。

```bash
$ echo 'int main() { return z; }' | ./tinyc /dev/stdin > /dev/null
undeclared variable: z
```

宣言の重複もエラー。

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

## 8. 生成アセンブリを追う

入力:

```c
int main() {
    int x = 1;
    int y = 2;
    return x + y;
}
```

出力アセンブリ:

```
  .text
  .globl main
main:
  pushq %rbp           # ─── プロローグ ───
  movq %rsp, %rbp
  subq $8, %rsp        # ─── int x = 1; ───
  movl $1, %eax
  movl %eax, -8(%rbp)  #   x のスロット (-8) に 1 を保存
  subq $8, %rsp        # ─── int y = 2; ───
  movl $2, %eax
  movl %eax, -16(%rbp) #   y のスロット (-16) に 2 を保存
  movl -16(%rbp), %eax # ─── return x + y; ───
                       #   先に rhs (= y) を読む
  pushq %rax           #   y の値を退避
  movl -8(%rbp), %eax  #   x を読む
  popq %rcx            #   ecx = y, eax = x
  addl %ecx, %eax      #   eax = x + y = 3
  leave                # ─── エピローグ ───
  ret
```

スタックの動きを時系列で。

```
時刻 0 (プロローグ後):                  %rsp ≡ %rbp         スタック: [旧rbp]
時刻 1 (subq $8, %rsp 後):              %rsp ≡ %rbp - 8     スタック: [旧rbp][?]
時刻 2 (movl %eax, -8(%rbp) 後):        %rsp ≡ %rbp - 8     スタック: [旧rbp][1]
時刻 3 (subq $8, %rsp 後):              %rsp ≡ %rbp - 16    スタック: [旧rbp][1][?]
時刻 4 (movl %eax, -16(%rbp) 後):       %rsp ≡ %rbp - 16    スタック: [旧rbp][1][2]
時刻 5 (pushq %rax で y を退避 後):     %rsp ≡ %rbp - 24    スタック: [旧rbp][1][2][2]
時刻 6 (popq %rcx 後):                  %rsp ≡ %rbp - 16    スタック: [旧rbp][1][2]
時刻 7 (leave 後):                      %rsp は元の値に復帰  スタック: 復元
```

（`≡` は「同じ位置を指す」の意。代入ではない。）

注目すべきは時刻 5。式の評価で `pushq %rax` が動くと、`%rsp` は `%rbp - 24` まで下がる。**でも `-8(%rbp)` と `-16(%rbp)` は `%rbp` 基準のアドレスなので、変わらず x と y を指している**。`%rsp` が動こうが動くまいが、変数アクセスは安定だ。

これが `%rbp` を別建てに持つ意味。プロローグで `movq %rsp, %rbp` をした瞬間、関数の中の世界では `%rbp` が「不動の基準点」になる。`%rsp` はその下を自由に動き回れる。

## 9. AST と生成アセンブリの対応

```
FUNC_DEF main                       <-- main: ラベル + プロローグ + エピローグ
  BLOCK                              <-- 中の各文を順に生成
    VAR_DECL x                       <-- subq $8, %rsp + movl ...
      INT_LIT 1
    VAR_DECL y                       <-- subq $8, %rsp + movl ...
      INT_LIT 2
    RETURN                           <-- gen_expr + leave + ret
      BINARY +                       <-- スタック方式の二項演算
        IDENT x                      <-- movl -8(%rbp), %eax
        IDENT y                      <-- movl -16(%rbp), %eax
```

ch02 のときと比べて、葉が `INT_LIT` ではなく `IDENT` になることがある、という違いだけ。`gen_expr` で見れば、`INT_LIT` は `movl $定数, %eax`、`IDENT` は `movl 番地, %eax`。同じ「`%eax` に値を入れる」という約束を、両方が守っている。

## 10. 次へ

第4章では **分岐 (if/else)** と **ループ (while)**、比較演算子 (`< <= > >= == !=`)、論理否定 `!` を加える。codegen の新しい道具は **ラベル** と **条件ジャンプ**。
