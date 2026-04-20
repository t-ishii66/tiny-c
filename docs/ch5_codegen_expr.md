# 章5: コード生成 — 式の計算

## この章で学ぶこと

章1〜4 で、ソースコードが AST になるまでの過程を理解した。ここからは、AST を辿ってアセンブリを出力する**コード生成**（codegen）に入る。

この章では、コード生成の最も基本的な対象である**式**を扱う。

```c
int main() {
    return 2 + 3 * 4;
}
```

この式の AST は:

```
Return
  └─ ADD
      ├─ IntLit(2)
      └─ MUL
          ├─ IntLit(3)
          └─ IntLit(4)
```

この木をどう命令列に変換するか。それがこの章のテーマだ。

## 核心: 木を後行順で巡回する

CPU は木を直接実行できない。CPU が理解するのは「1つずつ順番に実行する命令列」だけだ。コード生成の仕事は、**木構造を線形な命令列に変換する**ことだ。

アルゴリズムは驚くほど単純だ。再帰関数 `gen_expr` を1つ書くだけでよい:

```
gen_expr(node):
    もし node がリテラルなら:
        値を eax にセット
    もし node が二項演算なら:
        gen_expr(左の子)       ← 再帰
        eax をスタックに退避
        gen_expr(右の子)       ← 再帰
        スタックから左の結果を復元
        演算して結果を eax に入れる
```

**木の再帰構造がそのまま関数の再帰呼び出しになる。** これがコード生成の核心だ。

## gen_expr の再帰を追いかける

`2 + 3 * 4` の AST を `gen_expr` が巡回する様子を、インデントで再帰の深さを表して示す:

```
gen_expr(ADD)
│  gen_expr(IntLit 2)        →  movl $2, %eax
│  pushq %rax                →  2 をスタックに退避
│  gen_expr(MUL)
│  │  gen_expr(IntLit 3)     →  movl $3, %eax
│  │  pushq %rax             →  3 をスタックに退避
│  │  gen_expr(IntLit 4)     →  movl $4, %eax
│  │  movl %eax, %ecx        →  右(4) → ecx
│  │  popq %rax              →  左(3) → eax
│  │  imull %ecx, %eax       →  eax = 3 * 4 = 12
│  movl %eax, %ecx           →  右(12) → ecx
│  popq %rax                 →  左(2) → eax
│  addl %ecx, %eax           →  eax = 2 + 12 = 14
```

内側の `MUL` が先に計算される。木の再帰的な巡回が、自然に正しい計算順序を生み出す。

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp

    # --- gen_expr(ADD) ---
    movl  $2, %eax          # gen_expr(IntLit 2)
    pushq %rax              # 左の結果を退避

    movl  $3, %eax          # gen_expr(IntLit 3)  ─┐
    pushq %rax              # 退避                  │ gen_expr(MUL)
    movl  $4, %eax          # gen_expr(IntLit 4)   │
    movl  %eax, %ecx        # 右 → ecx             │
    popq  %rax              # 左(3) → eax          │
    imull %ecx, %eax        # eax = 12            ─┘

    movl  %eax, %ecx        # 右(12) → ecx
    popq  %rax              # 左(2) → eax
    addl  %ecx, %eax        # eax = 14

    leave
    ret
```

## 二項演算のパターン

すべての二項演算は同じ骨格を持つ。変わるのは最後の演算命令だけだ:

```c
gen_expr(node->lhs);            // 左を評価 → eax
emit("pushq %%rax");            // 退避
gen_expr(node->rhs);            // 右を評価 → eax
emit("movl %%eax, %%ecx");      // 右 → ecx
emit("popq %%rax");             // 左 → eax
```

この後に演算命令を1つ出す:

| 演算子 | 命令 | 補足 |
|--------|------|------|
| `+` | `addl %ecx, %eax` | |
| `-` | `subl %ecx, %eax` | |
| `*` | `imull %ecx, %eax` | |
| `/` | `cltd` + `idivl %ecx` | 商が eax に入る |
| `%` | `cltd` + `idivl %ecx` | 余りが edx に入る |

除算だけ特殊だ。`idivl` は `edx:eax`（64bit）を `ecx` で割る。`cltd` は `eax` を `edx:eax` に符号拡張する前準備だ。

## 比較演算

`<`, `==` などの比較演算も二項演算の一種だが、x86 には「比較結果を整数にする」命令がない。2ステップで行う:

```asm
cmpl  %ecx, %eax       # 1. eax と ecx を比較し、フラグを更新
setl  %al              # 2. 「eax < ecx」なら al=1、でなければ al=0
movzbl %al, %eax       # 3. al(8bit) を eax(32bit) にゼロ拡張
```

`setl`（set if less）、`sete`（set if equal）などの命令がフラグを 0/1 に変換する:

| 演算子 | 命令 |
|--------|------|
| `==` | `sete` |
| `!=` | `setne` |
| `<`  | `setl` |
| `<=` | `setle` |
| `>`  | `setg` |
| `>=` | `setge` |

## 単項演算

単項演算は子が1つだけなので、スタックの退避は不要:

```c
case ND_NEG:                    // -x
    gen_expr(node->lhs);
    emit("negl %%eax");         // eax = -eax

case ND_NOT:                    // !x
    gen_expr(node->lhs);
    emit("cmpl $0, %%eax");    // eax == 0 ?
    emit("sete %%al");         // 0 なら 1、非0 なら 0
    emit("movzbl %%al, %%eax");
```

## 実装コード

`src/codegen.c` の `gen_expr` 関数（式部分の抜粋）:

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case ND_INT_LIT:
        emit("movl $%d, %%eax", node->int_val);
        break;

    case ND_CHAR_LIT:
        emit("movl $%d, %%eax", (int)(unsigned char)node->char_val);
        break;

    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
        gen_expr(node->lhs);
        emit("pushq %%rax");
        gen_expr(node->rhs);
        emit("movl %%eax, %%ecx");
        emit("popq %%rax");

        switch (node->kind) {
        case ND_ADD: emit("addl %%ecx, %%eax"); break;
        case ND_SUB: emit("subl %%ecx, %%eax"); break;
        case ND_MUL: emit("imull %%ecx, %%eax"); break;
        case ND_DIV:
            emit("cltd");
            emit("idivl %%ecx");
            break;
        case ND_MOD:
            emit("cltd");
            emit("idivl %%ecx");
            emit("movl %%edx, %%eax");
            break;
        }
        break;

    case ND_NEG:
        gen_expr(node->lhs);
        emit("negl %%eax");
        break;

    case ND_NOT:
        gen_expr(node->lhs);
        emit("cmpl $0, %%eax");
        emit("sete %%al");
        emit("movzbl %%al, %%eax");
        break;
    }
}
```

5種類の算術演算、6種類の比較演算、2種類の単項演算——すべて同じ骨格で処理できる。

## なぜ優先順位が正しくなるのか

コード生成のどこにも「`*` は `+` より先に計算する」というルールは書いていない。

- パーサの文法規則が `*` を先に結合する木を作る（章3）
- コード生成は木を辿るだけで、内側の `MUL` が外側の `ADD` より先に評価される

**パーサは木の形を決め、コード生成は木を辿るだけ。** この責務の分離がコンパイラ設計の鍵だ。

## テスト

```bash
$ echo 'int main() { return 2 + 3 * 4; }' | ./tinyc /dev/stdin > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
14
```

## この章の要点

1. コード生成の核心は**再帰関数 `gen_expr`** 。木の構造がそのまま再帰呼び出しの構造になる
2. 二項演算は「左を評価 → 退避 → 右を評価 → 演算」のパターン。**スタックが中間結果の作業台**になる
3. 演算子の優先順位はパーサが決める。コード生成は木を信じて辿るだけ

## 次の章へ

今はリテラルの計算しかできない。次の章で変数を導入し、値に名前をつける仕組みを実装する。
