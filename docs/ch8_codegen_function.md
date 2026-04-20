# 章8: コード生成 — 関数

## この章で学ぶこと

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(3, 4);
}
```

関数の定義と呼び出しのコード生成を見る。

## 核心: 呼び出し規約という契約

関数呼び出しは、呼ぶ側（caller）と呼ばれる側（callee）の**契約**で成り立つ。この契約を**呼び出し規約**（calling convention）と呼ぶ。

x86-64 Linux の **System V AMD64 ABI** は次のように定める:

| 項目 | ルール |
|------|--------|
| 引数の渡し方 | 第1〜6引数を `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` に入れる |
| 戻り値 | `eax`（32bit int）/ `rax`（64bit ポインタ）に入れる |
| スタック | `call` 命令の時点で 16 バイトアラインされていること |

呼ぶ側と呼ばれる側がこのルールに従えば、**お互いの実装を知らなくても関数を呼べる**。C の関数を Python から呼べるのも、OS の API を呼べるのも、この契約のおかげだ。

## 生成されるアセンブリ

### 呼ばれる側（add 関数の定義）

```asm
    .globl add
add:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp

    # 引数をレジスタからスタックにコピー
    movq  %rdi, -8(%rbp)      # 第1引数 a → スタック
    movq  %rsi, -16(%rbp)     # 第2引数 b → スタック

    # return a + b;
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax        # eax = a
    pushq %rax
    leaq  -16(%rbp), %rax
    movl  (%rax), %eax        # eax = b
    movl  %eax, %ecx
    popq  %rax                # eax = a
    addl  %ecx, %eax          # eax = a + b

    leave
    ret
```

引数は `rdi`, `rsi` で届くが、すぐにスタックにコピーする。こうすれば引数をローカル変数と**まったく同じ方法**で扱える。章6 で学んだ変数のコード生成がそのまま使える。

### 呼ぶ側（main 関数からの呼び出し）

```asm
    .globl main
main:
    pushq %rbp
    movq  %rsp, %rbp

    # add(3, 4)
    movl  $3, %eax            # 第1引数を評価
    pushq %rax                # スタックに退避
    movl  $4, %eax            # 第2引数を評価
    pushq %rax
    popq  %rsi                # 第2引数 → rsi
    popq  %rdi                # 第1引数 → rdi
    movl  $0, %eax            # AL = 0（可変長引数の浮動小数点数の数）
    call  add                 # add を呼ぶ → 戻り値は eax に

    leave
    ret
```

引数は左から右に評価してスタックに退避し、最後にまとめて ABI のレジスタに pop する。`call add` が実行されると、CPU は戻りアドレスをスタックに push してから `add` にジャンプする。

## スタックフレームの全体像

`main` が `add(3, 4)` を呼んだ瞬間のスタック:

```
     main のフレーム              add のフレーム
┌──────────────────┐       ┌──────────────────┐
│ main の呼び元の   │       │ main への         │
│ 戻りアドレス      │       │ 戻りアドレス       │
├──────────────────┤       ├──────────────────┤
│ main の古い rbp   │← main │ main の rbp       │← add
│                  │  の    ├──────────────────┤  の
│                  │  rbp   │ a = 3            │  rbp
│                  │       ├──────────────────┤
│                  │       │ b = 4            │
└──────────────────┘       └──────────────────┘← rsp
```

各関数が自分の `rbp` を持ち、自分のローカル変数に `rbp` 相対でアクセスする。`leave` + `ret` でフレームを巻き戻し、呼び出し元に戻る。

## 再帰が「ただ動く」理由

```c
int fib(int n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
```

再帰に特別な仕組みは不要だ。`fib(3)` を呼ぶと:

```
fib(3) のフレーム [n=3]
  └→ fib(2) のフレーム [n=2]
       └→ fib(1) のフレーム [n=1]
       └→ fib(0) のフレーム [n=0]
  └→ fib(1) のフレーム [n=1]
```

呼び出しごとに**新しいスタックフレームが積まれる**。各フレームに独立した `n` のコピーがある。だから `fib(3)` の `n=3` と `fib(2)` の `n=2` が混ざることはない。

スタックフレームの仕組みを正しく実装すれば、再帰は**追加の実装なしに動く**。

## 関数定義の実装

`gen_func` の構造:

```c
static void gen_func(Node *node) {
    // 1. 関数ごとの状態をリセット
    locals = NULL;
    stack_offset = 0;

    // 2. 必要なスタック領域を計算
    int space = nparams * 8 + count_locals(node->body);
    space = (space + 15) & ~15;

    // 3. プロローグ
    emit("pushq %%rbp");
    emit("movq %%rsp, %%rbp");
    if (space > 0)
        emit("subq $%d, %%rsp", space);

    // 4. 引数をレジスタ → スタックにコピー
    static const char *param_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
    int i = 0;
    for (NodeList *l = node->children; l; l = l->next, i++) {
        Var *v = add_local(p->name, p->type, 0, 0);
        emit("movq %%%s, %d(%%rbp)", param_regs[i], v->offset);
    }

    // 5. 本体
    gen_stmt(node->body);

    // 6. 暗黙の return 0
    emit("movl $0, %%eax");
    emit("leave");
    emit("ret");
}
```

ステップ4 で引数を `add_local` でシンボルテーブルに登録することで、以降の本体では引数をローカル変数と区別なく扱える。

## 関数呼び出しの実装

```c
case ND_CALL: {
    static const char *regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};

    // 引数を評価してスタックに push
    int nargs = 0;
    for (NodeList *l = node->children; l; l = l->next) {
        gen_expr(l->node);
        emit("pushq %%rax");
        nargs++;
    }

    // pop して ABI レジスタに入れる
    for (int i = nargs - 1; i >= 0; i--)
        emit("popq %%%s", regs[i]);

    emit("movl $0, %%eax");      // AL = 0（printf 等の可変長引数用）
    emit("call %s", node->name);
    break;
}
```

「引数を評価 → push → pop してレジスタへ → call」の4ステップだ。

## printf を呼ぶ

tiny-c では外部関数を宣言なしで呼べる。リンク時に gcc が解決する:

```c
int main() {
    printf("hello %d\n", 42);
    return 0;
}
```

コンパイラから見れば `printf` も `add` も同じ `call` 命令だ。呼び出し規約に従って引数をレジスタに入れ、`call` するだけ。**リンカが関数のアドレスを埋めてくれる**。

## テスト

```bash
$ ./tinyc test/ch4_func.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
7

$ ./tinyc test/ch4_recursive.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
55
```

## この章の要点

1. **呼び出し規約（ABI）** は caller と callee の契約。引数のレジスタ、戻り値のレジスタ、スタックの状態を規定する
2. 引数はレジスタで受け取り、**スタックにコピーすればローカル変数と同じ扱い**になる
3. **再帰は特別な仕組みではない**。呼び出しごとにスタックフレームが積まれることの自然な帰結

## 次の章へ

ここまでで、式・変数・制御構造・関数——プログラミングの基本要素がすべて揃った。最後の章では、C 言語の核心であるポインタを扱う。
