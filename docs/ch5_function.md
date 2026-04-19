# 章5: 関数

## ゴール

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

自分で関数を定義し、呼び出す。

## 核心: 呼び出し規約という契約

関数呼び出しは、呼び出す側（caller）と呼ばれる側（callee）の**契約**で成り立つ。この契約を**呼び出し規約**（calling convention）と呼ぶ。

x86-64 Linux では **System V AMD64 ABI** を使う。契約の中身はこれだけ:

| 項目 | ルール |
|------|-------|
| 引数の渡し方 | 第1〜6引数を `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` に入れる |
| 戻り値 | `rax`（32bit なら `eax`）に入れる |
| スタック | 呼び出し前に 16 バイトアラインされていること |

呼ぶ側と呼ばれる側が同じルールに従えば、**お互いの実装を知らなくても関数を呼べる**。これが ABI の力だ。C で書いた関数を Python から呼んだり、OS の API を呼んだりできるのはこの契約のおかげだ。

## 生成されるアセンブリ

### 呼ばれる側（add 関数）

```asm
add:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp

    # 引数をレジスタからスタックにコピー
    movq  %rdi, -8(%rbp)      # 第1引数(a) → スタック
    movq  %rsi, -16(%rbp)     # 第2引数(b) → スタック

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
    ret                       # 戻り値は eax に入ったまま
```

引数は `rdi` と `rsi` で届くが、すぐにスタック上のローカル変数にコピーする。こうすることで、引数をローカル変数とまったく同じ方法で扱える。

### 呼ぶ側（main 関数）

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp

    # add(3, 4)
    movl  $3, %eax            # 第1引数の値
    pushq %rax                # いったんスタックに退避
    movl  $4, %eax            # 第2引数の値
    pushq %rax
    popq  %rsi                # 第2引数 → rsi
    popq  %rdi                # 第1引数 → rdi
    movl  $0, %eax            # AL = 0 (可変長引数の浮動小数点数の数)
    call  add                 # add を呼ぶ

    # int result = <戻り値>;
    movl  %eax, -8(%rbp)      # 戻り値(eax) → result
```

## スタックフレームの全体像

`main` が `add(3, 4)` を呼んだ瞬間のスタック:

```
     main のフレーム              add のフレーム
┌──────────────────┐       ┌──────────────────┐
│ (main の呼び元)   │       │ main への戻りアドレス │
├──────────────────┤       ├──────────────────┤
│ main の古い rbp   │← main │ main の rbp       │← add
│                  │  の    ├──────────────────┤  の
├──────────────────┤  rbp   │ a = 3            │  rbp
│ result           │       ├──────────────────┤
└──────────────────┘       │ b = 4            │
                           └──────────────────┘
                                              ← rsp
```

各関数が自分の rbp を持ち、自分のローカル変数にアクセスする。関数が戻ると `leave` + `ret` でフレームが巻き戻される。

## 再帰が「ただ動く」理由

```c
int fib(int n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
```

再帰呼び出しにも特別な仕組みは不要だ。`fib(3)` を呼ぶと:

```
fib(3) のフレーム → fib(2) のフレーム → fib(1) のフレーム → ...
```

呼び出しごとに**新しいスタックフレームが積まれる**。各フレームには独立した `n` のコピーがある。だから `fib(3)` の `n=3` と `fib(2)` の `n=2` が混ざることはない。

関数呼び出しの仕組みを正しく実装すれば、再帰は**追加の実装なしに動く**。

## コード生成の実装

### 関数定義 (gen_func)

```c
static void gen_func(Node *node) {
    locals = NULL;
    stack_offset = 0;

    // スタック領域を計算
    int space = nparams * 8 + count_locals(node->body);
    space = (space + 15) & ~15;   // 16バイトアライン

    // プロローグ
    emit("pushq %%rbp");
    emit("movq %%rsp, %%rbp");
    if (space > 0)
        emit("subq $%d, %%rsp", space);

    // 引数をレジスタ → スタックにコピー
    static const char *param_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
    int i = 0;
    for (NodeList *l = node->children; l; l = l->next, i++) {
        Var *v = add_local(p->name, p->type, 0, 0);
        emit("movq %%%s, %d(%%rbp)", param_regs[i], v->offset);
    }

    gen_stmt(node->body);   // 本体

    // 暗黙の return 0
    emit("movl $0, %%eax");
    emit("leave");
    emit("ret");
}
```

### 関数呼び出し (gen_expr の ND_CALL)

```c
case ND_CALL:
    // 引数を評価して、一旦スタックに push
    for (NodeList *l = node->children; l; l = l->next) {
        gen_expr(l->node);
        emit("pushq %%rax");
        nargs++;
    }
    // pop して ABI で決まったレジスタに入れる
    for (int i = nargs - 1; i >= 0; i--)
        emit("popq %%%s", regs[i]);

    emit("movl $0, %%eax");   // 可変長引数用 (浮動小数点数 = 0個)
    emit("call %s", node->name);
```

「引数を評価 → push → pop してレジスタに入れる → call」。この4ステップだけだ。

## printf を呼ぶ

tiny-c では外部関数を宣言なしで呼べる。リンク時に gcc が解決してくれる:

```c
int main() {
    printf("hello %d\n", 42);
    return 0;
}
```

コンパイラから見ると、`printf` も `add` も同じ `call` 命令だ。呼び出し規約に従って引数をレジスタに入れ、`call` するだけ。宣言がなくても**リンカが関数のアドレスを埋めてくれる**。

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

1. **呼び出し規約 (ABI)** は caller と callee の契約。引数のレジスタ、戻り値のレジスタ、スタックの状態を規定する
2. **引数はレジスタで渡す**が、callee 側でスタックにコピーすればローカル変数と同じ扱いになる
3. **再帰は特別な仕組みではない**。呼び出しごとにスタックフレームが積まれることの自然な帰結だ

## 次の章へ

ここまでの章で、値を計算し、名前をつけ、条件分岐・ループし、関数にまとめる——プログラミングの基本要素がすべて揃った。次の章では、C 言語の核心であるポインタを導入する。
