# 05 — 完全形とビルド

ch04 との差分を並べ、ビルドして再帰を動かす。フィボナッチの生成アセンブリで、**入れ子の call にパディングが入る瞬間**を見届ける。

## 1. lexer.l — カンマ追加

ch04 のものに `,` を1行追加するだけ。

```flex
","         { return ','; }                           /* 追加 */
```

## 2. parser.y — 大きく拡張

差分が多いので、要点だけ。完全版は `steps/ch05/src/parser.y`。

```yacc
%type <node> ... param
%type <list> stmts func_defs params param_list args arg_list

program
    : func_defs                  { program = new_program($1); }
    ;

func_defs                                              /* 追加 */
    : /* empty */                { $$ = NULL; }        /* 追加 */
    | func_def func_defs         { $$ = new_node_list($1, $2); }   /* 追加 */
    ;                                                  /* 追加 */

func_def
    : INT IDENT '(' params ')' '{' stmts '}'           /* params 追加 */
                                 { $$ = new_func_def($2, $4, new_block($7)); }
    ;

params                                                 /* 追加 */
    : /* empty */                { $$ = NULL; }        /* 追加 */
    | param_list                 { $$ = $1; }          /* 追加 */
    ;                                                  /* 追加 */
param_list                                             /* 追加 */
    : param                      { $$ = new_node_list($1, NULL); }     /* 追加 */
    | param ',' param_list       { $$ = new_node_list($1, $3); }       /* 追加 */
    ;                                                  /* 追加 */
param                                                  /* 追加 */
    : INT IDENT                  { $$ = new_ident($2); }               /* 追加 */
    ;                                                  /* 追加 */

primary
    : INT_LIT                    { $$ = new_int_lit($1); }
    | IDENT                      { $$ = new_ident($1); }
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }    /* 追加 */
    | '(' expr ')'               { $$ = $2; }
    ;

args                                                   /* 追加 */
    : /* empty */                { $$ = NULL; }        /* 追加 */
    | arg_list                   { $$ = $1; }          /* 追加 */
    ;                                                  /* 追加 */
arg_list                                               /* 追加 */
    : expr                       { $$ = new_node_list($1, NULL); }     /* 追加 */
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }       /* 追加 */
    ;                                                  /* 追加 */
```

`%expect 1` (dangling-else) は ch04 から引き続き。

## 3. ast.h — 新ノード型2つ、新フィールド2つ

```c
typedef enum {
    /* ... ch04 と同じ ... */
    NODE_CALL,           /* 追加 */
    NODE_FUNC_DEF,       /* シグネチャ変更 (params 追加) */
    NODE_BLOCK,
    NODE_PROGRAM,        /* 追加 */
} NodeKind;

struct Node {
    /* ... 既存 ... */
    NodeList *params;    /* 追加: NODE_FUNC_DEF */
    NodeList *args;      /* 追加: NODE_CALL */
};

Node *new_call(char *name, NodeList *args);                  /* 追加 */
Node *new_func_def(char *name, NodeList *params, Node *body);/* シグネチャ変更 */
Node *new_program(NodeList *funcs);                          /* 追加 */
```

## 4. ast.c — コンストラクタと print_ast

`new_call`, `new_program` を追加。`new_func_def` は params を受け取るように変更。`print_ast` に `NODE_CALL`、`NODE_PROGRAM` のケースを追加し、`NODE_FUNC_DEF` でパラメータリストも表示。

完全版は `steps/ch05/src/ast.c`。

## 5. codegen.c — 大改修

ch03 から育ててきた `codegen.c` を **再設計**。

主な変更:

- **二相 codegen**: `collect_locals` で先にシンボルテーブルを作り、`gen_func` で本体を生成。
- **事前確保**: プロローグで `subq $aligned_frame, %rsp` を1回。`var_decl` の中では `subq` を出さない。
- **`stack_offset` 追跡**: `emit_push`/`emit_pop` で増減し、call 時のパディング判断に使う。
- **`gen_func`**: 各関数を独立して生成。シンボルテーブルとフレームをリセット。
- **`NODE_CALL` の codegen**: 引数を逆順 push → pop でレジスタへ。アライメントを動的にパディング。
- **`codegen` 本体**: PROGRAM の下にぶら下がる関数をループで生成。

完全版は `steps/ch05/src/codegen.c` を参照。要点だけ抜粋:

```c
static const char *arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8",  "%r9"};   /* 追加 */
static const char *arg_regs32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};  /* 追加 */

static int stack_offset;                                        /* 追加 */

static void emit_push(void) {                                   /* 追加 */
    fprintf(out, "  pushq %%rax\n");                            /* 追加 */
    stack_offset++;                                             /* 追加 */
}                                                               /* 追加 */
static void emit_pop(const char *reg) {                         /* 追加 */
    fprintf(out, "  popq %s\n", reg);                           /* 追加 */
    stack_offset--;                                             /* 追加 */
}                                                               /* 追加 */

/* gen_expr の switch 内に新しく追加するケース */
case NODE_CALL: {                                               /* 追加 */
    int pad = (stack_offset % 2) != 0;                          /* 追加 */
    if (pad) {                                                  /* 追加 */
        fprintf(out, "  subq $8, %%rsp\n");                     /* 追加 */
        stack_offset++;                                         /* 追加 */
    }                                                           /* 追加 */
    int n_args = push_args(node->args);                         /* 追加 */
    for (int i = 0; i < n_args; i++)                            /* 追加 */
        emit_pop(arg_regs64[i]);                                /* 追加 */
    fprintf(out, "  movl $0, %%eax\n");                         /* 追加 */
    fprintf(out, "  call %s\n", node->name);                    /* 追加 */
    if (pad) {                                                  /* 追加 */
        fprintf(out, "  addq $8, %%rsp\n");                     /* 追加 */
        stack_offset--;                                         /* 追加 */
    }                                                           /* 追加 */
    return;                                                     /* 追加 */
}                                                               /* 追加 */

static void gen_func(Node *fn) {                                /* 追加 */
    locals = NULL; frame_size = 0; stack_offset = 0;            /* 追加 */
    for (NodeList *l = fn->params; l; l = l->next)              /* 追加 */
        add_local(l->node->name);                               /* 追加 */
    collect_locals(fn->body);                                   /* 追加 */
    int aligned = (frame_size + 15) & ~15;                      /* 追加 */

    fprintf(out, "  .globl %s\n", fn->name);                    /* 追加 */
    fprintf(out, "%s:\n", fn->name);                            /* 追加 */
    fprintf(out, "  pushq %%rbp\n");                            /* 追加 */
    fprintf(out, "  movq %%rsp, %%rbp\n");                      /* 追加 */
    if (aligned > 0) fprintf(out, "  subq $%d, %%rsp\n", aligned);  /* 追加 */

    int i = 0;                                                  /* 追加 */
    for (NodeList *l = fn->params; l; l = l->next) {            /* 追加 */
        int off = find_local(l->node->name);                    /* 追加 */
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);  /* 追加 */
        i++;                                                    /* 追加 */
    }                                                           /* 追加 */
    gen_stmt(fn->body);                                         /* 追加 */
    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");         /* 追加: implicit return */
}                                                               /* 追加 */

void codegen(Node *prog, FILE *output) {                        /* 変更: ch04 はここでプロローグ直接出力 */
    out = output;
    label_count = 0;
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)             /* 変更: 単一関数 → 関数のループ */
        gen_func(l->node);                                      /* 変更 */
}
```

## 6. main.c / Makefile — 変更なし

## 7. ビルドして動かす

```bash
$ cd steps/ch05
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

引数付き関数:

```bash
$ cat > test.c <<'EOF'
int add(int a, int b) {
    return a + b;
}
int main() {
    return add(3, 4);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
7
```

階乗:

```bash
$ cat > test.c <<'EOF'
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    return fact(5);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
120
```

フィボナッチ:

```bash
$ cat > test.c <<'EOF'
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    return fib(10);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
55
```

相互再帰:

```bash
$ cat > test.c <<'EOF'
int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}
int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}
int main() {
    return is_even(10);   // 1
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
1
```

`is_odd` を呼んでいる時点で `is_odd` は **まだ定義されていない**（後ろで定義される）。tiny-c は意味解析をしないので forward reference でも問題なく動く。

## 8. フィボナッチの生成アセンブリ ── パディングが入る瞬間

`fib(n - 1) + fib(n - 2)` の部分の生成アセンブリ:

```
; fib(n - 2) を計算して push するまで
movl $2, %eax            ; rhs of (n-2): 2
pushq %rax               ; so=1
movl -8(%rbp), %eax      ; lhs of (n-2): n
popq %rcx                ; so=0
subl %ecx, %eax          ; eax = n - 2
pushq %rax               ; arg push, so=1
popq %rdi                ; arg load, so=0
movl $0, %eax
call fib                 ; ★ so=0 (16-aligned), パディング不要
pushq %rax               ; fib(n-2) の結果を保存, so=1

; fib(n - 1) を計算する番
subq $8, %rsp            ; ★★ パディング！ so=1 (奇数) → so=2 にしてアライン
movl $1, %eax            ; rhs of (n-1): 1
pushq %rax               ; so=3
movl -8(%rbp), %eax
popq %rcx                ; so=2
subl %ecx, %eax
pushq %rax               ; so=3
popq %rdi                ; so=2
movl $0, %eax
call fib                 ; ★ so=2 (16-aligned), OK
addq $8, %rsp            ; パディング解除, so=1
popq %rcx                ; fib(n-2) を取り戻す, so=0
addl %ecx, %eax          ; eax = fib(n-1) + fib(n-2)
```

最初の `call fib` (= `fib(n-2)`) の時点では `stack_offset = 0` で偶数 → 何もパディングしない。

その後 `pushq %rax` (fib(n-2) の結果保存) で `stack_offset = 1`。

2回目の `call fib` (= `fib(n-1)`) の前に `stack_offset = 1` を検出 → `subq $8, %rsp` でパディング。call 後 `addq $8, %rsp` で解除。

## 9. 次へ

第6章では **ポインタと配列** を加える。`&x`、`*p`、`a[i]`、文字列リテラル、グローバル変数。アドレスを返す codegen (`gen_addr`)、`*` と `&` の対称性、配列アクセスのアドレス計算。
