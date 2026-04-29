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
    : /* empty */                { $$ = NULL; }
    | func_def func_defs         { $$ = new_node_list($1, $2); }
    ;

func_def
    : INT IDENT '(' params ')' '{' stmts '}'           /* params 追加 */
                                 { $$ = new_func_def($2, $4, new_block($7)); }
    ;

params                                                 /* 追加 */
    : /* empty */                { $$ = NULL; }
    | param_list                 { $$ = $1; }
    ;
param_list                                             /* 追加 */
    : param                      { $$ = new_node_list($1, NULL); }
    | param ',' param_list       { $$ = new_node_list($1, $3); }
    ;
param                                                  /* 追加 */
    : INT IDENT                  { $$ = new_ident($2); }
    ;

primary
    : INT_LIT                    { $$ = new_int_lit($1); }
    | IDENT                      { $$ = new_ident($1); }
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }    /* 追加 */
    | '(' expr ')'               { $$ = $2; }
    ;

args                                                   /* 追加 */
    : /* empty */                { $$ = NULL; }
    | arg_list                   { $$ = $1; }
    ;
arg_list                                               /* 追加 */
    : expr                       { $$ = new_node_list($1, NULL); }
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }
    ;
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
static const char *arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8",  "%r9"};
static const char *arg_regs32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};

static int stack_offset;     /* 追加: rsp delta in 8-byte units */

static void emit_push(void) {
    fprintf(out, "  pushq %%rax\n");
    stack_offset++;
}
static void emit_pop(const char *reg) {
    fprintf(out, "  popq %s\n", reg);
    stack_offset--;
}

case NODE_CALL: {
    int pad = (stack_offset % 2) != 0;
    if (pad) {
        fprintf(out, "  subq $8, %%rsp\n");
        stack_offset++;
    }
    int n_args = push_args(node->args);
    for (int i = 0; i < n_args; i++)
        emit_pop(arg_regs64[i]);
    fprintf(out, "  movl $0, %%eax\n");
    fprintf(out, "  call %s\n", node->name);
    if (pad) {
        fprintf(out, "  addq $8, %%rsp\n");
        stack_offset--;
    }
    return;
}

static void gen_func(Node *fn) {
    locals = NULL; frame_size = 0; stack_offset = 0;
    for (NodeList *l = fn->params; l; l = l->next)
        add_local(l->node->name);
    collect_locals(fn->body);
    int aligned = (frame_size + 15) & ~15;

    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    if (aligned > 0) fprintf(out, "  subq $%d, %%rsp\n", aligned);

    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        int off = find_local(l->node->name);
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);
        i++;
    }
    gen_stmt(fn->body);
    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");  /* implicit return */
}

void codegen(Node *prog, FILE *output) {
    out = output;
    label_count = 0;
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)
        gen_func(l->node);
}
```

## 6. main.c / Makefile — 変更なし

4章連続で main.c と Makefile は変えていない。フロントエンドのインターフェース (`yyparse`、`program` グローバル、`codegen()`) が安定している証拠。

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

**実際のアセンブリにこのパディングが入っていることを目で確認できる**。tiny-c はちゃんと ABI を守っている。

## 9. 各フレームの独立性

`fact(5)` を実行するとき、スタックには 5回分の `fact` フレームが積まれる。それぞれが独立した `n` を持つ。

`fact(5)` の `n=5` が `-8(%rbp_5)` に、`fact(4)` の `n=4` が `-8(%rbp_4)` に...という具合。`%rbp` の値だけが各フレームで違うので、同じ `-8(%rbp)` というアセンブリ表記でも、実行時に違うメモリ位置を指す。

これが ch04 までで作ってきた **「変数は `%rbp` 相対のアドレス」** の本当の意味。`%rbp` を **その関数呼び出し固有の値** に設定するから、同名の変数（再帰の `n`）でも混ざらない。

## 10. ここまでで作ったもの

- 複数関数のプログラム。`PROGRAM` ノードの下に `FUNC_DEF` のリスト。
- パラメータ付き関数定義、関数呼び出し式 (`NODE_CALL`)。
- **System V AMD64 ABI** に従った引数渡し（`%rdi`〜`%r9`）。
- **16バイトアライメント** を保つ事前確保 (`subq $aligned_frame`) と動的パディング (`stack_offset`)。
- **再帰** がコンパイラの特別な仕掛けなしに動く。フレームの独立性のおかげ。
- 暗黙の `return 0`（落っこち防止）。

第6章では、ここに **ポインタと配列** を加える。`&x`、`*p`、`a[i]`、文字列リテラル、グローバル変数。**lvalue** の世界が本格的に開ける ── アドレスを返す codegen (`gen_addr`)、`*` と `&` の対称性、配列アクセスのアドレス計算。tiny-c の最終形が見えてくる。

## まとめ

- **ABI は契約**。守れば再帰も外部関数呼び出しも自然に動く。
- フレーム確保を **事前一括** に変更 (`subq $aligned_frame`)。事前 pre-pass でフレームサイズ計算。
- 引数は **逆順 push → pop でレジスタ** という定石パターン。
- アライメント追跡 (`stack_offset`) で **動的パディング**。call の瞬間は必ず 16-aligned。
- 4章連続で main.c と Makefile を変えていない ── 設計の安定。
