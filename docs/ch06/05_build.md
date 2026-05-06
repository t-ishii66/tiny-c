# 04 — 完全形とビルド

ch06 の主要差分を ch05 との対比で並べる。最後に Hello, world と strlen を動かす。

## 1. lexer.l — 新キーワード・新トークン

ch05 のものに以下を追加:

```flex
"char"      { return CHAR; }                                  /* 追加 */
"void"      { return VOID; }                                  /* 追加 */
"&"         { return '&'; }                                   /* 追加 */
"["         { return '['; }                                   /* 追加 */
"]"         { return ']'; }                                   /* 追加 */

'(\\.|[^\\'])'      { /* 追加: char literal */ ... return CHAR_LIT; }
\"([^"\\]|\\.)*\"   { /* 追加: string literal */ ... return STRING_LIT; }
```

`unescape` ヘルパーで `\n`、`\t`、`\\`、`\'`、`\"`、`\0` を処理する。完全版は `steps/ch06/src/lexer.l`。

## 2. parser.y — 型・ポインタ・配列・グローバル

主要な追加・変更:

```yacc
%token <int_val> INT_LIT CHAR_LIT
%token <name> IDENT
%token <str> STRING_LIT                                     /* 追加 */
%token INT CHAR VOID RETURN IF ELSE WHILE                   /* CHAR VOID 追加 */
%token EQ_OP NE_OP LE_OP GE_OP

%type <type> type                                           /* 追加 */

program
    : top_levels                                            { program = new_program($1); }
    ;

top_levels                                                  /* 追加 (func_defs を改名し拡張) */
    : /* empty */
    | top_level top_levels                                  { $$ = new_node_list($1, $2); }
    ;

top_level                                                   /* 追加 */
    : func_def
    | type IDENT ';'                                        { $$ = new_global_var_decl($2, $1); }
    | type IDENT '[' INT_LIT ']' ';'                        { $$ = new_global_var_decl($2, type_array($1->base_size, $4)); }
    ;

type                                                        /* 追加 */
    : INT                                                   { $$ = type_int(); }
    | CHAR                                                  { $$ = type_char(); }
    | VOID                                                  { ... }
    | INT '*'                                               { $$ = type_ptr(4); }
    | CHAR '*'                                              { $$ = type_ptr(1); }
    ;

func_def
    : type IDENT '(' params ')' '{' stmts '}'               /* 変更: INT → type */
                                                            { $$ = new_func_def($2, $4, new_block($7)); }
    ;

param
    : type IDENT                                            /* 変更: INT → type */
    ;

stmt
    : ...
    | type IDENT '=' expr ';'                               /* 変更: INT → type */
    | type IDENT '[' INT_LIT ']' ';'                        /* 追加 */
    | ...
    ;

unary
    : ...
    | '&' unary                                             /* 追加 */
    | '*' unary                                             /* 追加 */
    ;

primary
    : INT_LIT                                               { ... }
    | CHAR_LIT                                              { ... }                  /* 追加 */
    | STRING_LIT                                            { ... }                  /* 追加 */
    | IDENT
    | IDENT '(' args ')'
    | IDENT '[' expr ']'                                    { ... }                  /* 追加 */
    | '(' expr ')'
    ;
```

## 3. ast.h — Type 構造体と新ノード

```c
typedef enum {
    /* ch05 と同じ + */
    NODE_CHAR_LIT,        /* 追加 */
    NODE_STRING_LIT,      /* 追加 */
    NODE_GLOBAL_VAR_DECL, /* 追加 */
    NODE_INDEX,           /* 追加 */
    /* ... */
} NodeKind;

typedef struct Type Type;          /* 追加 */
struct Type {                      /* 追加 */
    int is_pointer;                /* 追加 */
    int is_array;                  /* 追加 */
    int base_size;                 /* 追加 */
    int array_size;                /* 追加 */
};                                 /* 追加 */

struct Node {
    /* ... 既存 ... */
    Type *type;                    /* 追加: VAR_DECL, GLOBAL_VAR_DECL, IDENT(param) */
    char *str_val;                 /* 追加: NODE_STRING_LIT */
    int str_len;                   /* 追加 */
};

/* Type コンストラクタ追加 */
Type *type_int(void);
Type *type_char(void);
Type *type_ptr(int base_size);
Type *type_array(int base_size, int n);
int type_size(Type *t);
int elem_size(Type *t);

/* Node コンストラクタ追加 */
Node *new_char_lit(int val);
Node *new_string_lit(char *str, int len);
Node *new_global_var_decl(char *name, Type *type);
Node *new_index(Node *base, Node *idx);
/* var_decl のシグネチャ変更: type 引数追加 */
Node *new_var_decl(char *name, Type *type, Node *init);
```

## 4. codegen.c — 大改修

新登場:

- **`gen_addr`**: lvalue（IDENT、`*p`、`a[i]`）のアドレスを `%rax` に。
- **`expr_type`**: 式の型を推論（変数→シンボルテーブル、`&`→ポインタ昇格、`*`→ポインタ降格）。
- **`emit_load(sz)` / `emit_store(sz)`**: 1/4/8 バイトの読み書きをサイズ別に。
- **`globals` テーブル**: グローバル変数のシンボルテーブル。
- **`strs` テーブル**: 文字列リテラル。`.rodata` に `.LSn:` ラベルで配置。

`gen_expr` の主要ケース:

```c
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->type->is_array) {
        gen_addr(node);              /* 配列は decay */
        return;
    }
    gen_addr(node);
    emit_load(type_size(v->type));   /* スカラー/ポインタ load */
    return;
}
case NODE_UNARY:
    if (node->op == '&') { gen_addr(node->operand); return; }
    if (node->op == '*') {
        gen_expr(node->operand);
        emit_load(expr_type(node->operand)->base_size);
        return;
    }
    /* - ! は ch04 と同じ */
case NODE_INDEX:
    gen_addr(node);
    emit_load(expr_type(node->lhs)->base_size);
    return;
case NODE_ASSIGN:
    gen_addr(node->lhs);
    emit_push();
    gen_expr(node->rhs);
    emit_pop("%rcx");
    emit_store(...);
    return;
case NODE_STRING_LIT:
    int n = add_string(node->str_val, node->str_len);
    fprintf(out, "  leaq .LS%d(%%rip), %%rax\n", n);
    return;
```

`codegen()` 本体:

```c
void codegen(Node *prog, FILE *output) {
    /* Phase 1: グローバルを先に登録 */
    for (NodeList *l = prog->stmts; l; l = l->next)
        if (l->node->kind == NODE_GLOBAL_VAR_DECL)
            add_global(l->node->name, l->node->type);

    /* .text — 関数 */
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)
        if (l->node->kind == NODE_FUNC_DEF)
            gen_func(l->node);

    /* .bss — グローバル変数 */
    int has_globals = 0;
    for (NodeList *l = prog->stmts; l; l = l->next) {
        Node *n = l->node;
        if (n->kind != NODE_GLOBAL_VAR_DECL) continue;
        if (!has_globals) { fprintf(out, "  .bss\n"); has_globals = 1; }
        fprintf(out, "  .globl %s\n", n->name);
        fprintf(out, "%s:\n", n->name);
        fprintf(out, "  .zero %d\n", type_size(n->type));
    }

    /* .rodata — 文字列リテラル */
    if (strs) {
        fprintf(out, "  .section .rodata\n");
        for (StrLit *s = strs; s; s = s->next)
            emit_string_literal(s);
    }
}
```

完全版は `steps/ch06/src/codegen.c`。

## 5. main.c / Makefile — 変更なし

5章連続で main.c と Makefile は変えていない。

## 6. ビルドして動かす

```bash
$ cd steps/ch06
$ make
$ cat hello.c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}

$ ./tinyc hello.c > hello.s
$ gcc -o hello hello.s
$ ./hello
hello
```

ch01 では `return 42;` だけだったコンパイラが、6 章を経て `printf` で文字列出力できるところまで来た。

### ポインタ操作

```bash
$ cat ptr.c
int main() {
    int x = 5;
    int *p = &x;
    *p = 10;
    return x;
}
$ ./tinyc ptr.c > ptr.s && gcc -o ptr ptr.s && ./ptr; echo $?
10
```

### 配列

```bash
$ cat arr.c
int main() {
    int a[5];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    return a[0] + a[1] + a[2];
}
$ ./tinyc arr.c > arr.s && gcc -o arr arr.s && ./arr; echo $?
60
```

### グローバル変数

```bash
$ cat g.c
int g;
int set(int v) { g = v; return 0; }
int main() {
    set(42);
    return g;
}
$ ./tinyc g.c > g.s && gcc -o g g.s && ./g; echo $?
42
```

### 文字列処理（strlen）

```bash
$ cat strlen.c
int strlen(char *s) {
    int n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

int main() {
    char *s = "hello";
    int len = strlen(s);
    printf("len=%d\n", len);
    return len;
}
$ ./tinyc strlen.c > strlen.s && gcc -o strlen strlen.s && ./strlen
len=5
$ echo $?
5
```

`s[n]` で 1 バイトずつ読み出し、ヌル終端まで数える ── これだけのコードでも、char* / 添字 / グローバル不要 / printf が同居する完全な C プログラムだ。

## 7. Hello, world の生成アセンブリを読む

`int main() { char *s = "hello"; printf("%s\n", s); return 0; }` を変換:

```
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp                # ローカル s の領域 (8 バイト + アライン)

  ; char *s = "hello";
  leaq -8(%rbp), %rax           # &s
  pushq %rax
  leaq .LS0(%rip), %rax         # "hello" のアドレス
  popq %rcx
  movq %rax, (%rcx)             # *(&s) = "hello" のアドレス → s に代入

  ; printf("%s\n", s);
  leaq -8(%rbp), %rax           # &s
  movq (%rax), %rax             # s の値 = "hello" のアドレス
  pushq %rax                    # 引数 2 を push
  leaq .LS1(%rip), %rax         # "%s\n" のアドレス
  pushq %rax                    # 引数 1 を push
  popq %rdi                     # %rdi = "%s\n"
  popq %rsi                     # %rsi = s
  movl $0, %eax                 # variadic ABI
  call printf

  ; return 0;
  movl $0, %eax
  leave
  ret
  movl $0, %eax
  leave
  ret

  .section .rodata
.LS1:
  .byte 37, 115, 10, 0          # "%s\n\0"
.LS0:
  .byte 104, 101, 108, 108, 111, 0  # "hello\0"
```

`.LS0` と `.LS1` が `.rodata` に置かれ、`leaq ...(%rip), %rax` でアドレスを取得、`%rdi`/`%rsi` に並べて `printf` を呼ぶ。System V ABI に従ってスタックは 16-aligned に保たれている（ローカル `s` の 16 バイト確保のおかげで `call printf` の直前は 16-aligned）。

## 8. tiny-c の旅、ここまでで作ったもの

第6章をもって tiny-c は当初定めた言語仕様をほぼカバーする:

- 整数型 (`int`、`char`)、ポインタ、1 次元配列、`void` 戻り値
- 算術・比較・論理否定・代入、優先順位の文法表現
- ローカル変数（初期化必須）、グローバル変数（ゼロ初期化）
- 制御構造（`if`/`else`、`while`）
- 関数定義、引数 6 個まで、再帰、相互再帰、`printf` 等の外部関数呼び出し
- 文字リテラル、文字列リテラル
- ポインタ操作（`&` `*` `[]`）

全部合わせても `steps/ch06/` のソースは数百行。**コンパイラの主要な仕組み** ── lexer / parser / AST / codegen / lvalue-rvalue ── を 6 章かけて自分の手で組み立ててきた。

## まとめ

- ポインタ・配列・グローバル変数・文字列リテラル ── スタックの外のデータも扱えるようになった。
- `gen_addr` 関数の導入で、lvalue / rvalue の対称性が codegen に明示的に反映された。
- `&` は load を取り去り、`*` は load を足す。配列名は decay する。
- `.bss` でグローバル、`.rodata` で文字列リテラル、`%rip` 相対でアクセス。
- char/int/pointer のサイズ別 load/store: `movsbl`/`movl`/`movq`、`movb`/`movl`/`movq`。
- `printf` が呼べるようになり、tiny-c は実用的な C のサブセットに到達した。
