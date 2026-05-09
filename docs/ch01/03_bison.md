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

そこで使うのが **bison** だ。flex と同じ発想で、文法規則とアクションを書いた定義ファイルから C のソースコードを生成してくれる。

```
parser.y  ─ bison ─→  parser.tab.c   （構文解析器の C コード）
                      parser.tab.h   （トークン定義のヘッダ）
```


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

「`return 42;` は return文として正しい」「`return ;` は tiny-c では正しくない（式が抜けている）」── このルールを書く。

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

3つのルールが並んでいる。1つずつ言葉に直すとこうなる。

- **「`program` は `stmt` で構成される」** ── `program` は開始記号で、ソースコード全体を `program` にマッチさせる。プログラムというのは、つまるところ文 (stmt) ひとつだ、と言っている。
- **「`stmt` は `RETURN` トークン、`expr`、`;` トークンの並びで構成される」** ── 文の形は「`return`、何らかの式、セミコロン」の3つがこの順に並んだものだ、と決めている。
- **「`expr` は `INT_LIT` トークンで構成される」** ── 式は整数リテラル1個きりだ、と言っている。

`:` の左にあるのが「定義する名前」、右にあるのが「その名前の中身（並び）」。

ルールはお互いを参照し合える。`stmt` の定義の中に `expr` が登場して、`expr` の定義はまた別のルールとして書かれている。こうやって名前を介してルールを連鎖させることで、入れ子になった文法構造が表現できる。

`program` が **開始記号 (start symbol)** ── 文法の入り口になる名前。bison はファイルの先頭にあるルールの左辺を開始記号と見なす。「プログラム全体は `program` から展開していけば説明できる」という宣言だ。

これで「`return 整数;` だけを認める言語」の文法ができた。トークン列 `RETURN INT_LIT ';'` が来たら、`expr → INT_LIT`、`stmt → RETURN expr ';'`、`program → stmt` の順にルールが当てはまり、最終的に `program` まで戻れる。bison はこの「戻れるかどうか」を機械的に判定する。

bison は、トークン列がこのルールに従っているかを左から右に読みながら判定する。同時に、ルールにマッチしたタイミングでアクション（C のコード）を実行できる。アクションを使ってAST を組み立てるのだ。


## 3. アクションと `$1`, `$2`, `$$`

ルールの右辺の後ろに `{ ... }` でアクションを書く。

```
ret_stmt
    : RETURN expr ';'   { /* ここがアクション */ }
    ;
```

ここが bison を使うことのキモだ。**ルールが実際にマッチしたとき（= 入力のトークン列がそのルールに当てはまったとき）、対応するアクションの C コードが実行される**。たとえば上の規則で言えば、「`RETURN`、`expr`、`;`」という並びが認識された瞬間に、`{ ... }` の中身が動く。

文法ルールは「形を定義するもの」、アクションは「形が見つかったときに何をするか」── この2つがセットになっている。bison は形のチェックを自動でやってくれて、我々は **「形ができたときに何をするか」だけ** を書けばよい。アクションでは AST のノードを作って繋いでいく。実際のルールはこうなる。

```
expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;

ret_stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;
```

アクションが扱う「値」には2種類ある。

- **`$1`, `$2`, `$3`, ...** ── ルール右辺の各要素の値。数字は **位置** を表す（1番目、2番目、3番目）。
- **`$$`** ── 左辺の非終端記号（`expr`, `ret_stmt` など）自身の値。アクションで `$$ = ...` と決めた値は、上位のルールがこの非終端記号を参照するとき（`$2` などとして）に取り出される。

`expr` ルール: 右辺は `INT_LIT` 1個だけ。`$1` はその `INT_LIT` の値（整数 `42`）。`new_int_lit` に渡してリーフノードを作り、`$$` に入れる。これで `expr` は `Node *` を返すルールになる。

ここで「`$1` の値が `42`」というのが少しややこしい。flex 側で `[0-9]+ { yylval.int_val = atoi(yytext); return INT_LIT; }` と書いた。`return INT_LIT;` の `INT_LIT` は内部的にはトークン番号（たとえば 260 という整数）に展開され、それが bison に「次のトークンは何か」を伝える。**`$1` に入るのはこの 260 ではなく、その手前で `yylval.int_val` に入れた値（つまり `42`）の方だ。** 「トークンの値」と「トークンの種類を表す番号」は別物 ── どうやって `$1` の型と中身が決まるか、詳しい仕掛けは次節（`%union` / `%token`）で見る。

`ret_stmt` ルール: 右辺は `RETURN`, `expr`, `;` の3要素で `$1` `$2` `$3` がそれぞれに対応する。`$2` は `expr` ── 上のルール `expr: INT_LIT {$$=new_int_lit($1);};`が `$$` に入れたノード。それを `new_return` で包んで return 文ノードを作る。

ルールがマッチするたびに小さなノードが組み立てられ、それがさらに上のルールの引数になり、最終的に木全体ができあがる。木の葉から根に向かって組み上げていくイメージだ。


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

`%token <int_val> INT_LIT` は「`INT_LIT` トークンの値は `yylval.int_val` から取ってくる」と bison に教える宣言だ。flex 側で `yylval.int_val = atoi(yytext);` と書いていたのと辻褄が合う。

### `%type <field> RULE` で非終端記号の型を教える

```
%type <node> func_def stmt expr
%type <list> stmts
```

- `func_def`、`stmt`、`expr` ルールの結果は `node` フィールド（`Node *`）
- `stmts` ルールの結果は `list` フィールド（`NodeList *`）

これらの宣言があると、bison はアクション中の `$1` や `$$` を正しい型として扱ってくれる。


## 5. ch01 の parser.y 全体

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
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;

stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;

expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;

%%
```

42行。`%%` で区画が分かれているのは flex と同じ。


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
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;
```

「**文の並びは、空、または、文1つの後ろに文の並び**」── これが**再帰**による「文の並び」の定義だ。

`|` は「または」の意味で、2つの選択肢を並べている。

- 空のとき: `$$` は NULL。
- 文の後にもう一つ「文の並び」が続くとき: `new_node_list($1, $2)` で「この文を先頭、残りの並びを後ろ」というリストを作る。

例えば文stmtがちょうど1つだけの場合、最初に `stmts` が空ルールにマッチして `NULL` になり、次に `new_node_list($1, NULL)` ── 「この文だけのリストstmts」── が作られる。

> **補足: 右再帰と左再帰**
>
> 上の規則は **右再帰** (`stmts : stmt stmts`) と呼ばれる ── 規則の右辺の右側に再帰がある形だ。`stmts : stmts stmt` のように左側に再帰を置く **左再帰** という書き方もあり、bison ではメモリ効率の点で左再帰のほうが好まれる（パース中に内部スタックが浅く済む）。だが左再帰だとリストが**逆順** に組み上がってしまうため、後で反転処理が要る。tiny-c では小さなプログラムしか扱わないのでメモリ効率の差は実用上ゼロ。コードの単純さを取って **右再帰** を採用する。

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

3. **`stmt stmts` がそろった**（`stmt` が1個、後ろの `stmts` は空）→ ルール `stmts : stmt stmts` にマッチ。`new_node_list(stmt, NULL)` で文1個分のリストを作る。

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

```
+-----------+
| 入力ファイル |   tiny-c で書かれたソース (例: `int main() { return 42; }`)
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


## 次へ

次の節（`04_ast.md`）では、ここで作った木そのものの実装（`ast.h` と `ast.c`）を設計の意図から見ていく。
