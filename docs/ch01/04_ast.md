# 04 — AST と ast.h / ast.c

ここまでで、ソースコードからトークン列、トークン列から木を組み立てる流れが見えた。今度はその「木」自体を、データ構造として設計する。これが**抽象構文木 (AST, Abstract Syntax Tree)** だ。

`ast.h` でノードの型を定義し、`ast.c` でノードを作る関数（コンストラクタ）を書く。第1章で必要なのは、たった4種類のノードだけだ。


## 1. なぜ「木」なのか

そもそも、なぜソースコードを木にするのか。直接アセンブリを吐けないのか。

理屈の上では可能だ。しかし、ソースコードは**文字列**であり、コンパイラが扱うには不便だ。

たとえば「`return` の後に書かれている式は何か」を答えるとき、文字列のままだと「`return` という単語の後ろを探して、最初の `;` までを切り出して、その中身を解釈する」という処理を毎回書くことになる。式が `42` ならまだしも、第2章で扱う `2 + 3 * 4` のような複雑な式になると、悪夢だ。

そこで、ソースコードを一度**メモリ上の構造化されたデータ**に変換しておく。「return 文ノードがある。その子に式ノードがある。その式ノードは整数リテラルで、値は42」── 構造として保持してしまえば、後の処理は単純なポインタの辿り操作になる。

```
"return 42;"           RETURN ノード
                        └─ INT_LIT ノード（値: 42）
```

文字列としての扱いは構文解析までで終わる。それ以降のすべての処理（型チェック、最適化、コード生成）は、この**木の上で**行う。これがコンパイラの定石だ。


## 2. 「抽象」とは何か

AST の「**A**」は **Abstract**（抽象）。具象構文木（CST, Concrete Syntax Tree）と区別するための言葉だ。

具象構文木は「ソースコードに書かれた**全部**」を保持する。スペース、改行、コメント、カッコの位置 ── 全部。`return ( 42 ) ;` と `return 42 ;` は別のCSTになる。

抽象構文木は「**意味に関係するもの**だけ」を保持する。`return ( 42 ) ;` も `return 42 ;` も同じ AST になる。意味的にはどちらも「42 を返す」だけだ。カッコの有無やスペースの数は、コンパイラの後段では使わない情報だから、**捨てる**。

「抽象」とは「不要なものを削ぎ落とす」という意味だ。コンパイラが必要とする最小限の情報だけを残した木 ── それが AST だ。

だから AST には `;` も `(` も `{` もノードとして現れない。これらはソースの構造を解析するためにあったが、構造が分かってしまえば不要だ。


## 3. ノードの設計：1つの構造体か、種類ごとの構造体か

AST のノードを C で表現するとき、設計の選択肢が二つある。

**案A：種類ごとに別の構造体を作る**

```c
typedef struct { int value; } IntLitNode;
typedef struct { Node *expr; } ReturnNode;
typedef struct { char *name; Node *body; } FuncDefNode;
typedef struct { NodeList *stmts; } BlockNode;
```

種類ごとに必要なフィールドだけが入っていて、無駄がない。

**案B：1つの構造体で全種類を表現する**

```c
typedef struct Node {
    NodeKind kind;       /* 種類を示すタグ */
    int int_val;         /* INT_LIT 用 */
    char *name;          /* FUNC_DEF 用 */
    Node *body;          /* FUNC_DEF 用 */
    Node *expr;          /* RETURN 用 */
    NodeList *stmts;     /* BLOCK 用 */
} Node;
```

`kind` で種類を判別し、種類ごとに使うフィールドが決まっている。使わないフィールドは無駄になる。

第1章では**案B**を採用する。理由は **シンプルさ** だ。

案Aだと、ノードを扱う関数は「型ごとに分岐する」必要がある。`Node *` を引数にする関数を書くために、共通のヘッダ部分を持つ構造体や、`union` を使う必要が出てくる。コードが複雑になる。

案Bは確かに使わないフィールドが残るが、ノード1個あたり数十バイトの無駄に過ぎない。コンパイラが扱うノード数はせいぜい数千個程度で、メモリは問題にならない。それより、**コードの読みやすさ**を優先する。

「`switch (node->kind)` で枝分かれする」というシンプルなパターンが、コンパイラ全体で一貫して使える。これは大きな利点だ。


## 4. ch01 のノード4種類

第1章で必要なのは：

| 種類 | 定義する理由 |
|------|------------|
| `NODE_INT_LIT` | `42` のような整数リテラルを表現する |
| `NODE_RETURN` | `return 式;` を表現する |
| `NODE_FUNC_DEF` | `int main() { ... }` を表現する |
| `NODE_BLOCK` | `{ ... }` の中身（文の並び）を表現する |

第2章以降で `NODE_BINARY`（二項演算）、`NODE_VAR_DECL`（変数宣言）、`NODE_IF`、`NODE_WHILE` などが追加されていくが、第1章ではこの4つで足りる。


## 5. ast.h を読む

```c
#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,    /* integer literal: 42 */
    NODE_RETURN,     /* return statement */
    NODE_FUNC_DEF,   /* function definition */
    NODE_BLOCK,      /* { ... } */
} NodeKind;

/* Forward declaration */
typedef struct Node Node;
typedef struct NodeList NodeList;

/* Linked list of nodes */
struct NodeList {
    Node *node;
    NodeList *next;
};

/* AST node */
struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT: value */
    char *name;        /* NODE_FUNC_DEF: function name */
    Node *body;        /* NODE_FUNC_DEF: function body (block) */
    Node *expr;        /* NODE_RETURN: return value */
    NodeList *stmts;   /* NODE_BLOCK: list of statements */
};

/* Constructors */
Node *new_int_lit(int val);
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

/* List operations */
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug output */
void print_ast(Node *node, int indent);

#endif
```

順に見ていこう。

### `NodeKind` enum

```c
typedef enum {
    NODE_INT_LIT,
    NODE_RETURN,
    NODE_FUNC_DEF,
    NODE_BLOCK,
} NodeKind;
```

ノードの種類を表す列挙型。各ノードはこの値を `kind` フィールドに持つ。

### 前方宣言

```c
typedef struct Node Node;
typedef struct NodeList NodeList;
```

`Node` と `NodeList` は互いに参照し合うので、先に「こういう名前の型があるよ」と宣言しておく。これがないと、後の構造体定義で `NodeList *stmts;` と書いたときに「`NodeList` って何？」と怒られる。

### `NodeList` 構造体

```c
struct NodeList {
    Node *node;
    NodeList *next;
};
```

ただの単方向連結リストだ。`node` が今のノード、`next` が次の要素。配列ではなくリストにしているのは、文の数が事前に分からないから動的に作りやすいため。

### `Node` 構造体

```c
struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT: value */
    char *name;        /* NODE_FUNC_DEF: function name */
    Node *body;        /* NODE_FUNC_DEF: function body (block) */
    Node *expr;        /* NODE_RETURN: return value */
    NodeList *stmts;   /* NODE_BLOCK: list of statements */
};
```

これが全てのノードの共通構造体。`kind` を見て、どのフィールドを使うかを判断する。

| kind | 使うフィールド |
|------|--------------|
| `NODE_INT_LIT` | `int_val` |
| `NODE_RETURN` | `expr` |
| `NODE_FUNC_DEF` | `name`, `body` |
| `NODE_BLOCK` | `stmts` |

使わないフィールドは `calloc` でゼロに初期化される（後述）。

### コンストラクタとリスト操作の宣言

```c
Node *new_int_lit(int val);
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);
```

各種類のノードを作る関数の宣言。実装は `ast.c` にある。`print_ast` はデバッグ用で、`--dump-ast` 時に呼ばれる。


## 6. ast.c を読む

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_INT_LIT;
    n->int_val = val;
    return n;
}

Node *new_return(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_RETURN;
    n->expr = expr;
    return n;
}

Node *new_block(NodeList *stmts) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_BLOCK;
    n->stmts = stmts;
    return n;
}

Node *new_func_def(char *name, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_FUNC_DEF;
    n->name = name;
    n->body = body;
    return n;
}

NodeList *new_node_list(Node *node, NodeList *next) {
    NodeList *l = calloc(1, sizeof(NodeList));
    l->node = node;
    l->next = next;
    return l;
}
```

すべてのコンストラクタが同じパターンだ。

1. `calloc(1, sizeof(Node))` でメモリを確保（**ゼロ初期化**される）
2. `kind` を設定
3. 必要なフィールドを設定
4. ポインタを返す

なぜ `malloc` ではなく `calloc` を使うのか。`calloc` は確保したメモリを 0 で埋めてくれる。これにより、**今のノード種類で使わないフィールドが自動的に NULL や 0 になる**。コードが安全になり、初期化忘れのバグを防げる。

メモリは解放しない。コンパイラはコンパイル後すぐ終了するので、OS にまとめて回収させる。プロセスが終わればメモリは戻る。シンプルでいい。

### `print_ast` ── デバッグ用の表示関数

```c
static void indent(int level) {
    for (int i = 0; i < level; i++) printf("  ");
}

void print_ast(Node *node, int level) {
    if (!node) return;
    indent(level);
    switch (node->kind) {
    case NODE_INT_LIT:
        printf("INT_LIT %d\n", node->int_val);
        break;
    case NODE_RETURN:
        printf("RETURN\n");
        print_ast(node->expr, level + 1);
        break;
    case NODE_FUNC_DEF:
        printf("FUNC_DEF %s\n", node->name);
        print_ast(node->body, level + 1);
        break;
    case NODE_BLOCK:
        printf("BLOCK\n");
        for (NodeList *l = node->stmts; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    }
}
```

`--dump-ast` で AST を表示するための関数。**再帰**で木を辿りながら、インデント付きで種類と内容を表示する。

ここに「**コンパイラの主要な処理パターン**」が現れている：

1. `Node *` を受け取る
2. `switch (node->kind)` で種類ごとに分岐
3. 子ノード（`expr`、`body`、`stmts`）に対して再帰呼び出し

このパターンは次のサブ章のコード生成（`codegen.c`）でもまったく同じ形で出てくる。**木を再帰で辿る**── これがコンパイラの基本動作だ。


## 7. AST が組み立てられる様子

`int main() { return 42; }` という入力に対して、parser のアクションがどう実行され、メモリ上にどんな木が出来上がるかを追ってみよう。

トークンの読み込み順とアクションの実行順序：

```
1. INT_LIT(42) を読む
   → expr ルールのアクションが動く
   → new_int_lit(42) が呼ばれる
   → メモリ上：[Node A: kind=INT_LIT, int_val=42]

2. RETURN expr ';' がそろう
   → stmt ルールのアクションが動く
   → new_return(A) が呼ばれる
   → メモリ上：[Node B: kind=RETURN, expr=&A]

3. stmts に B が追加される
   → stmts ルールのアクションが動く
   → new_node_list(B, NULL) が呼ばれる
   → メモリ上：[NodeList L: node=&B, next=NULL]

4. INT IDENT ( ) { stmts } がそろう
   → func_def ルールのアクションが動く
   → new_block(L) が呼ばれる
   → メモリ上：[Node C: kind=BLOCK, stmts=&L]
   → new_func_def("main", C) が呼ばれる
   → メモリ上：[Node D: kind=FUNC_DEF, name="main", body=&C]

5. program に D が代入される
   → グローバル変数 program = &D
```

最終的にメモリ上にこんな木ができる：

```
program ──→ [D: FUNC_DEF "main"]
              │
              body: [C: BLOCK]
                      │
                      stmts: [L: NodeList]
                              │
                              node: [B: RETURN]
                                      │
                                      expr: [A: INT_LIT 42]
```

これが `--dump-ast` で表示される木の正体だ。

```
FUNC_DEF main      ← Node D
  BLOCK            ← Node C
    RETURN         ← Node B
      INT_LIT 42   ← Node A
```


## 8. 木の上で再帰すると何ができるか

AST がメモリ上にできてしまえば、コンパイラの後段の処理はとてもシンプルになる。なぜなら、**「木を辿る再帰関数」を1つ書けば済む**からだ。

例えば：

- **AST を表示する** → `print_ast()`（既に書いた）
- **AST からアセンブリを生成する** → `codegen()`（次のサブ章）
- **型をチェックする** → `type_check()`（後の章で）
- **未使用変数を見つける** → `find_unused()`（やる気があれば）

どれも「ノードの種類で分岐し、子に再帰する」というパターンの再利用だ。

```c
void process(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        /* 葉なので、ここで何かする */
        break;
    case NODE_RETURN:
        process(node->expr);     /* 子を先に処理 */
        /* return 文に対する処理 */
        break;
    case NODE_FUNC_DEF:
        process(node->body);     /* 子を先に処理 */
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            process(l->node);    /* 各文を処理 */
        break;
    }
}
```

このスケルトンに肉を詰めていけば、いろんな処理が書ける。**AST + 再帰**は、コンパイラの最強の武器だ。


## 9. まとめ

- **AST**（抽象構文木）は、コンパイラ内部でソースコードを表現するデータ構造。
- 「抽象」とは「意味に関係しない構文要素は捨てる」の意。スペース、コメント、`(`、`;` などはノードに残らない。
- 第1章では、4種類のノード（`INT_LIT`、`RETURN`、`FUNC_DEF`、`BLOCK`）と単方向リスト（`NodeList`）を使う。
- すべてのノードは1つの `struct Node` で表現する（kind タグで種類を区別）。シンプルさのため。
- ノードを作る関数（コンストラクタ）は `calloc` を使い、使わないフィールドはゼロに保つ。
- AST の上では、再帰で木を辿るのが基本パターン。`print_ast` も `codegen` も `type_check` も、同じ骨格で書ける。


## 次へ

AST がメモリ上にできた。次のサブ章（05_codegen.md）では、この木を辿って x86 アセンブリを出力するコード生成の中身を見ていく。アセンブリの読み方も最小限から押さえる。
