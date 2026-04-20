# 章4: AST — 抽象構文木

## この章で学ぶこと

章3 で、パーサが文法規則のアクションとして `new_binary(ND_ADD, $1, $3)` のようなコードを実行していた。この章では、その結果として作られる **AST（Abstract Syntax Tree, 抽象構文木）** のデータ構造を見る。

AST はコンパイラの**中間地点**だ。パーサが入力側、コード生成が出力側に立ち、AST がその間をつなぐ:

```
ソースコード → [lexer] → トークン列 → [parser] → AST → [codegen] → アセンブリ
                                                  ↑
                                            ここを見る
```

## Node 構造体 — すべてのノードの共通の器

tiny-c の AST は、1種類の構造体 `Node` だけで表現する。整数リテラルも、二項演算も、if 文も、関数定義も、すべて同じ `Node` だ:

```c
struct Node {
    NodeKind kind;      // ノードの種類 (ND_INT_LIT, ND_ADD, ND_IF, ...)
    Type type;          // 型情報 (TY_INT, TY_CHAR, ...)

    int int_val;        // ND_INT_LIT: 整数値
    char char_val;      // ND_CHAR_LIT: 文字
    char *name;         // ND_IDENT, ND_FUNC_DEF 等: 名前

    Node *lhs;          // 左の子 / 第1子
    Node *rhs;          // 右の子 / 第2子
    Node *extra;        // 第3子 (if の else 節)
    Node *body;         // 関数の本体

    NodeList *children; // 可変長の子リスト (ブロック、引数リスト等)
    int array_size;     // 配列のサイズ
};
```

`kind` フィールドが「このノードは何か」を決める。`kind` の値に応じて、使うフィールドが変わる:

| kind | 使うフィールド | 意味 |
|------|--------------|------|
| `ND_INT_LIT` | `int_val` | 整数リテラル `42` |
| `ND_IDENT` | `name` | 変数参照 `x` |
| `ND_ADD` | `lhs`, `rhs` | 加算 `lhs + rhs` |
| `ND_ASSIGN` | `lhs`, `rhs` | 代入 `lhs = rhs` |
| `ND_RETURN` | `lhs` | return 文 `return lhs` |
| `ND_IF` | `lhs`, `rhs`, `extra` | if: 条件, then, else |
| `ND_WHILE` | `lhs`, `rhs` | while: 条件, 本体 |
| `ND_FUNC_DEF` | `name`, `type`, `children`, `body` | 関数定義 |
| `ND_BLOCK` | `children` | ブロック `{ ... }` |
| `ND_CALL` | `name`, `children` | 関数呼び出し |

使わないフィールドは `calloc` によりゼロ初期化される。構造体が1種類なのは、コードの単純さのためだ（プロダクションコンパイラでは通常、ノード種ごとに別の構造体を使う）。

## コンストラクタ関数

ノードを作る関数は3種類ある:

```c
// リテラル: 値を持つ葉ノード
Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = ND_INT_LIT;
    n->int_val = val;
    return n;
}

// 二項演算: 左右の子を持つノード
Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = kind;
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

// 単項演算: 子を1つ持つノード
Node *new_unary(NodeKind kind, Node *operand) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = kind;
    n->lhs = operand;
    return n;
}
```

すべて同じパターン: `calloc` で確保し、フィールドを埋めて返す。これらの関数がパーサのアクションから呼ばれる。

## パーサのアクションが木を組み立てる

章3 の文法規則をもう一度見ると、アクションがコンストラクタを呼んでいることがわかる:

```
primary_expr
    : T_INT_LIT             { $$ = new_int_lit($1); }
    ;

add_expr
    : mul_expr              { $$ = $1; }
    | add_expr '+' mul_expr { $$ = new_binary(ND_ADD, $1, $3); }
    ;
```

`T_INT_LIT` にマッチすると `new_int_lit($1)` が呼ばれる。`$1` は lexer が `yylval.int_val` にセットした整数値だ。

`add_expr '+' mul_expr` にマッチすると `new_binary(ND_ADD, $1, $3)` が呼ばれる。`$1` と `$3` はそれぞれ子の規則が `$$` で返した `Node *` だ。

**値が下から上に伝搬する**: primary_expr が作ったリテラルノードは mul_expr に渡され、mul_expr が作った MUL ノードは add_expr に渡される。最終的に、プログラム全体を表す1つの木ができる。

## NodeList — 可変長の子リスト

ブロック `{ stmt; stmt; stmt; }` のように、子の数が固定でない場合は連結リストを使う:

```c
struct NodeList {
    Node *node;
    NodeList *next;
};

NodeList *new_node_list(Node *node, NodeList *next) {
    NodeList *l = calloc(1, sizeof(NodeList));
    l->node = node;
    l->next = next;
    return l;
}
```

パーサの規則でリストを積み上げる:

```
stmt_list
    : /* empty */           { $$ = NULL; }
    | stmt_list stmt        { $$ = new_node_list($2, $1); }
    ;
```

`new_node_list($2, $1)` は新しい要素 `$2` を既存のリスト `$1` の先頭に追加する。これにより bison はリストを**逆順**に構築する。最後に `reverse_list()` で正しい順序に戻す。

## 具体例: AST の全体像

```c
int add(int a, int b) {
    return a + b;
}
int main() {
    int x = add(1, 2);
    if (x > 0) {
        return x;
    }
    return 0;
}
```

この AST を `--dump-ast` で表示すると:

```
Program
  FuncDef(add : int)
    [params]
      Param(a : int)
      Param(b : int)
    [body]
      Block
        Return
          BinOp(+)
            Ident(a)
            Ident(b)
  FuncDef(main : int)
    [params]
    [body]
      Block
        VarDecl(x : int)
          [init]
            Call(add)
              IntLit(1)
              IntLit(2)
        If
          [cond]
            BinOp(>)
              Ident(x)
              IntLit(0)
          [then]
            Block
              Return
                Ident(x)
        Return
          IntLit(0)
```

これをメモリ上の構造体で描くと:

```
Node{ND_PROGRAM}
 └─ children: [FuncDef(add), FuncDef(main)]

Node{ND_FUNC_DEF, name="add", type=TY_INT}
 ├─ children: [Param(a), Param(b)]
 └─ body: Node{ND_BLOCK}
           └─ children: [Node{ND_RETURN}]
                          └─ lhs: Node{ND_ADD}
                                  ├─ lhs: Node{ND_IDENT, name="a"}
                                  └─ rhs: Node{ND_IDENT, name="b"}

Node{ND_FUNC_DEF, name="main", type=TY_INT}
 └─ body: Node{ND_BLOCK}
           └─ children: [VarDecl(x), If, Return]
                           │          │     └─ lhs: IntLit(0)
                           │          ├─ lhs: Node{ND_GT, lhs=Ident(x), rhs=IntLit(0)}
                           │          └─ rhs: Block[Return(Ident(x))]
                           ├─ name: "x"
                           └─ lhs: Node{ND_CALL, name="add"}
                                   └─ children: [IntLit(1), IntLit(2)]
```

ポインタでつながった `Node` の連なりが、プログラムの構造そのものだ。

## AST の設計判断

### なぜ「抽象」構文木なのか

AST は**抽象**構文木であり、ソースコードの字面をそのまま残す「具象構文木」（parse tree）とは異なる。たとえば:

- 括弧 `(2 + 3)` — 括弧はパーサが優先順位を判断するために使うが、木の構造に括弧のノードはない。木の形そのものが結合を表している
- セミコロン `;` — 文の区切り。木には残らない
- 波括弧 `{ }` — ブロックの区切り。`ND_BLOCK` ノードが代わりを務める

ソースコードの**意味に関係ない構文要素は捨てる**。これが AST の「抽象」の意味だ。

### Node が1種類だけの理由

プロダクションコンパイラでは、ノード種ごとに別の構造体やクラスを使うことが多い。tiny-c ではすべてを1つの `Node` 構造体で表す:

- 教育目的では、構造体が1種類のほうが**追いかけやすい**
- `kind` で分岐する `switch` 文で全ノードを網羅できる
- 使わないフィールドが存在するのは無駄だが、コードの単純さを優先する

## この章の要点

1. AST はプログラムの構造を表す**木**。パーサが入力側、コード生成が出力側に立ち、AST がその間をつなぐ
2. `Node` 構造体1つで全ノードを表す。`kind` フィールドが種類を決め、使うフィールドが変わる
3. パーサのアクション（`{ }` 内のコード）がコンストラクタ関数を呼び、**下から上へ**木を組み立てる
4. AST は**抽象**構文木——括弧やセミコロンは捨て、意味のある構造だけを残す

## 次の章へ

ここまでの4章で、ソースコード → トークン列 → AST という変換を理解した:

```
"int main() { return 2 + 3 * 4; }"
  → [lexer]  → T_INT T_IDENT(main) '(' ')' '{' T_RETURN T_INT_LIT(2) '+' ...
  → [parser] → Program → FuncDef → Block → Return → ADD(2, MUL(3, 4))
```

次の章からは、この AST を辿ってアセンブリを出力する**コード生成**に入る。
