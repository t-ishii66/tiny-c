# flex/bison の読み方ガイド

tiny-c のレキサ (`src/lexer.l`) とパーサ (`src/parser.y`) を読むための最低限の知識。

## flex (lexer.l)

flex は入力テキストを**トークン列**に分解する。ファイルは3つのセクションからなる:

```
%{ C言語のコード(ヘッダinclude等) %}
%option ...
%%
パターン    { アクション }
パターン    { アクション }
...
%%
```

### パターンとアクション

左側が正規表現パターン、右側がマッチしたときの C コード。

```
[0-9]+          { yylval.int_val = atoi(yytext); return T_INT_LIT; }
```

この例の意味:
- `[0-9]+` — 1文字以上の数字列にマッチ
- `yytext` — マッチした文字列 (例: `"42"`)
- `yylval` — パーサに値を渡すための共用体。`.int_val` に整数値をセット
- `return T_INT_LIT` — パーサに「整数リテラル」トークンを返す

### よく使うパターン

| パターン | 意味 |
|---------|------|
| `[0-9]+` | 1文字以上の数字 |
| `[a-zA-Z_][a-zA-Z0-9_]*` | 識別子 (変数名・関数名) |
| `\"([^"\\]|\\.)*\"` | 文字列リテラル (エスケープ対応) |
| `[ \t\r]+` | 空白 (読み飛ばす) |
| `"//".*` | 行コメント (読み飛ばす) |

### キーワードの扱い

キーワード (`if`, `int` 等) は識別子のパターンより**先に**書く。flex は先に書いたルールを優先する:

```
"int"               { return T_INT; }        ← 先にマッチ
[a-zA-Z_][a-zA-Z0-9_]*  { ... return T_IDENT; }  ← "int" 以外の識別子
```

## bison (parser.y)

bison は トークン列から**構文木 (AST)** を構築する。ファイルは3セクション:

```
%{ C言語のコード %}
%union { ... }
%token ...
%type ...
%%
文法規則
%%
```

### %union

パーサのスタックに載る値の型を定義する。flex の `yylval` と共有:

```c
%union {
    int int_val;
    char char_val;
    char *str_val;
    Node *node;
    NodeList *list;
    Type type;
}
```

### %token と %type

```
%token <int_val> T_INT_LIT    ← 終端記号(トークン)。<int_val> は union のどのメンバか
%type <node> expr stmt        ← 非終端記号。<node> は union のどのメンバか
```

### 文法規則の例

```
add_expr
    : mul_expr                  { $$ = $1; }
    | add_expr '+' mul_expr     { $$ = new_binary(ND_ADD, $1, $3); }
    | add_expr '-' mul_expr     { $$ = new_binary(ND_SUB, $1, $3); }
    ;
```

読み方:
- `add_expr` は `mul_expr` 単体か、`add_expr + mul_expr` か、`add_expr - mul_expr`
- `$$` — この規則が返す値
- `$1`, `$3` — 右辺の1番目、3番目の要素の値 (`$2` は `'+'` なので値はない)
- `new_binary(...)` — AST ノードを作って返す

### 優先順位の実現

bison で演算子の優先順位は、文法規則の**入れ子**で表現する:

```
expr → assign_expr → or_expr → and_expr → eq_expr → rel_expr → add_expr → mul_expr → unary_expr → postfix_expr → primary_expr
```

下に行くほど結合が強い。`2 + 3 * 4` は `mul_expr` が先に `3 * 4` を結合し、その結果を `add_expr` が `2 +` と結合する。

### dangling else

```
%nonassoc T_LOWER_THAN_ELSE
%nonassoc T_ELSE
```

`if (x) if (y) a; else b;` の `else` が内側の `if` に結合するよう解決するテクニック。bison に「`else` があれば shift する（内側に結合）」と伝える。

## ビルドの流れ

```
lexer.l  →[flex]→  lex.yy.c
parser.y →[bison]→ parser.tab.c + parser.tab.h
                         ↓
              gcc でコンパイル・リンク → tinyc
```

`parser.tab.h` にトークン定義 (`T_INT`, `T_IF` 等) が生成され、`lex.yy.c` がそれを include する。
