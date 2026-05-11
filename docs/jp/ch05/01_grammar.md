# 01 — 文法と AST

プログラムを関数の列にし、関数が引数を取れるようにする。

## 1. プログラムを関数の列に

ch04 までの `program : func_def ;` を「関数の列」に変える。

```yacc
program
    : func_defs                  { program = new_program($1); }   /* 変更: ch04 は : func_def */
    ;

func_defs                                                         /* 追加 */
    : /* empty */                { $$ = NULL; }                   /* 追加 */
    | func_def func_defs         { $$ = new_node_list($1, $2); }  /* 追加 */
    ;                                                             /* 追加 */
```

`func_defs` は右再帰で、関数の連続を `NodeList` として組み上げる。空でも OK（理論上は関数のないプログラム）。`new_program` は新しいコンストラクタで、`NODE_PROGRAM` ノードを作る。

`Node *program` というグローバル変数は引き続き「パース結果のルート」だが、その中身が `NODE_FUNC_DEF` から `NODE_PROGRAM` に変わる。`NODE_PROGRAM.stmts` が関数定義のリスト。

## 2. パラメータ付き関数定義

```yacc
func_def
    : INT IDENT '(' params ')' '{' stmts '}'                        /* 変更: params 追加 */
                                 { $$ = new_func_def($2, $4, new_block($7)); }  /* 変更: $4 追加 */
    ;

params                                                              /* 追加 */
    : /* empty */                { $$ = NULL; }                     /* 追加 */
    | param_list                 { $$ = $1; }                       /* 追加 */
    ;                                                               /* 追加 */

param_list                                                          /* 追加 */
    : param                      { $$ = new_node_list($1, NULL); }  /* 追加 */
    | param ',' param_list       { $$ = new_node_list($1, $3); }    /* 追加 */
    ;                                                               /* 追加 */

param                                                               /* 追加 */
    : INT IDENT                  { $$ = new_ident($2); }            /* 追加 */
    ;                                                               /* 追加 */
```

`params` は「空 or 1つ以上のパラメータ」。`param_list` は右再帰でカンマ区切りの並びを `NodeList` にする。

`param` は **`INT IDENT`** ── 型と変数名のペア。tiny-c の ch05 では型は `int` だけなので素直に書ける。ch06 で `char *` などが入ると `param : type IDENT` のように別規則を起こすが、今はリテラルに `INT` を書く。

各 `param` は `new_ident(name)` で `NODE_IDENT` を作って返す。これでパラメータ名が AST に乗る（ノードの種類は単に「名前を持つ」という用途で `IDENT` を流用）。

`new_func_def` のシグネチャはこれまでの `(name, body)` から **`(name, params, body)`** に変える。3引数に増える。

## 3. 関数呼び出し式

`primary` に1つ規則を加える。

```yacc
primary
    : INT_LIT                    { $$ = new_int_lit($1); }
    | IDENT                      { $$ = new_ident($1); }
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }      /* 追加 */
    | '(' expr ')'               { $$ = $2; }
    ;

args                                                             /* 追加 */
    : /* empty */                { $$ = NULL; }                  /* 追加 */
    | arg_list                   { $$ = $1; }                    /* 追加 */
    ;                                                            /* 追加 */

arg_list                                                         /* 追加 */
    : expr                       { $$ = new_node_list($1, NULL); }    /* 追加 */
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }      /* 追加 */
    ;                                                            /* 追加 */
```

注目点: `IDENT` 単体と `IDENT '(' args ')'` は同じトークン (`IDENT`) で始まる。**bison は競合せずに解決できる**: `IDENT` の次に `(` が来たら関数呼び出し、それ以外なら変数参照。

なぜ競合しないか? bison は規則を選ぶとき、**次に来るトークンをひとつだけ覗き見**する。`IDENT` を読んだ直後にこの覗き見をして:

- 次が `(` なら → 関数呼び出しの規則 (`IDENT '(' args ')'`) として組み立てる
- それ以外（`+`、`*`、`;`、`)`、`,` など、`primary` の後ろに来うる記号）なら → 変数参照の規則 (`IDENT` 単体) として `primary` に reduce する

どちらか一意に決まるので、規則の競合は起きない。

## 4. AST の新しいノード

```c
NODE_PROGRAM,    /* トップレベル: stmts = list of func_defs */
NODE_CALL,       /* 関数呼び出し: name, args (NodeList of expr) */
```

`Node` 構造体に `params` と `args` の2フィールドを追加（ともに `NodeList *`）。**`params` は関数定義側の仮引数、`args` は関数呼び出し側の実引数** ── 用途が違うので別フィールドで持つ。

```c
struct Node {
    /* ... 既存のフィールド ... */
    NodeList *params;    /* NODE_FUNC_DEF 用: 仮引数（IDENT のリスト）*/
    NodeList *args;      /* NODE_CALL 用:    実引数（式のリスト）*/
};
```

コンストラクタを3つ追加:

```c
Node *new_call(char *name, NodeList *args);                  /* 追加 */
Node *new_func_def(char *name, NodeList *params, Node *body);/* シグネチャ変更 */
Node *new_program(NodeList *funcs);                          /* 追加 */
```

`new_func_def` は `params` を中で保持するように変更。

## 5. 前方参照（forward reference）について

tiny-c は **prototype を要求しない**。フロントエンドで関数の存在チェックをせず、`call name` を吐くだけ。実体の解決は **リンカ任せ**。

- 同じソース内で後ろに定義された関数も呼べる（forward reference）。
- `printf` などの外部関数も同じように呼べる（libc から解決）。
- 存在しない関数名はリンクエラーになる（コンパイル時には気付けない）。

意味解析を省略しているおかげで、相互再帰や外部関数呼び出しがタダで手に入る。

## 6. 引数の数チェック

文法レベルでは「最大6個」という制約は表現できない。codegen で実行時的にチェックする（ch05 の節 04 で見る）。`call` の前に「7個以上だったらエラー」。

引数 0個の場合（`foo()`）は `args : /* empty */` で受け入れる。

## 7. AST の例

入力:

```c
int add(int a, int b) {
    return a + b;
}
int main() {
    return add(3, 4);
}
```

AST:

```
PROGRAM
  FUNC_DEF add
    PARAM a
    PARAM b
    BLOCK
      RETURN
        BINARY +
          IDENT a
          IDENT b
  FUNC_DEF main
    BLOCK
      RETURN
        CALL add
          INT_LIT 3
          INT_LIT 4
```

`PROGRAM` の下に2つの `FUNC_DEF`。`add` は `PARAM a`、`PARAM b` を持ち、本体で `IDENT a + IDENT b` を返す。`main` は `CALL add` ノード（引数 `INT_LIT 3`、`INT_LIT 4`）を含む。

`PARAM` は `print_ast` が `NODE_IDENT` を「PARAM」として表示しているだけで、ノードの kind 自体は `NODE_IDENT`。

## 次へ

次の節 (`02_abi.md`) で、関数を呼ぶときのルール ── **System V AMD64 ABI** ── を見る。
