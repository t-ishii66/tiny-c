# 01 — 文法の追加

## 1. 比較演算子と論理否定

CLAUDE.md の優先順位表を再掲する。

| 優先度 | 演算子               | 説明                 |
|-------|---------------------|---------------------|
| 2     | `*` (単項) `&` `-` (単項) `!` | 単項 |
| 3     | `*` `/` `%`         | 乗除・剰余           |
| 4     | `+` `-`             | 加減                 |
| 5     | `<` `<=` `>` `>=`   | 比較                 |
| 6     | `==` `!=`           | 等値比較             |
| 7     | `=`                 | 代入                 |

ch03 までで作った階層は `assign → add_expr → mul_expr → unary → primary` の5段。これに **比較 (relational)** と **等値比較 (equality)** の2段を `assign` と `add_expr` の間に挟む。論理否定 `!` は `unary` に1行加えるだけ。

```yacc
expr
    : assign
    ;

assign
    : equality
    | equality '=' assign
    ;

equality
    : relational
    | equality EQ_OP relational
    | equality NE_OP relational
    ;

relational
    : add_expr
    | relational '<' add_expr
    | relational LE_OP add_expr
    | relational '>' add_expr
    | relational GE_OP add_expr
    ;

add_expr   : ...   /* ch02 と同じ */
mul_expr   : ...
unary
    : primary
    | '-' unary
    | '!' unary                     /* 追加 */
    ;
```

`EQ_OP` `NE_OP` `LE_OP` `GE_OP` は、複数文字のトークンを bison で扱うために名前付きトークンにしたもの。1文字の `<` `>` はそのまま文字トークン。

新しい階層が増えただけで、書き方の **パターンは同じ** ── 階層を深くしていくほど優先順位が高い、左再帰で左結合、というルールは ch02 から変わらない。

## 2. 演算子コードの拡張

`==` `!=` `<=` `>=` は単一文字に収まらない。ch03 までは `Node->op` が `char` 1バイトだったが、これを `int` に拡張し、複数文字演算子には ASCII 範囲外の値を割り当てる。

```c
/* ast.h */
struct Node {
    ...
    int op;          /* ASCII code for single-char ops, or OP_xx for multi-char */
    ...
};

enum {
    OP_LE = 256,    /* <= */
    OP_GE,          /* >= */
    OP_EQ,          /* == */
    OP_NE,          /* != */
};
```

これで `node->op == '+'` も `node->op == OP_LE` も同じ `int` 比較で書ける。codegen の `switch` 文に新しい `case` を足すだけで対応できる。

なぜ 256 から始めるか? `char` は最大 255 までしか取れないので、256 以上の値なら **絶対に ASCII とぶつからない**。ch01 の lexer の章で見た「名前付きトークンは 256 以降」と同じ発想だ。値の住み分けで衝突を避ける。

## 3. lexer の追加

複数文字トークンと新しいキーワード、`!` `<` `>` を追加。

```flex
"if"        { return IF; }
"else"      { return ELSE; }
"while"     { return WHILE; }
"=="        { return EQ_OP; }
"!="        { return NE_OP; }
"<="        { return LE_OP; }
">="        { return GE_OP; }
"<"         { return '<'; }
">"         { return '>'; }
"!"         { return '!'; }
```

flex は **最長一致** がデフォルトだから、`==` の規則が `=` の規則より下にあっても、入力 `==` は `==` のほうにマッチする（`=` は1文字、`==` は2文字、長いほうが勝つ）。同様に `!=` も `=` も衝突しない。

キーワード（`if` `else` `while`）は識別子の規則 `[a-zA-Z_][a-zA-Z0-9_]*` より **先に書く**（最長一致が同点のときは先勝ち）。これは ch01 で `int` `return` を識別子より先に書いたのと同じ理由。

## 4. 新しい文 — if、while、ブロック

```yacc
stmt
    : RETURN expr ';'
    | INT IDENT '=' expr ';'
    | expr ';'
    | '{' stmts '}'                         /* 追加: ブロック文 */
    | IF '(' expr ')' stmt                  /* 追加: if */
    | IF '(' expr ')' stmt ELSE stmt        /* 追加: if-else */
    | WHILE '(' expr ')' stmt               /* 追加: while */
    ;
```

それぞれ素直な文法。`if` の本体（`then` 部）も `else` 部も、`while` の本体も、すべて **stmt** ── つまり「式文」「ブロック」「他の if/while」「return」など何でも置ける。だから `if` の中に `while` を、`while` の中に `if` を入れ子にできる。

ブロック文 (`'{' stmts '}'`) を新しく加える。これがないと、複数の文をひとかたまりとして if や while の中に書けない。`new_block` は ch01 から存在するコンストラクタを再利用する（func_def で使っていたのと同じ）。

`new_if` と `new_while` は ast.c で新たに作る。それぞれ `NODE_IF` `NODE_WHILE` を返す。

## 5. dangling-else の問題

```c
if (a) if (b) x = 1; else x = 2;
```

この `else` は、外側の `if (a)` と内側の `if (b)` のどちらに属するか? C 言語では「**最も内側の if に属する**」と定められている。つまり次と等価:

```c
if (a)
    if (b)
        x = 1;
    else
        x = 2;
```

文法を素直に書くと、bison はこれを **shift/reduce 競合** として報告する。

```
| IF '(' expr ')' stmt
| IF '(' expr ')' stmt ELSE stmt
```

外側の `if (a)` を還元する直前、内側の `if (b) x = 1;` の後に `else` トークンが見えたとき、bison は迷う:

- 還元: 内側の `IF '(' expr ')' stmt`（`else` なし版）を **stmt に還元** し、続く `else` は外側の `if` のものとして扱う。
- shift: `else` を **shift して** 内側の `IF '(' expr ')' stmt ELSE stmt`（`else` あり版）の一部として組み立てる。

bison のデフォルトは **shift**。これがちょうど C 言語の規則（内側の if に bind）と一致する。だからデフォルト動作で正しい。

警告だけ出るのを、`%expect 1` 宣言で「1 個の競合は想定通りである」と明示して抑える。

```yacc
%expect 1
```

これが宣言されていると、競合が **ちょうど1個** のときに警告にならない。0 個でも 2 個以上でも警告が出るので、想定外の競合があれば気付ける。

## 6. AST のかたち

新しい3種類のノード。

```c
NODE_IF:
    cond        // 条件式
    then_body   // then 部の文
    else_body   // else 部の文 (NULL なら else なし)

NODE_WHILE:
    cond        // 条件式
    body        // 本体の文

NODE_BLOCK:
    stmts       // 文のリスト (ch01 から既存)
```

`Node` 構造体に `cond`、`then_body`、`else_body` のフィールドを追加する。`body` は ch01 で関数定義のために用意したもので、`while` でも再利用する。

`new_if` と `new_while` のコンストラクタは素朴。

```c
Node *new_if(Node *cond, Node *then_body, Node *else_body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_IF;
    n->cond = cond;
    n->then_body = then_body;
    n->else_body = else_body;       // NULL でもよい
    return n;
}

Node *new_while(Node *cond, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_WHILE;
    n->cond = cond;
    n->body = body;
    return n;
}
```

## 7. AST 例

入力:

```c
if (x > 0)
    y = 1;
else
    y = 2;
```

AST:

```
IF
├─ BINARY >          ← 条件
│  ├─ IDENT x
│  └─ INT_LIT 0
├─ EXPR_STMT         ← then 部
│  └─ ASSIGN
│     ├─ IDENT y
│     └─ INT_LIT 1
└─ EXPR_STMT         ← else 部
   └─ ASSIGN
      ├─ IDENT y
      └─ INT_LIT 2
```

`while` も同様に2つ枝（条件と本体）を持つ木になる。

## 8. ブロックスコープについての補足

ch04 では `{ ... }` をブロック文として扱うが、**ブロックスコープは実装していない**。例えば:

```c
if (x) {
    int y = 1;        // 1
}
{
    int y = 2;        // 2: ch04 ではエラー (redeclared variable: y)
}
```

C ではブロックを抜けると変数は寿命を終え、新しいブロックで同名の変数を宣言できる。tiny-c の codegen はシンボルテーブルがフラット（関数全体で1つ）なので、これを「再宣言」とみなしてエラーにする。

ブロックを抜けたタイミングで symbolテーブルから消す、というスコープ管理は意図的に省いた。教育用には十分だし、ch04 の本題ではない。

## 9. まとめ

- 比較演算子は2段階の階層 `equality > relational` で表現する（C の優先順位通り）。
- 複数文字演算子は ASCII 範囲外（256 以降）の値を割り当てて `op` を `int` に。
- `if` `while` `{ ... }` を stmt として加える。それぞれ stmt を子に持つ。
- dangling-else は bison のデフォルト shift 動作で正しく解決される。`%expect 1` で想定通りと宣言。
- ブロックスコープはこの章では扱わない（実装の単純さを優先）。

## 次へ

文法と AST が整った。次は codegen。比較演算と論理否定がどうアセンブリになるかを、サブ章 02（`02_compare.md`）で見る。
