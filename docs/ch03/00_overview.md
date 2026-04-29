# 第3章 — 名前をつける

## 0. はじめに

前章までで、整数の式が計算できるようになった。電卓としては動くが、書けるのは1行きりの式だけ。何度も使う値を取り出すこともできない。

第3章では **変数** を導入する。

```c
int main() {
    int x = 1;
    int y = 2;
    return x + y;
}
```

`x`、`y` という名前で値に紐づけ、後でその名前で取り出せる。これだけでプログラムらしくなる。

代入もできるようになる。

```c
int main() {
    int x = 10;
    x = x + 5;
    return x;        // 15
}
```

## 1. ch02 との差分

| ファイル | 変更 |
|---------|------|
| `lexer.l` | `=` トークン追加（1行） |
| `parser.y` | `var_decl`, `expr_stmt`, `assign`, `IDENT` を expr に追加。`stmts` を**右再帰**に |
| `ast.h` / `ast.c` | `NODE_IDENT`, `NODE_ASSIGN`, `NODE_VAR_DECL`, `NODE_EXPR_STMT` を追加 |
| `codegen.c` | **シンボルテーブル**（名前 → スタックオフセット）を導入。新ノード4つの生成を追加 |
| `main.c` / `Makefile` | 変更なし |

新しい中心テーマは **「名前をスタック上のアドレスに変換する」** ── これだけ。

変数は実際にはメモリ上の場所だ。`x` という名前は人間のためのラベルで、CPU が見るのはアドレスだけ。コンパイラの仕事は、**名前 → アドレス** の変換表（シンボルテーブル）を作ることだ。

## 2. まず動かしてみる

```bash
$ cat test.c
int main() {
    int x = 1;
    int y = 2;
    return x + y;
}

$ ./tinyc --dump-ast test.c
FUNC_DEF main
  BLOCK
    VAR_DECL x
      INT_LIT 1
    VAR_DECL y
      INT_LIT 2
    RETURN
      BINARY +
        IDENT x
        IDENT y
```

3つの statement が順に並ぶ AST。`VAR_DECL` ノードは「変数を作る」、`IDENT` ノードは「変数を読む」。

```bash
$ ./tinyc test.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $8, %rsp                  # x のための領域
  movl $1, %eax
  movl %eax, -8(%rbp)            # x = 1
  subq $8, %rsp                  # y のための領域
  movl $2, %eax
  movl %eax, -16(%rbp)           # y = 2
  movl -16(%rbp), %eax           # eax = y
  pushq %rax
  movl -8(%rbp), %eax            # eax = x
  popq %rcx
  addl %ecx, %eax                # eax = x + y
  leave
  ret

$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
3
```

注目すべきは `-8(%rbp)`、`-16(%rbp)` という記法。これが「`%rbp` から見て -8 バイトの場所」「-16 バイトの場所」── つまり **スタック上の番地** を指している。`x` と `y` は、それぞれその番地に住んでいる。

この番地と名前のマッピングを、コンパイラがどう作るか ── それがこの章の中身だ。

## 3. サブ章の構成

| サブ章 | 内容 |
|--------|------|
| 00（この文書）| 章の見取り図 |
| 01 | 文法の追加分 — `var_decl`、`assign`、`IDENT` を式に。stmts の右再帰化 |
| 02 | スタックフレームとシンボルテーブル — 名前をオフセットに変える |
| 03 | 全ファイルの完全形と差分、ビルドして動かす |

## 4. 次へ

サブ章 01（`01_grammar.md`）では、文法に何を足すかを見る。`int x = expr;` の文、`x = expr` の代入式、識別子を式として使うルール。それから、stmts を左再帰から右再帰に変えた理由 ── 複数文を扱い始めると顕在化する小さな落とし穴 ── を片付ける。
