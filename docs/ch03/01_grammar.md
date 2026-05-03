# 01 — 文法の追加分

`parser.y` への変更を見ていく。やることは3つ。

1. **新しい文** を加える（変数宣言、式文）。
2. **代入式** を式の階層に追加する（最も低い優先順位）。
3. **識別子** を `primary` に追加する。

## 1. 新しい文

ch02 までは `stmt` は `RETURN expr ';'` の1種類だけだった。ここに2種類足す。

```yacc
stmt
    : RETURN expr ';'              { $$ = new_return($2); }
    | INT IDENT '=' expr ';'       { $$ = new_var_decl($2, $4); }    /* 追加 */
    | expr ';'                     { $$ = new_expr_stmt($1); }       /* 追加 */
    ;
```

- **`INT IDENT '=' expr ';'`** が変数宣言。`int x = 1;` のかたち。tiny-c では **単純化のため初期化子は必須** で、`int x;` という宣言は文法的に書けない。
- **`expr ';'`** は式文。式を実行して値を捨てる。これがあるから `x = x + 5;` のような代入を「文」として書ける。

`new_var_decl` と `new_expr_stmt` は ast.c に新しく加えるコンストラクタ。それぞれ `NODE_VAR_DECL`、`NODE_EXPR_STMT` のノードを作る。

### `INT IDENT` と `IDENT` は曖昧にならない？

`int` で始まる行と、IDENT（変数名）で始まる行は、bison が先頭の1トークンを見れば区別できる。`INT` トークンはキーワード（lexer で `"int"` を最優先でマッチ）なので、IDENT になることはない。だから両ルールは衝突しない。

## 2. 代入式

代入は、Cでは「優先順位が最も低い式」で「右結合」だ。`a = b = 7` は `a = (b = 7)` と解釈される。これを文法で表現する。

```yacc
expr
    : assign                       { $$ = $1; }                  /* 変更: ch02 は : add_expr */
    ;

assign                                                           /* 追加 */
    : add_expr                     { $$ = $1; }                  /* 追加 */
    | add_expr '=' assign          { $$ = new_assign($1, $3); }  /* 追加 */
    ;                                                            /* 追加 */
```

`assign : add_expr '=' assign` の右側に **再び `assign`** が来ているのが右結合の表現（左に `assign` を置けば左結合になる）。`expr` は `assign` を1段挟んで取り込み、`expr → assign → add_expr → ...` という階層になる。

### lvalue について

C では `=` の左に書けるものは限られる ── 変数、配列要素、`*p` 形式の間接参照など。これを **左辺値 (lvalue)** と呼ぶ。`5 = 3` のように整数リテラルを左に書くのは不正。

文法上は `add_expr '=' assign` と書いてあるので、bison は左に何でも受け入れてしまう。lvalue かどうかは **codegen 時にチェック** する（次のサブ章で）。文法レベルで lvalue を区別すると規則が爆発するので、エラー検出だけ後ろに回すのが定石だ。

ch03 では lvalue として認めるのは **`IDENT` のみ**。配列の `[]` や間接参照 `*` は ch06 で扱う。

## 3. 識別子を式に追加

`x + y` の `x` や `y` は、式として登場する識別子だ。`primary` に1行足す。

```yacc
primary
    : INT_LIT                      { $$ = new_int_lit($1); }
    | IDENT                        { $$ = new_ident($1); }       /* 追加 */
    | '(' expr ')'                 { $$ = $2; }
    ;
```

`new_ident` は `NODE_IDENT` ノードを作る。中身は変数名の文字列だけ。**`-8(%rbp)` のような物理的な場所は codegen が決める** ── AST は「名前」のレベルで止めておく。

## 4. ch03 の parser.y の全体像

差分を反映した全体はこうなる（`%union` 等のヘッダ部は省略）。

```yacc
program
    : func_def

func_def
    : INT IDENT '(' ')' '{' stmts '}'

stmts
    : /* empty */
    | stmt stmts

stmt
    : RETURN expr ';'
    | INT IDENT '=' expr ';'              # 追加: 変数宣言
    | expr ';'                            # 追加: 式文

expr
    : assign

assign                                    # 追加: 代入の階層
    : add_expr
    | add_expr '=' assign                 # 右結合

add_expr : ...   (ch02 と同じ)
mul_expr : ...
unary    : ...

primary
    : INT_LIT
    | IDENT                               # 追加: 識別子
    | '(' expr ')'
```

bison の **`-d` オプションで競合警告は出ない** はず。代入式の `add_expr '=' assign` は一見 shift/reduce 競合になりそうだが、`assign` の FOLLOW 集合に `=` が含まれない（`;` か `)` しか続かない）ので、bison は LALR(1) で正しく解決できる。

## 5. AST の形

入力:

```c
int main() {
    int x = 10;
    x = x + 5;
    return x;
}
```

AST:

```
FUNC_DEF main
  BLOCK
    VAR_DECL x          ← int x = 10;
      INT_LIT 10
    EXPR_STMT           ← x = x + 5;
      ASSIGN
        IDENT x
        BINARY +
          IDENT x
          INT_LIT 5
    RETURN              ← return x;
      IDENT x
```

文が3つ並び、それぞれが新しい種類のノード（`VAR_DECL`、`EXPR_STMT` を含む `ASSIGN`、`RETURN`）を持つ。`IDENT` も初登場で、変数名の文字列だけを抱えている。

## 次へ

次は codegen ── 名前 `x` `y` を **`-8(%rbp)`** などの番地に変換する。シンボルテーブルが登場する。
