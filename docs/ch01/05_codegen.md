# 05 — x86 アセンブリ最小限 と codegen.c

第1章で生成するアセンブリはたった7行。

```asm
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $42, %eax
  leave
  ret
```

この一行一行が何を意味するのかを、最小限の知識から積み上げていく。


## 1. アセンブリとは何か

CPU が直接実行できるのは**機械語**（バイナリの命令列）だ。`0x48 0x89 0xe5` のような数字の並び。これを人間が直接書くのは無理がある。

そこで、機械語の各命令に**ニーモニック**（人間が読みやすい名前）をつけたものが**アセンブリ言語**だ。

```
機械語:    48 89 e5
アセンブリ: movq %rsp, %rbp     ← 人間が読みやすい
```

アセンブリと機械語は **1対1対応**している。アセンブリで `movq %rsp, %rbp` と書けば、必ず `48 89 e5` というバイト列に変換される。これを変換するのが**アセンブラ**（`as` や `gcc` 内蔵のもの）だ。

つまりアセンブリは「読み書きできる機械語」だ。コンパイラがアセンブリを吐けば、あとはアセンブラとリンカが実行ファイルにしてくれる。


## 2. レジスタ ── CPU の中の作業箱

CPU は計算をするとき、**レジスタ**という小さな記憶領域を使う。レジスタは CPU 内部にあって、メモリに比べて圧倒的に速い。値を一時的に置いて計算する「作業台」のようなもの。

x86-64 には汎用レジスタが16個ある。**名前が `r` で始まる 64 ビット幅のレジスタ群**で、`%rax`、`%rbx`、`%rcx`... のように呼ぶ。第1章で使うのはこの3つだけ：

| レジスタ | 幅 | 役割 |
|---------|----|------|
| `%rax` | 64 ビット | 戻り値や計算の主役。下位 32 ビットの別名は `%eax` |
| `%rsp` | 64 ビット | スタックポインタ。スタックの先端を指す |
| `%rbp` | 64 ビット | ベースポインタ。関数のスタックフレームの起点 |

レジスタ名の頭の `%` は AT&T 構文の流儀。

`%rax` と `%eax` の関係は次のとおり：

```
+----------------------+
|         %rax         |   ← 64 ビット全体
+----------+----------+
            |   %eax   |   ← 下位 32 ビット
            +---+------+
                | %ax  |   ← 下位 16 ビット
                +------+
```

整数（`int`）は 32 ビットで扱うので `%eax`、ポインタやアドレスは 64 ビット必要なので `%rax` を使う、という使い分けになる。


## 3. 命令の書き方 ── AT&T 構文

x86 のアセンブリには「Intel 構文」と「AT&T 構文」の2つがある。我々は **AT&T 構文** を使う（gcc が標準で使うため）。Linux のアセンブラ `as` も AT&T 構文がデフォルトだ。

AT&T 構文の特徴：

- レジスタ名に `%` が付く（`%eax`）
- 即値（定数）に `$` が付く（`$42`）
- オペランドの順序は **「ソース → 宛先」**（`mov src, dst`）
- 命令名の末尾に**サイズの接尾辞**を付ける（`b`, `w`, `l`, `q` = 1, 2, 4, 8 バイト）

例：

```asm
movl $42, %eax        # eax に 42 をセット（4バイト転送）
movq %rsp, %rbp       # rbp に rsp の値をコピー（8バイト転送）
pushq %rbp            # rbp の値をスタックに積む（8バイト）
```

## 4. 関数とスタック ── 値はどこに置くか

関数を呼ぶときに必要なのは:

1. **戻り先のアドレス** — 呼ばれた関数が終わったら呼び出し元に戻る。
2. **ローカル変数の置き場所**。
3. **計算の途中結果の保管場所** — レジスタは数が限られるので、複雑な式で退避が要る。

これらに使われるのが**スタック**だ。スタックはメモリの一領域で、関数呼び出しのたびに伸び、戻ると縮む。

x86-64 のスタックは「**上から下に伸びる**」。アドレス的に「下に」というのは、**数値が小さくなる方向**だ。慣習でこれを上下逆にして「スタックの先端は『下』」と表現することが多い。

先ほど登場した `pushq %rbp` がスタックを実際にどう動かすかを図にするとこうなる。

```
高アドレス  ┌──────────────┐
           │              │
           │              │
           │     ...      │  ← push 前の %rsp はここ（直前のスタック先端）
           ├──────────────┤
           │ caller の rbp │  ← push 後の %rsp はここ（8 バイト下に動いた）
           ├──────────────┤
           │              │
低アドレス  └──────────────┘
```

`%rsp` は**スタックの先端**を指すレジスタ。値を積む（`push`）と `%rsp` が下に動き、降ろす（`pop`）と上に戻る。`pushq %rbp` は具体的には「`%rsp` を 8 バイト下に動かして、`%rbp` の値をその場所に書き込む」という2段の動作になっている。

`%rbp` は**今いる関数のフレームの起点**を指すレジスタ。関数の中で「自分のローカル変数はどこにある？」と問うとき、`%rbp` からの相対位置で答えられる。`%rbp` は関数が始まってから終わるまで動かない（変わらない）ので、安定した起点になる。


## 5. 関数プロローグとエピローグ

x86-64 で関数を実装するときの**お決まり**がある。関数の入り口で必ず行う処理を**プロローグ**、出口で行う処理を**エピローグ**と呼ぶ。

### プロローグ

```asm
pushq %rbp           # 呼び出し元の rbp をスタックに退避
movq %rsp, %rbp      # 現在の rsp を rbp にセット（自分のフレームを開始）
```

1行目：`%rbp` には呼び出し元（caller）の関数のベースが入っている。これを破壊しないようにスタックに保存する。
2行目：今のスタックの先端を `%rbp` にセットする。これで、自分の関数のスタックフレームの起点が決まった。以後、自分の関数が終わるまで `%rbp` は動かない。

### エピローグ

```asm
leave                # rbp を復元し、rsp を元に戻す
ret                  # 戻り先アドレスにジャンプ
```

1行目：`leave` は実は2つの命令を合体させたもの。`movq %rbp, %rsp` で「自分のフレームの底まで rsp を戻す」、続いて `popq %rbp` で「保存しておいた呼び出し元の rbp を復元する」。
2行目：`ret` はスタックから戻り先アドレスを取り出して、そこにジャンプする。これで呼び出し元に制御が戻る。

第1章ではローカル変数を使わないので、プロローグは「rbp の保存と更新」だけ、エピローグは `leave` と `ret` だけ、というシンプルな形になる。第3章で変数が登場すると、プロローグが少し複雑になる（フレーム分のスタックを確保する命令が増える）。


## 6. 戻り値の渡し方 ── %eax の約束

C の関数が値を返すとき、その値はどこに置くのか。

答えは **`%eax` レジスタ**だ。これは **System V AMD64 ABI** という呼び出し規約で決まっている。

```
return 42;   →   movl $42, %eax    + return 命令
```

`%rax` の下位32ビットが `%eax`。32ビット整数（`int`）の戻り値は `%eax` にセットする決まりになっている。`int` で十分な値ならこれでよい。`%eax` に 42 が入った状態で `ret` すると、呼び出し元（OS のローダ → libc の `_start` → `main` の呼び出し元）が「main の戻り値は 42 だな」と判断する。

そしてプロセスの**終了コード**（`exit code`）が、結果的に 42 になる。`echo $?` で 42 が出るのは、これが理由だ。

「**`int` の戻り値は `%eax` にセットして `ret`**」。


## 7. アセンブリの全体像

7行をもう一度。

```asm
  .text                  # ここから先はコード領域
  .globl main            # main を外部に公開
main:                    # 関数 main の入り口ラベル
  pushq %rbp             # ┐
  movq %rsp, %rbp        # ┴ プロローグ：スタックフレームを設定
  movl $42, %eax         # 戻り値 42 を eax にセット
  leave                  # ┐
  ret                    # ┴ エピローグ：呼び出し元に戻る
```

行頭が `.` で始まるものは**ディレクティブ**（アセンブラへの指示）。CPU の命令ではなく、「ここから先はコード領域です」「この名前を外部から見えるようにしてください」といった指示だ。

| 行 | 種類 | 意味 |
|----|-----|------|
| `.text` | ディレクティブ | コードセクション開始 |
| `.globl main` | ディレクティブ | `main` をリンカから見える名前にする |
| `main:` | ラベル | この位置に `main` という名前を付ける |
| `pushq %rbp` | 命令 | `%rbp` をスタックに退避 |
| `movq %rsp, %rbp` | 命令 | 自分のフレームを開始 |
| `movl $42, %eax` | 命令 | 戻り値を `%eax` にセット |
| `leave` | 命令 | フレームをたたむ |
| `ret` | 命令 | 呼び出し元に戻る |

`main` ラベルがないと、リンカが「`main` ってどこ？」となる。`.globl` がないと、リンカから見えない（同じファイルの中だけの私的な名前になる）。


## 8. codegen.c を読む

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;

/* Generate code for an expression — result goes into %eax */
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

/* Generate code for a statement */
static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        gen_expr(node->expr);
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        break;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}

/* Generate code for the entire program */
void codegen(Node *prog, FILE *output) {
    out = output;

    /* prog is a single function definition (for now) */
    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

3つの関数からなる。順に見ていく。

### `gen_expr` ── 式の値を `%eax` に置く

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}
```

式のコード生成。**約束は1つ：式の評価結果を `%eax` に入れること**。

第1章では式は整数リテラルしかない。`movl $42, %eax` のような命令を出すだけだ。

`fprintf(out, "  movl $%d, %%eax\n", node->int_val)` の `%%` は printf でリテラルの `%` を出すための書き方。アセンブリ側のレジスタ名 `%eax` をそのまま出力したいから、`%%eax` と書く。

第2章で式が複雑になると、`gen_expr` がぐっと大きくなる（`+` や `*` の処理が増える）。だが**約束は変わらない**：「式の値を `%eax` に置く」。

### `gen_stmt` ── 文をコードに変換する

```c
static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        gen_expr(node->expr);
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        break;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}
```

文のコード生成。

`NODE_RETURN` の場合：
1. **まず式を評価する**（`gen_expr(node->expr)`）── これで `%eax` に戻り値が入る。
2. その後 `leave` と `ret` を出す。

たった3行で「`return 42;` を return 文として正しく実装している」。式の評価という部分問題を `gen_expr` に委ね、自分は「戻る」処理を出すだけ。

`NODE_BLOCK` の場合：
中の文をリスト順に再帰的に処理するだけ。ブロック自体は何のコードも出さない。これは前の節で見た「AST と再帰」のパターンそのもの。

### `codegen` ── エントリポイント

```c
void codegen(Node *prog, FILE *output) {
    out = output;

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

`main.c` から呼ばれるエントリポイント。

1. 出力先 `FILE *` をモジュール内静的変数に保存（`gen_expr` や `gen_stmt` から使うため）。
2. ディレクティブとラベル（`.text`、`.globl main`、`main:`）を出す。
3. プロローグを出す。
4. 関数本体（ブロック）を `gen_stmt` で処理。

第1章ではプログラムは関数定義1つだけなので、この単純な作りで十分だ。第5章で複数の関数が登場すると、ここをループに変える。

ちなみに `prog->name` は文字列 `"main"`。bison のアクション `new_func_def($2, ...)` で `$2` から受け取った識別子の名前そのままだ。だから `main` 以外の関数名でも対応できる構造になっている。

### codegen.h

```c
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"

void codegen(Node *program, FILE *out);

#endif
```

## 9. 木の形と命令の順序が一致する

`int main() { return 42; }` の AST と、生成されるアセンブリを並べてみよう。

```
AST:                       アセンブリ:
                           
                           .text
                           .globl main
FUNC_DEF main              main:
                             pushq %rbp
                             movq %rsp, %rbp
  BLOCK
    RETURN
      INT_LIT 42             movl $42, %eax
                             leave
                             ret
```

- `FUNC_DEF` ノードからプロローグが出る
- `RETURN` ノード配下の `INT_LIT` から `movl` が出る
- `RETURN` ノード自体からエピローグが出る

AST を再帰で辿るとき、訪問順序が自然と命令の出力順序になる。「木の形」と「命令の順序」が対応するのは、コンパイラがシンプルに書ける根本の理由だ。


## 次へ

最後の節（`06_build.md`）では、これらを統合する `main.c` と `Makefile` を見て、コンパイラを完成させる。
