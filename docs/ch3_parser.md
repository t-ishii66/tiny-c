# 章3: 構文解析 — bison

## この章で学ぶこと

章2 で lexer がトークン列を作った。しかしトークン列は**平ら**だ:

```
T_INT_LIT(2)  '+'  T_INT_LIT(3)  '*'  T_INT_LIT(4)
```

この5つのトークンから、「`*` が先に結合する」という**構造**を読み取らなければならない。構文解析（parsing）は、トークン列を**文法規則**に照らして木構造に変換する作業だ。

tiny-c では構文解析器（parser）を **bison** というツールで生成する。

## bison ファイルの構造

`src/parser.y` は3つのセクションからなる:

```
%{  Cコード（ヘッダ、ヘルパー関数）  %}
宣言（%union, %token, %type 等）
%%
文法規則
%%
```

各セクションを順に見ていく。

## セクション1: 宣言

### %union — 値の型を定義する

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

パーサはトークンを処理しながら**値をスタックに積む**。その値の型を union で定義する。章2 で見た `yylval` はこの union と同じ型だ。

### %token — トークンの宣言

```
%token T_INT T_CHAR T_VOID T_IF T_ELSE T_WHILE T_RETURN
%token <int_val>  T_INT_LIT
%token <char_val> T_CHAR_LIT
%token <str_val>  T_STRING_LIT
%token <str_val>  T_IDENT
%token T_EQ T_NE T_LE T_GE
```

`%token` は lexer が返すトークンの種類を宣言する。bison はこれを元に `parser.tab.h` に定数を生成し、lexer がそれを include する。

`<int_val>` は「このトークンの値は union の `int_val` メンバに入っている」という指定だ。値を持たないトークン（キーワードや演算子）には型指定がない。

### %type — 非終端記号の型

```
%type <node> expr add_expr mul_expr primary_expr
%type <list> stmt_list arg_list
%type <type> type_spec
```

文法規則の左辺（非終端記号）が生成する値の型を指定する。`expr` は `Node *` を生成し、`stmt_list` は `NodeList *` を生成する。

## セクション2: 文法規則

### 文法規則の読み方

```
add_expr
    : mul_expr                  { $$ = $1; }
    | add_expr '+' mul_expr     { $$ = new_binary(ND_ADD, $1, $3); }
    | add_expr '-' mul_expr     { $$ = new_binary(ND_SUB, $1, $3); }
    ;
```

読み方:

- `add_expr` は3つの形のいずれかになる（`|` で区切る）
- 1つ目: `mul_expr` 単体。その値をそのまま渡す
- 2つ目: `add_expr '+' mul_expr`。左辺と右辺の値から ADD ノードを作る
- `{ }` 内が**アクション**。パターンが認識されたときに実行される C コード

アクション内の特殊変数:

| 変数 | 意味 |
|------|------|
| `$$` | この規則が生成する値（左辺の値） |
| `$1` | 右辺の1番目の要素の値 |
| `$2` | 右辺の2番目の要素の値 |
| `$3` | 右辺の3番目の要素の値 |

`add_expr '+' mul_expr` なら、`$1` は `add_expr` の値、`$2` は `'+'`（値なし）、`$3` は `mul_expr` の値。

### 優先順位の実現: 文法規則の入れ子

tiny-c の式の文法は、以下の入れ子構造になっている:

```
expr → assign_expr → eq_expr → rel_expr → add_expr → mul_expr
                                                         → unary_expr → postfix_expr → primary_expr
```

**下に行くほど結合が強い。** なぜこれが優先順位を実現するのか？

`2 + 3 * 4` で考えてみよう。

```
add_expr
    : mul_expr
    | add_expr '+' mul_expr     ← '+' の右側は mul_expr
    ;

mul_expr
    : unary_expr
    | mul_expr '*' unary_expr   ← '*' はここで先に結合する
    ;
```

`add_expr` の規則は `+` の**右側に `mul_expr` を要求する**。bison は `3 * 4` を `mul_expr` として先に解析しなければ、`add_expr '+' mul_expr` の規則を適用できない。つまり、`*` が `+` より先に結合するのは、**文法規則の入れ子が強制する**のだ。

### パース過程を追いかける

`2 + 3 * 4` がどう解析されるか、ステップごとに見る:

```
入力トークン: T_INT_LIT(2)  '+'  T_INT_LIT(3)  '*'  T_INT_LIT(4)

ステップ1: T_INT_LIT(2) を読む
  → primary_expr → postfix_expr → unary_expr → mul_expr と帰着
  → さらに add_expr にもなれるが、次が '+' なので保留

ステップ2: '+' を読む
  → add_expr '+' ... の途中。右辺の mul_expr を待つ

ステップ3: T_INT_LIT(3) を読む
  → primary_expr → ... → mul_expr と帰着
  → 次が '*' なので、まだ mul_expr の途中

ステップ4: '*' を読む
  → mul_expr '*' ... の途中。右辺を待つ

ステップ5: T_INT_LIT(4) を読む
  → primary_expr → ... → unary_expr と帰着

ステップ6: 入力終了
  → mul_expr '*' unary_expr → mul_expr に帰着（3 * 4 = MUL ノード）
  → add_expr '+' mul_expr → add_expr に帰着（2 + MUL = ADD ノード）
```

結果の木:

```
      ADD
     /   \
    2    MUL
        /   \
       3     4
```

**内側の `MUL` が先に組み立てられ、外側の `ADD` がそれを子として取り込む。** 文法規則の入れ子が、正しい木の形を自動的に保証する。

### 左結合と右結合

`2 - 3 - 4` は `(2 - 3) - 4 = -5` であって、`2 - (3 - 4) = 3` ではない。減算は**左結合**だ。

文法規則で左結合を実現するのは**左再帰**:

```
add_expr
    : mul_expr
    | add_expr '+' mul_expr     ← 左辺が add_expr 自身（左再帰）
    ;
```

`add_expr '+' mul_expr` の左辺が `add_expr` 自身なので、`2 - 3 - 4` は次のように解析される:

```
  (add_expr '-' mul_expr) '-' mul_expr
  ─────────┬──────────
       add_expr                          ← 2 - 3 が先に結合
```

もし右結合にしたければ（代入 `=` は右結合）、**右再帰**にする:

```
assign_expr
    : eq_expr
    | unary_expr '=' assign_expr    ← 右辺が assign_expr 自身（右再帰）
    ;
```

これにより `a = b = 5` は `a = (b = 5)` と解析される。

### 文と宣言の規則

式だけでなく、文や宣言も文法規則で定義する:

```
stmt
    : expr ';'                { $$ = new_node(ND_EXPR_STMT); $$->lhs = $1; }
    | T_RETURN expr ';'       { $$ = new_node(ND_RETURN); $$->lhs = $2; }
    | T_IF '(' expr ')' stmt T_ELSE stmt
                              { $$ = new_node(ND_IF);
                                $$->lhs = $3; $$->rhs = $5; $$->extra = $7; }
    | T_WHILE '(' expr ')' stmt
                              { $$ = new_node(ND_WHILE);
                                $$->lhs = $3; $$->rhs = $5; }
    | ...
    ;
```

`T_RETURN expr ';'` は「`return` トークン、式、セミコロンの並び」にマッチする。マッチしたら ND_RETURN ノードを作り、`$2`（expr の値）を子にセットする。

`stmt` の右辺にも `stmt` が現れる（`if` の then 節や while の本体）。これにより `if` のネストや `while` の中に `if` を書くといった構造が自然に表現できる。

### dangling else の解決

```c
if (a) if (b) x; else y;
```

この `else` は内側の `if (b)` に属するのか、外側の `if (a)` に属するのか？ C 言語では内側に属する。bison にはこの曖昧さを解決する仕組みがある:

```
%nonassoc T_LOWER_THAN_ELSE
%nonassoc T_ELSE

stmt
    : T_IF '(' expr ')' stmt  %prec T_LOWER_THAN_ELSE
    | T_IF '(' expr ')' stmt T_ELSE stmt
    ;
```

`%prec T_LOWER_THAN_ELSE` は「else なしの if は、else ありの if より優先度が低い」と宣言する。これにより、bison は `else` を見たとき**内側の if に結合する**方を選ぶ。

### 関数定義

```
func_def
    : type_spec T_IDENT '(' param_list ')' '{' stmt_list '}'
        { $$ = new_node(ND_FUNC_DEF);
          $$->type = $1;
          $$->name = $2;
          $$->children = reverse_list($4);
          $$->body = new_node(ND_BLOCK);
          $$->body->children = reverse_list($7); }
    ;
```

`type_spec T_IDENT '(' param_list ')' '{' stmt_list '}'` ——型名、関数名、括弧、引数リスト、波括弧、文のリスト、波括弧——がこの順に並ぶとき、関数定義と認識する。

`reverse_list` が必要な理由: bison はリストを**逆順**に積み上げるため（`stmt_list stmt` の規則で、新しい要素が先頭に追加される）、最後にリストを反転する。

## ビルドの流れ

```
src/parser.y  →[bison]→  build/parser.tab.c   →[gcc]→  parser.tab.o
                          build/parser.tab.h  ←── lexer.l がこれを include
```

`parser.tab.h` にはトークン定数（`T_INT = 258` のような `#define`）と `yylval` の union 定義が入る。lexer はこれを include して、`T_INT_LIT` や `yylval.int_val` を使う。

## この章の要点

1. bison の文法規則は**パターンとアクション**の対。パターンが認識されるとアクションが実行される
2. **演算子の優先順位は文法規則の入れ子**で実現する。`add_expr` が `mul_expr` を子に持つことで、`*` が `+` より先に結合する
3. **左再帰 = 左結合、右再帰 = 右結合**。文法規則の再帰の方向が結合性を決める
4. 各規則のアクション（`{ }` 内の C コード）が **AST のノードを組み立てる**。`$1`, `$3` で子要素の値を受け取り、`$$` で親に返す

## 次の章へ

パーサのアクションで `new_binary(ND_ADD, $1, $3)` のように AST ノードを作っていた。次の章で、この AST のデータ構造を詳しく見る。
