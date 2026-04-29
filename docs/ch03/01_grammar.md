# 01 — 文法の追加分

`parser.y` への変更を見ていく。やることは4つ。

1. **新しい文** を加える（変数宣言、式文）。
2. **代入式** を式の階層に追加する（最も低い優先順位）。
3. **識別子** を `primary` に追加する。
4. `stmts` を **右再帰** に書き換える（複数文の順序を保つため）。

## 1. 新しい文

ch02 までは `stmt` は `RETURN expr ';'` の1種類だけだった。ここに2種類足す。

```yacc
stmt
    : RETURN expr ';'              { $$ = new_return($2); }
    | INT IDENT '=' expr ';'       { $$ = new_var_decl($2, $4); }
    | expr ';'                     { $$ = new_expr_stmt($1); }
    ;
```

- **`INT IDENT '=' expr ';'`** が変数宣言。`int x = 1;` のかたち。tiny-c では **初期化子は必須** なので、`int x;` という宣言は文法的に書けない（CLAUDE.md 参照）。
- **`expr ';'`** は式文。式を実行して値を捨てる。これがあるから `x = x + 5;` のような代入を「文」として書ける。

`new_var_decl` と `new_expr_stmt` は ast.c に新しく加えるコンストラクタ。それぞれ `NODE_VAR_DECL`、`NODE_EXPR_STMT` のノードを作る。

`var_decl` の文法には `INT` トークンが入っているのに注意。型名は今のところ `int` 一種類だけ。ch06 で `char` が来たら、ここを `type` 規則に置き換えることになる。

### `INT IDENT` と `IDENT` は曖昧にならない？

`int` で始まる行と、IDENT（変数名）で始まる行は、bison が先頭の1トークンを見れば区別できる。`INT` トークンはキーワード（lexer で `"int"` を最優先でマッチ）なので、IDENT になることはない。だから両ルールは衝突しない。

## 2. 代入式

代入は、Cでは「優先順位が最も低い式」で「右結合」だ。`a = b = 7` は `a = (b = 7)` と解釈される。これを文法で表現する。

```yacc
expr
    : assign                       { $$ = $1; }
    ;

assign
    : add_expr                     { $$ = $1; }
    | add_expr '=' assign          { $$ = new_assign($1, $3); }
    ;
```

ポイントは `assign : add_expr '=' assign` の右側に **再び `assign`** が来ていること。これが右結合の表現だ（左に `assign` を置けば左結合になる ── ch02 の `add_expr` の章で見たやり方の鏡像）。

`expr` は `assign` を1段挟んで取り込んでいる。`expr → assign → add_expr → ...` という階層。新しい層が入ったが、構造は今までと同じ ── 各階層は1つの優先順位に対応する。

### lvalue について

C では `=` の左に書けるものは限られる ── 変数、配列要素、`*p` 形式の間接参照など。これを **左辺値 (lvalue)** と呼ぶ。`5 = 3` のように整数リテラルを左に書くのは不正。

文法上は `add_expr '=' assign` と書いてあるので、bison は左に何でも受け入れてしまう。lvalue かどうかは **codegen 時にチェック** する（次のサブ章で）。文法レベルで lvalue を区別すると規則が爆発するので、エラー検出だけ後ろに回すのが定石だ。

ch03 では lvalue として認めるのは **`IDENT` のみ**。配列の `[]` や間接参照 `*` は ch06 で扱う。

## 3. 識別子を式に追加

`x + y` の `x` や `y` は、式として登場する識別子だ。`primary` に1行足す。

```yacc
primary
    : INT_LIT                      { $$ = new_int_lit($1); }
    | IDENT                        { $$ = new_ident($1); }
    | '(' expr ')'                 { $$ = $2; }
    ;
```

`new_ident` は `NODE_IDENT` ノードを作る。中身は変数名の文字列だけ。実際のスタックオフセットは codegen 時に解決する。

つまり、AST はまだ「名前」のレベル。`-8(%rbp)` のような物理的な場所は **codegen が決める**。フロントエンド（lexer/parser/AST）とバックエンド（codegen）の責任分担が、ここで明確になる。

## 4. stmts を右再帰に

ch01・ch02 では `stmts` の規則はこうだった。

```yacc
stmts
    : /* empty */       { $$ = NULL; }
    | stmts stmt        { $$ = new_node_list($2, $1); }
    ;
```

これは bison の流儀でいう **左再帰**。ch01・ch02 では関数本体に1文しかなかったので問題は出なかったが、複数文になると順番が問題になる。

`new_node_list(stmt, prev_list)` は「先頭に追加」する操作。だから順々に文を足していくと、リストは **逆順** に組み上がる。

| 入力 | 還元順 | 構築されるリスト |
|------|-------|--------------|
| s1 | stmts(NULL) → s1 を足す | [s1] |
| s1 s2 | [s1] → s2 を足す | [s2, s1]   ← 逆 |
| s1 s2 s3 | [s2, s1] → s3 を足す | [s3, s2, s1]   ← 逆 |

ch03 では codegen が前から後ろへ文を実行するので、この逆順は致命的だ。

修正は2通りある。

- (a) リストを最後に逆転する関数を書く。
- (b) `stmts` を右再帰にする。

ch03 では (b) を選ぶ。書き換えるとこうなる。

```yacc
stmts
    : /* empty */       { $$ = NULL; }
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;
```

「文 stmts → リストの先頭にこの文、末尾に残り」── これで自然に順序が保たれる。

| 入力 | 還元順（bison は内側から） | 構築されるリスト |
|------|------------------------|--------------|
| s1 s2 s3 | 内側 stmts(empty)、 s3 stmts(empty) → [s3]、 s2 [s3] → [s2, s3]、 s1 [s2, s3] → [s1, s2, s3] | [s1, s2, s3] ✓ |

bison は普通 **左再帰のほうがメモリ効率がいい**（パース中に内部スタックが浅く済む）。だが我々の小さなプログラムでは、右再帰の差は実用上ほぼゼロ。**順序の自然さを優先する**。これは教育用コンパイラとしての判断だ。

実プロダクションのコンパイラだと、左再帰のまま `reverse_list` を書いて使うことが多い。tiny-c では右再帰のシンプルさを取った。

## 5. ch03 の parser.y の全体像

差分を反映した全体はこうなる（`%union` 等のヘッダ部は省略）。

```yacc
program
    : func_def

func_def
    : INT IDENT '(' ')' '{' stmts '}'

stmts
    : /* empty */
    | stmt stmts                          # 右再帰

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

## 6. AST の形

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

## 7. まとめ

- 文に2種類追加（`var_decl`、`expr_stmt`）。
- 式に1階層追加（`assign`）── 右結合、優先順位は最低。
- `primary` に `IDENT` を追加 ── 変数を読む式。
- `stmts` を **右再帰** に書き換え、複数文の順序を保つ。
- lvalue チェックは codegen に任せる（`IDENT` 以外を `=` の左に置くとエラー）。

## 次へ

文法は固まった。AST の形も決まった。次は codegen ── 名前 `x` `y` をどう **`-8(%rbp)`** `-16(%rbp)` のような番地に変換するか。シンボルテーブルが登場する。
