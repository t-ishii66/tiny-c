# 03 — bison 入門 と parser.y

字句解析器（lexer）はソースコードをトークンの列に変換した。次は**構文解析器**（parser）だ。

構文解析器の仕事は、トークンの列が「文法的に正しいか」を判定し、同時に**木のかたち**（AST）に組み立てることだ。

```
INT  IDENT(main)  '('  ')'  '{'  RETURN  INT_LIT(42)  ';'  '}'
                                  ↓ 構文解析器
                          FUNC_DEF main
                            BLOCK
                              RETURN
                                INT_LIT 42
```

これも自分で書くのは大変だ。文法がちょっと複雑になると、再帰下降パーサを手書きするのは骨が折れる。

そこで使うのが **bison** だ。bison は**構文解析器を自動生成するプログラム**で、文法規則とアクションを書いた定義ファイルから C のソースコードを生成してくれる。

```
parser.y  ─ bison ─→  parser.tab.c   （構文解析器の C コード）
                      parser.tab.h   （トークン定義のヘッダ）
```

flex とまったく同じ発想だ。我々は `parser.y` を書く。bison がそれを読んで C コードを吐く。あとは gcc でコンパイルするだけ。


## 1. 文法とは何か

構文解析を理解するには、まず**文法**の考え方を押さえる必要がある。

文法とは「**何が正しい構造か**」を記述したルールの集まりだ。例えば、自然言語の英語にも文法がある。

```
文 → 主語 動詞 目的語
主語 → "I" | "You" | "He" | ...
動詞 → "see" | "love" | ...
目的語 → "you" | "him" | ...
```

「`I see you` は文として正しい（主語 動詞 目的語の並び）」「`see you I` は正しくない」── こういう判定をするのが文法だ。

プログラミング言語の文法も同じ発想だ。

```
return文 → "return" 式 ";"
式      → 整数リテラル
```

「`return 42;` は return文として正しい」「`return ;` は正しくない（式が抜けている）」── このルールを書く。

bison ではこれを次のように書く。

```
ret_stmt
    : RETURN expr ';'
    ;

expr
    : INT_LIT
    ;
```

左辺の `ret_stmt` や `expr` は**非終端記号**（nonterminal）と呼ばれ、「他のものに展開される名前」を表す。
右辺の `RETURN`、`';'`、`INT_LIT` は**終端記号**（terminal、=トークン）で、字句解析器から実際に流れてくるもの。

「非終端記号は他のルールでさらに展開できる、終端記号はそれ以上分解できない」── この区別が大事だ。


## 2. 一番小さな例

bison の流儀でちょっとだけ大きな例を見てみる。「整数リテラルだけを式とするミニ言語」の構文解析器を考える。

```
program
    : stmt
    ;

stmt
    : RETURN expr ';'
    ;

expr
    : INT_LIT
    ;
```

これで「`return 整数;` だけを認める言語」の文法ができた。`program` から始まって `stmt` を経由して `expr` まで、ルールが連鎖して展開される。

bison は、トークン列がこのルールに従っているかを左から右に読みながら判定する。同時に、ルールにマッチしたタイミングでアクション（C のコード）を実行できる。アクションを使ってAST を組み立てるのだ。


## 3. アクションと `$1`, `$2`, `$$`

ルールの右辺の後ろに `{ ... }` でアクションを書く。

```
ret_stmt
    : RETURN expr ';'   { /* ここがアクション */ }
    ;
```

このアクションの中で、ルール右辺の各要素の値を **`$1`, `$2`, $3, ...`** で参照できる。

```
ret_stmt
    : RETURN expr ';'   { /* $1 = RETURN, $2 = expr の値, $3 = ';' */ }
    ;
```

そしてこのルール自体の結果を **`$$`** で表す。

```
expr
    : INT_LIT           { $$ = $1; }
    ;
```

これは「`expr` の結果は INT_LIT の値そのもの」という意味だ。

実際には `$$` には我々が**ノードを作って入れる**ことになる。

```
expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;
```

「`INT_LIT` の値（整数）を受け取って、`new_int_lit` で AST のノードを作って、それをこのルールの結果として返す」。これで `expr` は `Node *` を返すルールになった。

return 文ならこうなる：

```
ret_stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;
```

`$2` は expr ルールが返したノード（`new_int_lit` で作ったノード）。それを `new_return` で包んで、return 文ノードを作る。

**こうしてルールがマッチするたびに小さなノードが組み立てられ、それがさらに上のルールの引数になり、最終的に木全体が出来上がる**。木の葉から根に向かって組み上げていくイメージだ。


## 4. `%union`、`%token`、`%type` ── 値の型を教える

bison のルールは値（`$1`, `$$`）を扱うが、その値の型を bison に教える必要がある。「この `$2` は `Node *` ね」と。

そのための仕組みが3つある。

### `%union` で全部の型を列挙

```
%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}
```

これは「この parser で使う値の型は、整数か、文字列か、ノードか、ノードリストのいずれか」と宣言している。実際には共用体（union）が定義され、`yylval` の型もこれになる。

### `%token <field> NAME` でトークンの型を教える

```
%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN
```

- `INT_LIT` トークンの値は `int_val` フィールド（つまり `int`）
- `IDENT` トークンの値は `name` フィールド（つまり `char *`）
- `INT`、`RETURN` は値を持たない

flex 側で `yylval.int_val = ...` と書いていたのと辻褄が合う。

### `%type <field> RULE` で非終端記号の型を教える

```
%type <node> func_def stmt expr
%type <list> stmts
```

- `func_def`、`stmt`、`expr` ルールの結果は `node` フィールド（`Node *`）
- `stmts` ルールの結果は `list` フィールド（`NodeList *`）

これらの宣言があると、bison はアクション中の `$1` や `$$` を正しい型として扱ってくれる。


## 5. ch01 の parser.y 全体

第1章の `parser.y` の全体を見せる。

```c
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;  /* parser result */
%}

%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}

%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN

%type <node> func_def stmt expr
%type <list> stmts

%%

program
    : func_def          { program = $1; }
    ;

func_def
    : INT IDENT '(' ')' '{' stmts '}'
                        { $$ = new_func_def($2, new_block($6)); }
    ;

stmts
    : /* empty */       { $$ = NULL; }
    | stmts stmt        { $$ = new_node_list($2, $1); }
    ;

stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;

expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;

%%
```

42行。flex の lexer.l と同じく、`%%` で区画が分かれている。flex とほぼ同じ構造だ。


## 6. 1区画ずつ読む

### 第1区画（先頭の C コード）

```c
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;  /* parser result */
%}
```

- `#include "ast.h"` で AST のノード型と `new_xxx` コンストラクタを使えるようにする。
- `yylex` は flex が生成する関数（次のトークンを1つ返す）。`extern` 宣言で「別のファイルにある」と教える。
- `yyerror` は構文エラー時に bison が呼ぶ関数。我々が定義する。今はエラーメッセージを出して終了するだけ。
- `Node *program` は構文解析の結果を入れるグローバル変数。`main` 関数からアクセスする。

### 宣言区画（`%union`、`%token`、`%type`）

前のセクションで説明したとおり。型情報を bison に教える区画だ。

### 第2区画（文法ルール）

順に見ていく。

```
program
    : func_def          { program = $1; }
    ;
```

「**プログラムは関数定義1つから成る**」。`func_def` ルールが返したノード（`Node *`）をグローバル変数 `program` に保存する。これがコンパイラの最終結果だ。

`$$` を設定していないが、`program` ルールの結果は誰も使わないのでそれでよい。

```
func_def
    : INT IDENT '(' ')' '{' stmts '}'
                        { $$ = new_func_def($2, new_block($6)); }
    ;
```

「**関数定義は `int 名前 ( ) { 文の並び }` の形**」。

- `$1` は `INT`（値なし）
- `$2` は `IDENT` の値、つまり関数名の文字列（`char *`、たとえば `"main"`）
- `$6` は `stmts` の結果、つまり文のリスト（`NodeList *`）

`new_block($6)` で文のリストをブロックノードにくるみ、`new_func_def($2, ...)` で関数定義ノードにする。

```
stmts
    : /* empty */       { $$ = NULL; }
    | stmts stmt        { $$ = new_node_list($2, $1); }
    ;
```

「**文の並びは、空、または、文の並びの後ろに文1つ**」── これが**再帰**による「文の並び」の定義だ。

`|` は「または」の意味で、2つの選択肢を並べている。

- 空のとき：`$$` は NULL。
- 文の後にもう一つ文が続くとき：`new_node_list($2, $1)` で「新しい文を、それまでの並びの先頭にくっつける」。

ここで気をつける点：このルールは**左再帰**（`stmts : stmts stmt`）になっていて、リストは**逆順**に積まれる。`return 1; return 2;` という入力なら、できあがる NodeList の先頭は「return 2 のノード」だ。第1章では文が1つしかないので問題にならないが、後の章で文が並ぶようになると、必要に応じて反転させる処理が要る。

```
stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;
```

「**文は `return 式;` の形**」。`$2` は式のノード。`new_return` で return 文ノードを作る。今の文法では他に文の種類がないが、後の章で `if`、`while`、変数宣言、ブロック、代入式などが増えていく。

```
expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;
```

「**式は整数リテラルだけ**」。`$1` は INT_LIT トークンの値（`int`）。`new_int_lit` で整数リテラルノードを作る。

これが第1章の文法すべてだ。


## 7. パースを追ってみる

入力 `int main() { return 42; }` のトークン列がこの parser をどう通るか、追ってみよう。

トークン列：

```
INT  IDENT(main)  '('  ')'  '{'  RETURN  INT_LIT(42)  ';'  '}'
```

bison は左から右にトークンを読み込んで、ルールにマッチしたら**逆向きに**（葉から根へ）木を組み立てていく。これを「**ボトムアップ構文解析**」と呼ぶ。

順を追って見る。

1. **`INT_LIT(42)` を読む** → ルール `expr : INT_LIT` にマッチ。`new_int_lit(42)` でノードを作り、`expr` の結果（`$$`）にする。
   ```
   INT_LIT(42) ──→ INT_LIT(42) ノード
   ```

2. **`RETURN expr ';'` の3要素がそろった** → ルール `stmt : RETURN expr ';'` にマッチ。`new_return(...)` で return 文ノードを作る。
   ```
   RETURN ── INT_LIT(42)ノード ── ';' 
                  ↓
              RETURN ノード
              └─ INT_LIT(42) ノード
   ```

3. **`stmts stmt` がそろった**（`stmts` は最初は空、`stmt` が1個追加）→ ルール `stmts : stmts stmt` にマッチ。`new_node_list(stmt, NULL)` で文1個分のリストを作る。

4. **`INT IDENT '(' ')' '{' stmts '}'` の7要素がそろった** → ルール `func_def : ...` にマッチ。`new_block(stmts)` でブロックノードを作り、`new_func_def("main", block)` で関数定義ノードを作る。
   ```
              FUNC_DEF main
              └─ BLOCK
                  └─ RETURN
                      └─ INT_LIT 42
   ```

5. **`func_def` が完成した** → ルール `program : func_def` にマッチ。グローバル変数 `program` に代入。

これで AST が完成し、`program` から木全体にアクセスできる。

これを `--dump-ast` で表示するとさっき見たあの形になる：

```
FUNC_DEF main
  BLOCK
    RETURN
      INT_LIT 42
```

ボトムアップ解析の利点は、ルールごとのアクションが「子のノードがすでに揃っている状態」で実行されることだ。`stmt : RETURN expr ';'` が動くときには、`expr` は既に評価済みでノードができている。だから `new_return($2)` が自然に書ける。


## 8. bison が生成するコード

`parser.y` を bison に通すと、2つのファイルが生成される。

```bash
$ bison -d -o parser.tab.c parser.y
```

- **`parser.tab.c`**: 構文解析器本体の C ソース。`yyparse()` という関数を含む。
- **`parser.tab.h`**: トークン名（INT, RETURN, INT_LIT, ...）の `#define` と、`yylval` の型定義（`%union` から生成）が入ったヘッダ。flex の `lexer.l` でこれを include することで、トークン名と `yylval` の型を共有できる。

`-d` オプションがヘッダ生成のキーだ。これを忘れると `parser.tab.h` が生成されず、flex 側でビルドエラーになる。

`yyparse()` を呼ぶと、flex の `yylex()` を内部で呼びながらトークンを消費し、ルールにマッチするたびにアクションを実行する。最後にグローバル変数 `program` に AST が出来上がっている。


## 9. flex と bison の連携

ここまでで、flex と bison の役割と連携が見えたはず。

```
+-----------+
| 入力ファイル |
+-----+-----+
      |
      v
+-----------+
|  yyin     |   FILE * 型のグローバル変数
+-----+-----+
      |
      v
+-----------+
|  yylex()  |   flex 生成。1回呼ぶと1トークン返す
+-----+-----+
      |
      v
+-----------+
|  yyparse()|   bison 生成。yylex を内部で呼びながら木を作る
+-----+-----+
      |
      v
+-----------+
|  program  |   グローバル変数。AST の根が入る
+-----------+
```

`main.c` でやることはたった3つ：

1. `yyin` に入力ファイルをセット
2. `yyparse()` を呼ぶ
3. `program` から AST を取り出して使う


## 10. まとめ

- **bison は構文解析器を自動生成するツール**。`.y` ファイルを書くと、`.c` と `.h` が出てくる。
- **文法ルール**は「左辺 : 右辺」の形。右辺は終端記号（トークン）と非終端記号（他のルール）の並び。
- **アクション**で AST を組み立てる。`$1, $2, ...` でルール右辺の値を、`$$` で結果を表す。
- **`%union`、`%token <field>`、`%type <field>`** で値の型を bison に教える。
- bison は**ボトムアップ**で解析する。子のノードが揃ってから親のアクションが動く。
- 第1章の parser.y は42行。`int main() { return ...; }` の形を木に変換できる。


## 次へ

トークンを木にするところまで来た。だがまだ、木そのものの実装（`ast.h` と `ast.c`）について深く触れていない。次のサブ章（04_ast.md）では、AST のデータ構造を設計の意図から見ていく。
