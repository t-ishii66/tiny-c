# 01 — 型と文法の追加

ch05 までの tiny-c は **暗黙に全部 int** だった。ch06 で初めて型を区別する: `int`、`char`、`int *`、`char *`、加えて配列 `int[N]` `char[N]`、関数戻り値専用の `void`。

## 1. Type 構造体

各変数・パラメータの型を表現する小さな構造体を導入する。tiny-c の型はかなり限られている（1 段ポインタのみ、1 次元配列のみ）ので、フラットな構造で十分:

```c
typedef struct {
    int is_pointer;   /* 1 if T*  */
    int is_array;     /* 1 if T[N] */
    int base_size;    /* 1 (char) or 4 (int) */
    int array_size;   /* T[N] の N */
} Type;
```

組み合わせの一覧:

| C の型 | is_pointer | is_array | base_size | array_size |
|--------|-----------|----------|-----------|-----------|
| `int`     | 0 | 0 | 4 | 0 |
| `char`    | 0 | 0 | 1 | 0 |
| `int *`   | 1 | 0 | 4 | 0 |
| `char *`  | 1 | 0 | 1 | 0 |
| `int[10]` | 0 | 1 | 4 | 10 |
| `char[10]`| 0 | 1 | 1 | 10 |
| `void`    | 0 | 0 | 0 | 0 |

ヘルパー関数:

- `type_size(t)`: その変数が占めるバイト数（ptr=8、array=base_size×array_size、scalar=base_size）
- `elem_size(t)`: 配列・ポインタの要素サイズ（base_size）

`int *p` の **値そのもの** は 8 バイト（アドレス）だが、`*p` で読み出すのは 4 バイト（int）。`base_size` と「値そのもののサイズ」を区別する必要がある。

## 2. 新しいトークン

```flex
"char"      { return CHAR; }
"void"      { return VOID; }
"&"         { return '&'; }
"["         { return '['; }
"]"         { return ']'; }

'(\\.|[^\\'])'      { /* char literal */ ... return CHAR_LIT; }
\"([^"\\]|\\.)*\"   { /* string literal */ ... return STRING_LIT; }
```

文字リテラル: `'a'`、`'\n'`、`'\\'` など。エスケープシーケンスを実際の文字に変換し、`yylval.int_val` に文字コード（`int` として）を入れる。

文字列リテラル: `"hello"`、`"%s\n"` など。前後の `"` を剥がし、エスケープを解決して、`yylval.str.s`（文字列）と `yylval.str.n`（長さ + `\0`）に入れる。

エスケープ処理の例:

```c
"hello\n"   →  バイト列: 'h' 'e' 'l' 'l' 'o' 0x0A '\0'   長さ 7
'\n'        →  値: 10 (ASCII で改行 LF、0x0A)
'\0'        →  値: 0  (ASCII で NUL)
```

## 3. type 規則

```yacc
type
    : INT                  { $$ = type_int(); }
    | CHAR                 { $$ = type_char(); }
    | VOID                 { /* base_size = 0 */ }
    | INT '*'              { $$ = type_ptr(4); }
    | CHAR '*'             { $$ = type_ptr(1); }
    ;
```

`INT '*'` のように `*` をトークンとして使っているのに注意。`*` は **二項演算子**（乗算）でもあるし **単項演算子**（間接参照）でもある。bison は文脈で区別する。

## 4. グローバル変数: トップレベルの拡張

ch05 までは `program : func_defs` だった。ch06 で関数定義とグローバル宣言を並べられるよう、トップレベルを書き直す。

```yacc
program
    : top_levels                             { program = new_program($1); }
    ;

top_levels
    : /* empty */
    | top_level top_levels                   { $$ = new_node_list($1, $2); }
    ;

top_level
    : func_def                               { $$ = $1; }
    | type IDENT ';'                         { $$ = new_global_var_decl($2, $1); }
    | type IDENT '[' INT_LIT ']' ';'         { $$ = new_global_var_decl($2, type_array($1->base_size, $4)); }
    ;
```

グローバル変数は tiny-c では **初期化子なし** ── `int g;` `int a[10];` の形だけ。コード生成時に `.bss` セクションへゼロ初期化で配置する。

## 5. ローカル宣言の拡張: type を使う

ch03 では `INT IDENT '=' expr ';'` だったが、ch06 では `type IDENT '=' expr ';'`。型情報が AST に乗る。

```yacc
stmt
    : ...
    | type IDENT '=' expr ';'                { $$ = new_var_decl($2, $1, $4); }
    | type IDENT '[' INT_LIT ']' ';'         { $$ = new_var_decl($2, type_array($1->base_size, $4), NULL); }
    | ...
    ;
```

`int x = 5;` は `var_decl(name="x", type=int, init=5)`。`int a[5];` は `var_decl(name="a", type=int[5], init=NULL)`（配列は初期化子なし）。

## 6. パラメータも type を使う

```yacc
param
    : type IDENT                             {
        Node *p = new_ident($2);
        p->type = $1;
        $$ = p;
    }
    ;
```

これで `int x` も `char *s` も同じ枠組みで受け取れる。

## 7. 添字 `a[i]`

新しいノード `NODE_INDEX`。AST 上は左辺（配列/ポインタ）と右辺（インデックス）を持つ。

```yacc
primary
    : ...
    | IDENT '[' expr ']'                     { $$ = new_index(new_ident($1), $3); }
    | ...
    ;
```

```c
typedef struct Node Node;
struct Node {
    /* ... */
    /* NODE_INDEX: lhs = base, rhs = index */
};
```

`a[0]` も `a[i+1]` も同じ規則で扱える（インデックスは任意の式）。tiny-c では実用上 `IDENT '[' expr ']'` の形だけサポート（`(p+1)[i]` のような複雑な左辺は許さない）。

## 8. 単項 `&` `*`

```yacc
unary
    : primary
    | '-' unary                              { $$ = new_unary('-', $2); }
    | '!' unary                              { $$ = new_unary('!', $2); }
    | '&' unary                              { $$ = new_unary('&', $2); }    /* 追加 */
    | '*' unary                              { $$ = new_unary('*', $2); }    /* 追加 */
    ;
```

`*` は **二項** で `mul_expr '*' unary`、**単項** で `'*' unary` の両方に登場する。`a * b` は `mul_expr` で、`*p` は `unary` でマッチする。

## 9. 文字 / 文字列リテラル

```yacc
primary
    : INT_LIT                                { $$ = new_int_lit($1); }
    | CHAR_LIT                               { $$ = new_char_lit($1); }       /* 追加 */
    | STRING_LIT                             { $$ = new_string_lit($1.s, $1.n); } /* 追加 */
    | ...
    ;
```

- `CHAR_LIT` は単なる整数値（C では `'a'` の値は `int`）。
- `STRING_LIT` は文字列の中身と長さを持つ。codegen 時に `.rodata` に配置し、その**アドレス** を式の値とする（`char *` 相当）。

## 10. AST のフィールド追加

```c
struct Node {
    /* ... 既存 ... */
    Type *type;        /* NODE_VAR_DECL, NODE_GLOBAL_VAR_DECL, IDENT (param) */
    char *str_val;     /* NODE_STRING_LIT */
    int str_len;       /* NODE_STRING_LIT */
};
```

`type` は変数宣言とパラメータに付く。式ノード（`IDENT`、`INDEX` など）には付けず、コード生成時にシンボルテーブルから引く（または式構造から推論する）。

## 11. AST の例

```c
int main() {
    char *s = "hello";
    return s[0];
}
```

AST:

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL s : char*
        STRING_LIT "hello"
      RETURN
        INDEX
          IDENT s
          INT_LIT 0
```

`s` の型が `char*`、`STRING_LIT` で `"hello"` を保持、`s[0]` が `INDEX` ノード。codegen ではこの型情報を見て、char 単位で 1 バイトずつ読み出すコードを生成する（次の節）。

## 12. 次へ

文法と AST に型が入った。次の節（`02_lvalue.md`）で、コード生成側の核心 ── **`gen_addr` 関数** と **lvalue / rvalue の対称性** ── を見る。`*` と `&` がコード生成の上でどう対応関係にあるか、配列名がなぜ自動的にアドレスになる（"decay"）か、すべて `gen_addr` と `gen_expr` の使い分けで説明できる。
