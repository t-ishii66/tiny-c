# 章6: ポインタ

## ゴール

```c
int main() {
    int x = 0;
    int *p = &x;
    *p = 99;
    return x;
}
```

`x` を直接書き換えず、ポインタ `p` 経由で書き換えて、`x` が 99 になることを確かめる。

## 核心: 値とアドレスの二面性

ここまでの章で、変数は「メモリ上のアドレスに付けた名前」だと学んだ。変数 `x` に対する操作は常に:

1. `x` のアドレスを計算する
2. そのアドレスから値を読む or そのアドレスに値を書く

だった。ポインタとは、**この「アドレス」自体を値として扱う**仕組みだ。

コンパイラの視点で言い換えると、式には2つの顔がある:

| | 意味 | 例 | コード生成 |
|---|---|---|---|
| **rvalue** | 式の「値」 | `x` → `5` | `gen_expr` |
| **lvalue** | 式の「アドレス」 | `x` → `-8(%rbp)` | `gen_addr` |

`&x` は `x` の lvalue（アドレス）を rvalue として返す。`*p` は `p` の rvalue（値 = アドレス）を lvalue として使う。

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp

    # int x = 0;
    movl  $0, %eax
    movl  %eax, -8(%rbp)       # x は -8(%rbp) に配置

    # int *p = &x;
    leaq  -8(%rbp), %rax       # ← &x: x のアドレスを計算 (gen_addr)
    movq  %rax, -16(%rbp)      # p に「x のアドレス」を格納 (movq: 64bit)

    # *p = 99;
    movl  $99, %eax            # 値 99
    pushq %rax                 # 退避

    leaq  -16(%rbp), %rax      # p 自体のアドレス
    movq  (%rax), %rax         # ← p の値を読む (= x のアドレス)
    movq  %rax, %rcx           # 書き込み先アドレス → rcx

    popq  %rax                 # 値 99 → eax
    movl  %eax, (%rcx)         # ← 99 を x のアドレスに書き込む

    # return x;
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax         # eax = 99 (x は *p 経由で書き換わっている)
    leave
    ret
```

## 図: メモリの状態

`*p = 99;` 実行前:

```
アドレス        値          変数名
─────────────────────────────
-8(%rbp)   │  0         │  x
-16(%rbp)  │ -8(%rbp)のアドレス │  p  ──→ x を指す
```

`*p = 99;` 実行後:

```
アドレス        値          変数名
─────────────────────────────
-8(%rbp)   │  99        │  x  ← p 経由で書き換わった
-16(%rbp)  │ (同じ)      │  p
```

`x` を名指ししていないのに、`x` の値が変わった。ポインタは**間接参照**——名前を知らなくても、アドレスさえあればメモリを読み書きできる。

## gen_addr: コード生成のもう一つの柱

章2 で導入した `gen_expr` は式の**値**を計算する。ポインタの導入により、式の**アドレス**を計算する `gen_addr` が必要になった:

```c
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_IDENT: {
        Var *v = find_var(node->name);
        emit("leaq %d(%%rbp), %%rax", v->offset);
        break;
    }
    case ND_DEREF:
        // *p のアドレス = p の値
        gen_expr(node->lhs);
        break;
    case ND_INDEX:
        // a[i] のアドレス = a の先頭 + i * 要素サイズ
        ...
        break;
    }
}
```

`gen_expr` と `gen_addr` の関係を整理する:

```
gen_expr(x)    = gen_addr(x) + load     「x のアドレスから値を読む」
gen_expr(&x)   = gen_addr(x)            「x のアドレスそのものが値」
gen_expr(*p)   = gen_expr(p) + load     「p の値をアドレスとして、そこから読む」
gen_addr(*p)   = gen_expr(p)            「p の値がそのままアドレス」
```

`&` は load を削り、`*` は load を足す。この対称性が美しい。

## 代入の一般化

章3 で代入 `x = 5` を実装したが、ポインタの導入により代入の左辺が一般化された:

```c
x = 5;      // gen_addr(x)  → leaq -8(%rbp), %rax
*p = 5;     // gen_addr(*p) → gen_expr(p) → p の値がアドレス
a[i] = 5;   // gen_addr(a[i]) → base + i * size
```

代入のコード生成は**常に同じ**:

```c
case ND_ASSIGN:
    gen_expr(node->rhs);      // 右辺の値を計算
    emit("pushq %%rax");
    gen_addr(node->lhs);      // 左辺のアドレスを計算
    emit("movq %%rax, %%rcx");
    emit("popq %%rax");
    gen_store(type);           // 値をアドレスにストア
```

`gen_addr` さえあれば、左辺が変数でも、ポインタ間接参照でも、配列要素でも、同じコードで処理できる。

## 配列

配列 `int a[3]` は、スタック上に連続した領域を確保するだけだ:

```
         ┌──────┐
-8(%rbp) │ a[0] │  4バイト
         ├──────┤
-12      │ a[1] │  4バイト
         ├──────┤
-16      │ a[2] │  4バイト
         └──────┘
```

配列名 `a` は式の中では先頭要素のアドレスに変換される（配列のポインタへの**暗黙変換**）:

```c
// gen_expr で ND_IDENT かつ is_array の場合:
gen_addr(node);   // 値をロードせず、アドレスを返す
```

`a[i]` は `*(a + i * sizeof(int))` と等価。`gen_addr` でアドレスを計算する:

```c
case ND_INDEX:
    gen_expr(node->lhs);      // 配列のベースアドレス
    emit("pushq %%rax");
    gen_expr(node->rhs);      // インデックス i
    emit("cltq");             // 32bit → 64bit に符号拡張
    emit("imulq $4, %%rax");  // i * sizeof(int)
    emit("popq %%rcx");
    emit("addq %%rcx, %%rax"); // base + i * 4
```

## ポインタと配列の違い

ソースコード上では `a[i]` と `p[i]` は同じに見えるが、コンパイラの内部では異なる:

| | 配列 `int a[3]` | ポインタ `int *p` |
|---|---|---|
| メモリ上 | 12バイトの連続領域 | 8バイト（アドレスを格納） |
| 式中の値 | 先頭アドレス（暗黙変換） | 格納されたアドレス |
| `[i]` のベース | `gen_addr(a)` — 領域そのもの | `gen_expr(p)` — 格納された値 |

この違いを `gen_expr` の `ND_IDENT` で処理している:

```c
if (v->is_array)
    gen_addr(node);           // 配列: アドレスがそのまま値
else {
    gen_addr(node);
    emit("movl (%%rax), %%eax");  // 変数: アドレスから値をロード
}
```

## テスト

```bash
$ ./tinyc test/ch5_pointer_write.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
99

$ ./tinyc test/ch6_array.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
60
```

## この章の要点

1. **ポインタ = アドレスを値として扱う**。`&` でアドレスを取得し、`*` でアドレスを通じて読み書きする
2. コード生成には **`gen_expr`（値）と `gen_addr`（アドレス）の2つの関数**がある。`&` と `*` はこの2つの間を行き来する演算子
3. **配列は連続メモリ + ポインタ演算**。コンパイラが `a[i]` を `base + i * size` に変換する

## この先の学び方

ここまでの6章で、コンパイラの核心部分——式の評価、変数、制御構造、関数、ポインタ——を実装した。

残りの「雑多な部分」は、ここで学んだ原理の応用にすぎない:

- **型システム** — `gen_store` で型ごとに命令を変えたように、より精密に型を追跡する
- **最適化** — 定数畳み込み、レジスタ割り当て、不要コード除去 etc.
- **構造体** — 変数のオフセット管理を、構造体メンバのオフセットに拡張する
- **リンカ** — `call` で名前を書いた先を実際のアドレスに埋める
- **ELF / 実行ファイル形式** — `.text`, `.rodata`, `.bss` をバイナリにパッケージする

どれも新しい**原理**ではなく、同じ原理の**適用範囲の拡大**だ。tiny-c のコードを読み替えながら、一つずつ足していけばよい。
