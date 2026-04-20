# 章9: コード生成 — ポインタ

## この章で学ぶこと

```c
int main() {
    int x = 0;
    int *p = &x;
    *p = 99;
    return x;    // → 99
}
```

`x` を直接書き換えず、ポインタ `p` 経由で書き換える。C 言語の核心であるポインタのコード生成を見る。

## 核心: 値とアドレスの二面性

章6 で、変数は「メモリ上の場所に付けた名前」だと学んだ。変数 `x` を使うとき、コンパイラは常に:

1. `x` のアドレスを計算する（`gen_addr`）
2. そのアドレスから値を読む（load）

の2段階を踏んでいた。ポインタとは、**この「アドレス」自体を値として扱う**仕組みだ。

コンパイラの視点では、式には2つの顔がある:

| | 意味 | 例 | コード生成 |
|---|---|---|---|
| **rvalue** | 式の「値」 | `x` → `5` | `gen_expr` |
| **lvalue** | 式の「場所」 | `x` → `-8(%rbp)` | `gen_addr` |

- `&x` は `x` の lvalue（アドレス）を rvalue として返す
- `*p` は `p` の rvalue（値、つまりアドレス）を lvalue として使う

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp

    # int x = 0;
    movl  $0, %eax
    movl  %eax, -8(%rbp)       # x = 0

    # int *p = &x;
    leaq  -8(%rbp), %rax       # ← &x: x のアドレスを計算
    movq  %rax, -16(%rbp)      # p にアドレスを格納 (movq: 64bit)

    # *p = 99;
    movl  $99, %eax            # 値 99
    pushq %rax                 # 退避

    leaq  -16(%rbp), %rax      # p 自体のアドレス
    movq  (%rax), %rax         # ← p の値を読む (= x のアドレス)
    movq  %rax, %rcx           # 書き込み先 → rcx

    popq  %rax                 # 値 99 → eax
    movl  %eax, (%rcx)         # ← 99 を x のアドレスに書き込む

    # return x;
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax         # eax = 99 (p 経由で書き換わった)
    leave
    ret
```

## 図: メモリの状態

`*p = 99` 実行前:

```
アドレス         値              変数
─────────────────────────────────────
-8(%rbp)    │  0             │  x
-16(%rbp)   │ (x のアドレス)   │  p ──→ x を指す
```

`*p = 99` 実行後:

```
アドレス         値              変数
─────────────────────────────────────
-8(%rbp)    │  99            │  x  ← p 経由で書き換わった
-16(%rbp)   │ (同じ)          │  p
```

`x` を名指ししていないのに `x` の値が変わった。アドレスさえあれば、名前を知らなくてもメモリを読み書きできる——これが**間接参照**だ。

## gen_addr と gen_expr の対称性

`&` と `*` は、`gen_addr` と `gen_expr` の間を行き来する演算子だ:

```
gen_expr(x)    = gen_addr(x)  + load   「アドレスから値を読む」
gen_expr(&x)   = gen_addr(x)           「アドレスそのものが値」
gen_expr(*p)   = gen_expr(p)  + load   「値をアドレスとみなして読む」
gen_addr(*p)   = gen_expr(p)           「値がそのままアドレス」
```

`&` は load を**取り除き**、`*` は load を**追加する**。この対称性が美しい。

実装:

```c
// gen_addr: 式のアドレスを rax に計算する
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_IDENT:
        emit("leaq %d(%%rbp), %%rax", v->offset);
        break;
    case ND_DEREF:
        // *p のアドレス = p の値
        gen_expr(node->lhs);
        break;
    }
}

// gen_expr: 式の値を eax/rax に計算する
static void gen_expr(Node *node) {
    switch (node->kind) {
    case ND_ADDR:
        // &x の値 = x のアドレス
        gen_addr(node->lhs);
        break;
    case ND_DEREF:
        // *p の値 = p の値が指す先の内容
        gen_expr(node->lhs);
        emit("movl (%%rax), %%eax");   // load
        break;
    }
}
```

`gen_addr(ND_DEREF)` は `gen_expr` を呼び、`gen_expr(ND_ADDR)` は `gen_addr` を呼ぶ。相互再帰だ。

## 代入の一般化

章6 で代入 `x = 5` を実装したが、ポインタにより代入の左辺が一般化される:

| 代入式 | gen_addr の動作 |
|--------|----------------|
| `x = 5` | `leaq -8(%rbp), %rax` — 変数のオフセット |
| `*p = 5` | `gen_expr(p)` — ポインタの値がアドレス |
| `a[i] = 5` | ベースアドレス + i × 要素サイズ |

代入のコード生成は**常に同じ**:

```c
case ND_ASSIGN:
    gen_expr(node->rhs);          // 右辺の値
    emit("pushq %%rax");
    gen_addr(node->lhs);          // 左辺のアドレス
    emit("movq %%rax, %%rcx");
    emit("popq %%rax");
    gen_store(type);              // 値をアドレスに書き込み
```

`gen_addr` さえあれば、左辺が変数でもポインタ間接参照でも配列要素でも、同じコードで処理できる。

## 配列

配列 `int a[3]` は、スタック上に連続した領域を確保するだけだ:

```
  -8(%rbp)  ┌──────┐
            │ a[0] │  4バイト
 -12(%rbp)  ├──────┤
            │ a[1] │  4バイト
 -16(%rbp)  ├──────┤
            │ a[2] │  4バイト
            └──────┘
```

配列名 `a` は式の中で**先頭要素のアドレス**に暗黙変換される:

```c
case ND_IDENT:
    Var *v = find_var(node->name);
    if (v->is_array)
        gen_addr(node);      // 値ではなくアドレスを返す
    else {
        gen_addr(node);
        load();              // アドレスから値を読む
    }
```

`a[i]` は `gen_addr` で `ベースアドレス + i × 要素サイズ` を計算する:

```c
case ND_INDEX:
    gen_expr(node->lhs);             // ベースアドレス（配列はアドレスに変換済み）
    emit("pushq %%rax");
    gen_expr(node->rhs);             // インデックス i
    emit("cltq");                    // 32bit → 64bit に符号拡張
    emit("imulq $4, %%rax, %%rax");  // i × sizeof(int)
    emit("popq %%rcx");
    emit("addq %%rcx, %%rax");       // base + i × 4
```

## ポインタと配列の違い

ソースコード上では `a[i]` と `p[i]` は同じに見えるが、内部の扱いは異なる:

| | 配列 `int a[3]` | ポインタ `int *p` |
|---|---|---|
| メモリ上 | 12バイトの連続領域 | 8バイト（アドレスを保持） |
| 式中の値 | 先頭アドレス（暗黙変換） | 格納されたアドレス |
| `[i]` のベース | `gen_addr(a)` — 領域そのもの | `gen_expr(p)` — 格納された値を読む |

配列名は `gen_addr` でアドレスを直接得る。ポインタは `gen_expr` で値（= アドレス）を読む。結果としてどちらもアドレスを返すので、`a[i]` と `p[i]` は同じ `gen_addr(ND_INDEX)` で処理できる。

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

1. **ポインタ = アドレスを値として扱う**。`&` でアドレスを取得し、`*` でアドレス経由で読み書きする
2. `gen_expr`（値）と `gen_addr`（アドレス）の**相互再帰**がポインタの実装の核心。`&` は load を除き、`*` は load を足す
3. 代入の左辺は `gen_addr` で統一的に処理できる。変数、`*p`、`a[i]` のどれでも同じコード

## この先の学び方

9章を通して、コンパイラの核心部分を実装した:

```
ソースコード
  → [ch2: lexer]   トークン列
  → [ch3: parser]  文法規則で木を組み立て
  → [ch4: AST]     Node 構造体で表現
  → [ch5: 式]      木を再帰的に辿って命令列へ
  → [ch6: 変数]    名前をスタック上のアドレスに変換
  → [ch7: 制御]    ラベルとジャンプ
  → [ch8: 関数]    呼び出し規約とスタックフレーム
  → [ch9: ポインタ] 値とアドレスの二面性
```

残りの「雑多な部分」は、ここで学んだ原理の応用だ:

- **型システム** — `gen_store` で型ごとに命令を変えたように、より精密に型を追跡する
- **最適化** — 定数畳み込み、レジスタ割り当て、不要コード除去
- **構造体** — 変数のオフセット管理を構造体メンバに拡張する
- **リンカ** — `call` で書いた名前を実際のアドレスに解決する
- **ELF** — `.text`, `.rodata`, `.bss` をバイナリにパッケージする

どれも新しい原理ではなく、同じ原理の**適用範囲の拡大**だ。
