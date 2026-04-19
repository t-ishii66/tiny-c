# 章3: 変数

## ゴール

```c
int main() {
    int a = 3;
    int b = 4;
    return a + b * 2;
}
```

変数に値を保存し、それを読み出して計算する。

## 核心: 変数はメモリ上の場所に付けた名前

ソースコード上の変数名 `a` は、コンパイル後のアセンブリには存在しない。コンパイラは変数名を**メモリ上のアドレス**に変換する。この「名前 → アドレス」の対応表が**シンボルテーブル**だ。

tiny-c では、ローカル変数はスタック上に配置する。各変数は `rbp`（ベースポインタ）からの**相対オフセット**で管理する:

```
rbp          ┌──────────┐
             │  古い rbp │
  -8(%rbp)   ├──────────┤
             │  a (= 3) │   ← 最初の変数
 -16(%rbp)   ├──────────┤
             │  b (= 4) │   ← 次の変数
             └──────────┘
                         ← rsp
```

変数が宣言されるたびに、オフセットを 8 ずつ減らしていく。`a` は `-8(%rbp)`、`b` は `-16(%rbp)`。

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp           # 変数2つ分 (8×2=16) のスタック領域を確保

    # int a = 3;
    movl  $3, %eax            # 初期値 3 を eax に
    movl  %eax, -8(%rbp)      # eax → a のメモリ位置に書き込み

    # int b = 4;
    movl  $4, %eax
    movl  %eax, -16(%rbp)     # eax → b のメモリ位置に書き込み

    # return a + b * 2;
    leaq  -8(%rbp), %rax      # a のアドレスを rax に
    movl  (%rax), %eax        # そのアドレスから値を読む → eax = 3
    pushq %rax                # 退避

    leaq  -16(%rbp), %rax     # b のアドレス
    movl  (%rax), %eax        # eax = 4
    pushq %rax                # 退避
    movl  $2, %eax            # eax = 2
    movl  %eax, %ecx
    popq  %rax                # eax = 4
    imull %ecx, %eax          # eax = 4 * 2 = 8

    movl  %eax, %ecx
    popq  %rax                # eax = 3
    addl  %ecx, %eax          # eax = 3 + 8 = 11

    leave
    ret
```

## 変数の2つの操作

変数に対する操作は、本質的に2つしかない:

| 操作 | 意味 | アセンブリ |
|-----|------|-----------|
| **読み取り** | 変数のアドレスから値をロード | `movl -8(%rbp), %eax` |
| **書き込み** | 値を変数のアドレスにストア | `movl %eax, -8(%rbp)` |

コード生成では、この2つを明確に分けて扱う:

```c
// 書き込み (宣言時の初期化)
gen_expr(node->lhs);                       // 初期値を計算 → eax
emit("movl %%eax, %d(%%rbp)", v->offset);  // eax → メモリ

// 読み取り (式の中で変数を参照)
emit("leaq %d(%%rbp), %%rax", v->offset);  // アドレスを rax に
emit("movl (%%rax), %%eax");               // メモリ → eax
```

## シンボルテーブルの実装

tiny-c のシンボルテーブルは連結リストで十分だ:

```c
typedef struct Var {
    char *name;       // 変数名
    int offset;       // rbp からのオフセット (-8, -16, ...)
    Type type;        // 型 (TY_INT, TY_CHAR, ...)
    struct Var *next;
} Var;

static Var *locals;       // 現在の関数のローカル変数リスト
static int stack_offset;  // 次に使えるオフセット
```

変数が宣言されるたびに:

```c
static Var *add_local(const char *name, Type type, ...) {
    Var *v = calloc(1, sizeof(Var));
    v->name = strdup(name);
    v->type = type;
    stack_offset -= 8;          // 8バイト分、下に伸ばす
    v->offset = stack_offset;   // -8, -16, -24, ...
    v->next = locals;
    locals = v;
    return v;
}
```

変数の参照は名前で線形探索するだけ:

```c
static Var *find_var(const char *name) {
    for (Var *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}
```

## 代入

代入 `x = expr` のコード生成は3ステップ:

```c
case ND_ASSIGN:
    gen_expr(node->rhs);          // 1. 右辺を計算 → eax
    emit("pushq %%rax");          //    結果を退避
    gen_addr(node->lhs);          // 2. 左辺のアドレスを計算 → rax
    emit("movq %%rax, %%rcx");    //    アドレスを rcx に移動
    emit("popq %%rax");           //    値を eax に復元
    gen_store(expr_type(node));   // 3. 値をアドレスにストア
```

ここで `gen_addr` は新しい関数だ。変数の**値**ではなく**アドレス**を計算する。この値とアドレスの区別が、次の章以降で重要になる。

## `subq $16, %rsp` — なぜ最初に領域を確保するのか

関数の先頭で `subq $16, %rsp` を実行し、ローカル変数の領域を一括確保している。変数ごとに `pushq` で1つずつ伸ばすこともできるが、一括確保する理由は:

- 関数呼び出し時にスタックが 16 バイト境界にアラインされている必要がある（x86-64 ABI の要件）
- 変数の配置が宣言順に固定され、オフセットが予測可能になる

コンパイラは関数本体を先にスキャンして必要なバイト数を数え、プロローグで確保する:

```c
int space = nparams * 8 + count_locals(node->body);
space = (space + 15) & ~15;   // 16バイトにアライン
emit("subq $%d, %%rsp", space);
```

## テスト

```bash
$ ./tinyc test/parse_expr.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
11
```

## この章の要点

1. **変数はメモリ上のアドレスに付けた名前**。コンパイル後、変数名は消え、オフセットだけが残る
2. **シンボルテーブル**が「名前 → オフセット」の対応を管理する
3. 変数の操作は**読み取り**（メモリ → レジスタ）と**書き込み**（レジスタ → メモリ）の2種類だけ

## 次の章へ

変数を使えるようになったが、まだ一本道のコードしか書けない。次の章で `if` と `while` を導入し、条件によって実行経路を変える。
