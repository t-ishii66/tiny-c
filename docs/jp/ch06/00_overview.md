![](../../../images/img-8.png)

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

C のあらゆる式は **lvalue** か **rvalue** のどちらかに分類される:

- **lvalue（左辺値）** ── **メモリ上の場所** を表す式。アドレスを持ち、書き込みもできる。`x`、`*p`、`a[i]`、`g`（グローバル）など。
- **rvalue（右辺値）** ── **値** を表す式。アドレスを持たず、読み出すだけ。`42`、`x + 1`、`f()`、`'a'` など。

名前は歴史的に「`=` の左に書けるか / 右にしか書けないか」から来ているが、現代では **アドレスを持つかどうか** が本質。具体例:

```c
x = 5;          /* x は lvalue（書き込み先）、5 は rvalue */
y = x + 1;      /* y は lvalue、x は rvalue 文脈で使われている */
*p = 10;        /* *p は lvalue（p が指す場所） */
int *p = &x;   /* &x で x の lvalue を rvalue（アドレス値）に変換し、p に代入 */
&(x + 1);       /* エラー: x + 1 は rvalue なのでアドレスを取れない */
42 = x;         /* エラー: 42 は rvalue なので代入先になれない */
```

同じ `x` でも、`x = 5;` では lvalue（書き込み先）、`y = x + 1;` では rvalue（値の読み出し） ── **文脈で扱いが変わる**。

そこで ch06 では codegen を 2 つの関数に分ける:

- **`gen_expr`** ── rvalue を計算（値を `%eax`/`%rax` に置く）。ch05 までの方針そのまま。
- **`gen_addr`** ── lvalue のアドレスを計算（アドレスを `%rax` に置く）。ch06 で初登場。

ポインタの `&` と `*` は lvalue / rvalue を行き来する演算子だ:

- `&x` ── x の lvalue（アドレス）を rvalue（値）として取り出す → `gen_addr(x)` を呼ぶ
- `*p` ── p の rvalue（p に格納されたアドレス）が指す場所 → そのアドレスから load 処理を足せば値が取れる

`&` は load(アドレスからデータを取り込む処理) を 1 段取り去り、`*` は load を 1 段足す ── 対称的な操作。これが ch06 のコード生成の骨格になる。

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

## 4. 節の構成

| 節 | 内容 |
|--------|------|
| 00（この文書）| 章の見取り図 |
| 01 | 型と文法の追加 — `Type` 構造体、`int*`/`char*` 型、添字 `[]`、`&` `*` 単項、char/string リテラル、グローバル宣言 |
| 02 | lvalue と rvalue — `gen_addr` 関数、`&` と `*` の対称性、配列の decay |
| 03 | グローバル変数と文字列リテラル — `.bss` と `.rodata`、サイズ別の load/store |
| 04 | 全ファイルの完全形と差分、Hello, world デモ |

## 5. 次へ

節 01（`01_types.md`）では、tiny-c に **型の概念** を初めて入れる。これまで全変数が暗黙に `int` だったが、ch06 からは `char`、`int*`、`char*`、`int[N]` を区別する必要がある。Type 構造体と関連する文法・AST 変更を見る。
