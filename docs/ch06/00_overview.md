# 第6章 — ポインタと配列

## 0. はじめに

第6章で **ポインタ**、**配列**、**文字列リテラル**、**グローバル変数** を導入する。これで tiny-c は `printf` 経由で「Hello, world」が書ける言語になる。

```c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}
```

`*p` と `&x`、`a[i]`、`'a'`、`"hello"`、`int g;` ── ここまで来ると、見慣れた C のかなりの部分が動く。

## 1. ch05 との差分

| ファイル | 変更 |
|---------|------|
| `lexer.l` | `&` `[` `]` 記号、`char` `void` キーワード、文字リテラル `'...'`、文字列リテラル `"..."` を追加 |
| `parser.y` | `type` 規則（`int` `char` `int*` `char*` `void`）、トップレベルにグローバル変数宣言、添字 `a[i]`、単項 `&` `*`、文字/文字列リテラル |
| `ast.h` / `ast.c` | `Type` 構造体、`NODE_CHAR_LIT` `NODE_STRING_LIT` `NODE_GLOBAL_VAR_DECL` `NODE_INDEX`、コンストラクタ拡張 |
| `codegen.c` | **`gen_addr` 関数の導入**、シンボルテーブルが `Type` を保持、グローバル変数の `.bss` 出力、文字列リテラルの `.rodata` 出力、`movb`/`movsbl`/`movq` のサイズ別 load/store |
| `main.c` / `Makefile` | 変更なし |

新しい中心テーマは **「lvalue と rvalue の二面性」**。

`int x = 0; x = 5; ... return x;` を眺めると、`x` という同じ名前が **書き込み先（場所）** にも **読み取り元（値）** にも使われている。コンパイラ内部ではこれを別の道具で計算する:

- **rvalue** = 値を計算する → `gen_expr` （これまで使ってきた）
- **lvalue** = アドレスを計算する → **`gen_addr`** （ch06 で初登場）

ポインタの `&` と `*` は、この対称性そのものを操作する演算子だ。

- `&x` = `gen_addr(x)`（`x` のアドレスを取る）
- `*p` = `gen_expr(p)` で得たアドレスから値をロード

これが噛み合うように `gen_addr` と `gen_expr` を別々に書く ── それが ch06 のコード生成の骨格になる。

## 2. まず動かしてみる

### Hello, world

```bash
$ cat hello.c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}
$ ./tinyc hello.c > hello.s && gcc -o hello hello.s && ./hello
hello
```

### ポインタで値を書き換える

```bash
$ cat ptr.c
int main() {
    int x = 5;
    int *p = &x;
    *p = 10;
    return x;       // 10
}
```

### 配列

```bash
$ cat arr.c
int main() {
    int a[5];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    return a[0] + a[1] + a[2];   // 60
}
```

### グローバル変数

```bash
$ cat g.c
int g;

int set(int v) { g = v; return 0; }

int main() {
    set(42);
    return g;       // 42
}
```

### 文字列処理（自前 strlen）

```bash
$ cat strlen.c
int strlen(char *s) {
    int n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

int main() {
    char *s = "hello";
    return strlen(s);   // 5
}
```

`s[n]` は char 単位で 1 バイトずつ読み出す（ポインタ算術が型に依存することを内部でちゃんと処理している）。

## 3. AST の新しい形

入力:

```c
int main() {
    int x = 5;
    int *p = &x;
    *p = 10;
    return x;
}
```

AST:

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 5
      VAR_DECL p : int*
        UNARY &
          IDENT x
      EXPR_STMT
        ASSIGN
          UNARY *
            IDENT p
          INT_LIT 10
      RETURN
        IDENT x
```

ノード型に `Type` 情報が乗るようになった（`x : int`、`p : int*`）。`UNARY &` と `UNARY *` がポインタ操作の AST 表現。`*p = 10` の AST では `ASSIGN` の左に `UNARY *` が立つ ── 「lvalue としての `*p`」だ。

## 4. サブ章の構成

| サブ章 | 内容 |
|--------|------|
| 00（この文書）| 章の見取り図 |
| 01 | 型と文法の追加 — `Type` 構造体、`int*`/`char*` 型、添字 `[]`、`&` `*` 単項、char/string リテラル、グローバル宣言 |
| 02 | lvalue と rvalue — `gen_addr` 関数、`&` と `*` の対称性、配列の decay |
| 03 | グローバル変数と文字列リテラル — `.bss` と `.rodata`、サイズ別の load/store |
| 04 | 全ファイルの完全形と差分、Hello, world デモ |

## 5. 次へ

サブ章 01（`01_types.md`）では、tiny-c に **型の概念** を初めて入れる。これまで全変数が暗黙に `int` だったが、ch06 からは `char`、`int*`、`char*`、`int[N]` を区別する必要がある。Type 構造体と関連する文法・AST 変更を見る。
