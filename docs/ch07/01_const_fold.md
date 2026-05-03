# 01 — AST レベル最適化（定数畳み込み + 代数的単純化）

最適化の第一弾は **AST レベル**。パースが終わって AST が出来上がった直後、codegen に渡る前に「明らかに無駄な構造」を畳み込む。

## 1. 定数畳み込み (Constant folding)

二項演算の両子が定数なら、その演算結果を **コンパイル時に計算** して、単一の定数ノードに置き換える。

```
BINARY +              →   INT_LIT 5
├─ INT_LIT 2
└─ INT_LIT 3
```

入れ子があっても再帰的に効く:

```
BINARY +              →   BINARY +              →   INT_LIT 14
├─ INT_LIT 2              ├─ INT_LIT 2
└─ BINARY *               └─ INT_LIT 12
   ├─ INT_LIT 3
   └─ INT_LIT 4
```

子を先に最適化（畳み込み）してから親を見ることで、深いネストも一発で全部畳まれる。

対応する演算子は `+ - * / %` の算術と `< > <= >= == !=` の比較。後者は `0` か `1` の `int` を返す（C の比較演算子の規約通り）。

```c
case '<':   r = (a <  b); break;
case '>':   r = (a >  b); break;
case OP_LE: r = (a <= b); break;
...
```

ゼロ除算は畳み込まない（実行時に未定義動作だが、コンパイラが落ちるよりは asm に残してリンク時/実行時の判断に任せる）。

## 2. 代数的単純化 (Algebraic simplification)

片側だけが特定の定数のときに、計算なしで簡単な形に書き換える:

| 式 | 単純化 | 根拠 |
|----|-------|------|
| `0 + x`、`x + 0` | `x` | 加法単位元 |
| `x - 0` | `x` | 減法単位元 |
| `1 * x`、`x * 1` | `x` | 乗法単位元 |
| `0 * x`、`x * 0` | `0` | 乗法零元 |
| `x / 1` | `x` | 除法単位元 |

注意点:

- `0 - x` を `-x` には**しない**（単項演算子ノードを生成する手間に対してメリットが薄い）。
- `x - x → 0` も**しない**（変数を捨てるので意味解析が必要、間違うとバグの元）。
- 浮動小数の単純化は罠が多いが、tiny-c は int しか扱わないので関係ない。

## 3. 単項演算子も対象

`-(定数)` や `!(定数)` も畳み込む:

```c
case NODE_UNARY:
    node->operand = optimize_ast(node->operand);
    if (is_int_lit(node->operand)) {
        switch (node->op) {
        case '-': return new_int_lit(-X->int_val);
        case '!': return new_int_lit(!X->int_val);
        }
    }
    return node;
```

`!0` は `1`、`!1` は `0`、`-(5)` は `-5`、など。ループ条件 `while (!0)` のような書き方が `while (1)` に化け、その後さらに ch05 の codegen が無限ループとして処理する（ただし `1` を定数として認識して条件分岐ジャンプを省く、までは tiny-c ではやらない）。

## 4. `optimize_ast` の構造

```c
Node *optimize_ast(Node *node) {
    if (!node) return NULL;
    switch (node->kind) {
    case NODE_BINARY: {
        node->lhs = optimize_ast(node->lhs);
        node->rhs = optimize_ast(node->rhs);
        /* ここで代数的単純化と定数畳み込みを試みる */
        ...
        return node;
    }
    case NODE_UNARY:
        node->operand = optimize_ast(node->operand);
        ...
        return node;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    /* ... 同様に他のノード型を再帰 ... */
    }
}
```

**要点**:

- `Node *` を受け取って `Node *` を返す。**置き換え** が起きるので戻り値が違うことがある。
- 子を **先に** 最適化する（postorder）。これで深いネストも下から畳まれる。
- 親が `lhs = optimize_ast(lhs)` のように受け取り直すパターンを徹底する（戻り値を捨てない）。
- 全てのノード型を網羅して再帰する。`switch` の `default` にはフォールバックを書いて未対応ノードを壊さないように。

## 5. 副作用に注意

代数的単純化は **「式を捨てて別の式に置き換える」** ことがある。例えば `x * 0 → 0` で、左の `x` を計算するコードが消える。

C では `x` がただの変数参照なら問題ないが、もし `x` が副作用を持つ式（関数呼び出しなど）だったら困る:

```c
f() * 0   /* C 的には f() を呼ぶべき */
```

tiny-c の代数的単純化は **副作用の有無を判定しない**。そのため `f() * 0` を `0` に化けさせると、`f()` の呼び出しが消える ── 厳密には不正な変換。

実プロダクションのコンパイラは「式の純粋性」を判定してから単純化する。tiny-c は教育用なので割り切っている（`f() * 0` のような書き方をしないと信じる）。

## 6. AST のビフォー・アフター

入力:

```c
int main() {
    int x = 7;
    return x + 0 + x * 1;
}
```

`--no-opt --dump-ast` の AST:

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          BINARY +
            IDENT x
            INT_LIT 0
          BINARY *
            IDENT x
            INT_LIT 1
```

最適化後の AST (`--dump-ast`):

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          IDENT x
          IDENT x
```

`x + 0` は `x`、`x * 1` も `x`、それらを `+` で結ぶ全体が `x + x` に縮んだ。

## 7. 次へ

AST レベルの最適化はこれで完了。次のサブ章（`02_peephole.md`）では、AST → codegen の後、**生成 asm を文字列のまま眺めて** 隣接命令を書き換える **ピープホール最適化** を見る。`pushq %rax; popq %rcx` が `movq %rax, %rcx` に化け、`ret` の後の dead code が消える ── codegen に手を入れずに、後段で asm を整形するイメージだ。
