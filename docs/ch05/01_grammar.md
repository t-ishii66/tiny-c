# 01 — 文法と AST

ch04 までの「プログラムは1つの関数」「関数は引数なし」という前提を捨てる。プログラムは関数の列であり、関数は引数を取れる。

## 1. プログラムを関数の列に

ch01 から ch04 まで、`program` の規則はずっとこうだった。

```yacc
program : func_def ;
```

「プログラムは関数1つ」。これを「関数の列」に変える。

```yacc
program
    : func_defs                  { program = new_program($1); }
    ;

func_defs
    : /* empty */                { $$ = NULL; }
    | func_def func_defs         { $$ = new_node_list($1, $2); }
    ;
```

`func_defs` は右再帰で、関数の連続を `NodeList` として組み上げる。空でも OK（理論上は関数のないプログラム）。`new_program` は新しいコンストラクタで、`NODE_PROGRAM` ノードを作る。

`Node *program` というグローバル変数は引き続き「パース結果のルート」だが、その中身が `NODE_FUNC_DEF` から `NODE_PROGRAM` に変わる。`NODE_PROGRAM.stmts` が関数定義のリスト。

## 2. パラメータ付き関数定義

```yacc
func_def
    : INT IDENT '(' params ')' '{' stmts '}'
                                 { $$ = new_func_def($2, $4, new_block($7)); }
    ;

params
    : /* empty */                { $$ = NULL; }
    | param_list                 { $$ = $1; }
    ;

param_list
    : param                      { $$ = new_node_list($1, NULL); }
    | param ',' param_list       { $$ = new_node_list($1, $3); }
    ;

param
    : INT IDENT                  { $$ = new_ident($2); }
    ;
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
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }    /* 追加 */
    | '(' expr ')'               { $$ = $2; }
    ;

args
    : /* empty */                { $$ = NULL; }
    | arg_list                   { $$ = $1; }
    ;

arg_list
    : expr                       { $$ = new_node_list($1, NULL); }
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }
    ;
```

注目点: `IDENT` 単体と `IDENT '(' args ')'` は同じトークン (`IDENT`) で始まる。**bison は競合せずに解決できる**: `IDENT` の次に `(` が来たら関数呼び出し、それ以外なら変数参照。

なぜ競合しないか? lookahead `(` が **`primary` の FOLLOW 集合に含まれない** から。`primary` の後ろに来るのは演算子 (`+`、`*` 等) や `;`、`)`、`,` であって、`(` はない。だから bison は「`IDENT` の後に `(` を見たら、必ず関数呼び出しの規則」と一意に判断できる。

## 4. AST の新しいノード

```c
NODE_PROGRAM,    /* トップレベル: stmts = list of func_defs */
NODE_CALL,       /* 関数呼び出し: name, args (NodeList of expr) */
```

`Node` 構造体に `params` と `args` の2フィールドを追加（ともに `NodeList *`）。

```c
struct Node {
    /* ... 既存のフィールド ... */
    NodeList *params;    /* NODE_FUNC_DEF: list of NODE_IDENT (param names) */
    NodeList *args;      /* NODE_CALL: list of expression nodes */
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

C では普通、関数を呼ぶ前に **宣言** (prototype) が必要だ。

```c
int foo(int x);          // prototype
int main() {
    return foo(3);       // 呼び出し
}
int foo(int x) {         // 定義（prototype 不要なら定義が main の前にあるべき）
    return x + 1;
}
```

tiny-c は **prototype を要求しない**。理由は単純で、コンパイラのフロントエンドで「関数の存在チェック」をしないから。`new_call(name, args)` は名前を `char *` で持つだけ。codegen 時にも、関数が同じソース内で定義されているかチェックしない。`call name` を吐くだけ。

つまり実体は **リンカ任せ**。`call name` の `name` を「リンカの仕事」にしておけば、

- 同じプログラムの後ろのほうの関数も呼べる（forward reference）。
- `printf` のような外部関数も同じように呼べる（リンク時に libc から解決される）。
- 存在しない関数を書くと **リンクエラー**（実行ファイルにできない）。

これが「最小のフロントエンド」の利点。意味解析を省略すれば、相互再帰も外部関数呼び出しもタダで手に入る。

ただし誤った関数名のミスタイプは、コンパイル時には気付けず、リンク時にやっと「`undefined reference to xyz`」とわかる。プロダクションのコンパイラはここで意味解析をするが、tiny-c では割り切る。

## 6. 引数の数チェック

文法レベルでは「最大6個」のような制約は表現できない。codegen で実行時的にチェックする（ch05 のサブ章 04 で見る）。`call` の前に「7個以上だったらエラー」。

引数 0個の場合（`foo()`）も `args : /* empty */` で受け入れる。よくある「`int main(void)` と書くべきか `int main()` と書くべきか」の議論は、tiny-c では関係ない ── どちらも「引数なし」として同じに扱う。

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

## 8. まとめ

- `program : func_defs`、複数の関数を並べられるようになった。
- `func_def : INT IDENT '(' params ')' '{' stmts '}'`、`param_list` でカンマ区切りの引数を取る。
- `primary : IDENT '(' args ')'` で関数呼び出し。bison は `IDENT` の後の `(` を見て区別する。
- AST に `NODE_PROGRAM`、`NODE_CALL` 追加。`Node` に `params`、`args` フィールド追加。
- 前方参照と外部関数呼び出しは **意味解析を省略している**ことの自然な帰結。

## 次へ

文法と AST はこれで揃った。だがコンパイラの仕事の本番はここから。次のサブ章 (`02_abi.md`) で、関数を呼ぶときの「ルール」── **System V AMD64 ABI** ── を見る。レジスタの使い分け、戻り値の置き場所、スタックの整え方。これは「契約」だ。守らないと動かない。
