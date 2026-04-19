# 章2: 式の計算

## ゴール

```c
int main() {
    return 2 + 3 * 4;
}
```

これをコンパイルして、終了コード 14 (= 2 + 12) で終了する実行ファイルを作る。

## 核心: 木を平らにする

コンパイラの仕事の本質は、**木構造を線形な命令列に変換する**ことだ。

`2 + 3 * 4` は、パーサによって次のような AST（抽象構文木）になる:

```
    ADD
   /   \
  2    MUL
      /   \
     3     4
```

CPU は木を直接実行できない。CPU が理解するのは「1つずつ順番に実行する命令列」だけだ。では、この木をどう命令列にするか？

## スタックマシン方式

答えは単純だ。**スタックを作業台にして、木を後行順（post-order）で巡回する**。

アルゴリズム:
1. 左の子を評価する → 結果は `eax` に入る
2. `eax` をスタックに退避（`pushq`）
3. 右の子を評価する → 結果は `eax` に入る
4. スタックから左の結果を復元（`popq`）
5. 演算して結果を `eax` に入れる

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp

    # --- return 2 + 3 * 4 ---

    movl  $2, %eax          # 左の子 (2) を評価
    pushq %rax              # eax を退避

      movl  $3, %eax        #   MUL の左の子 (3)
      pushq %rax            #   退避

      movl  $4, %eax        #   MUL の右の子 (4)
      movl  %eax, %ecx      #   右 → ecx
      popq  %rax            #   左 (3) → eax
      imull %ecx, %eax      #   eax = 3 * 4 = 12

    movl  %eax, %ecx        # MUL の結果(12) → ecx
    popq  %rax              # 左 (2) → eax
    addl  %ecx, %eax        # eax = 2 + 12 = 14

    leave
    ret
```

インデントで木の深さを表現した。内側（深い子）が先に計算されている。

## 木の巡回を追いかける

`2 + 3 * 4` の AST をコード生成関数 `gen_expr` が巡回する様子:

```
gen_expr(ADD)
├─ gen_expr(2)         → movl $2, %eax
├─ pushq %rax          → 2 をスタックに退避
├─ gen_expr(MUL)
│  ├─ gen_expr(3)      → movl $3, %eax
│  ├─ pushq %rax       → 3 をスタックに退避
│  ├─ gen_expr(4)      → movl $4, %eax
│  ├─ movl %eax, %ecx  → 右(4) を ecx へ
│  ├─ popq %rax        → 左(3) を eax に復元
│  └─ imull %ecx, %eax → eax = 3 * 4 = 12
├─ movl %eax, %ecx     → 右(12) を ecx へ
├─ popq %rax           → 左(2) を eax に復元
└─ addl %ecx, %eax     → eax = 2 + 12 = 14
```

`gen_expr` は再帰関数だ。木の構造がそのまま再帰呼び出しの構造になる。

## なぜ優先順位が正しくなるのか

コードのどこにも「`*` は `+` より先に計算する」というルールは書いていない。**パーサが正しい木を作ってくれるから、gen_expr は木を辿るだけでよい。**

パーサの文法規則（bison）:

```
add_expr
    : mul_expr
    | add_expr '+' mul_expr    ← + の子として mul_expr を取る
    ;

mul_expr
    : unary_expr
    | mul_expr '*' unary_expr  ← * が先に結合する
    ;
```

この入れ子構造により、`3 * 4` が `MUL` ノードとして先に結合し、`2 + ...` がその上に来る。コード生成側は優先順位を一切気にしなくてよい。

**パーサは木の形を決め、コード生成は木を辿るだけ。** これが責務の分離だ。

## 比較演算

`<`, `==` などの比較もバイナリ演算の一種だが、CPU には「比較結果を整数にする」命令がない。代わりに2ステップで行う:

```asm
cmpl  %ecx, %eax       # 1. 比較してフラグレジスタを更新
setl  %al              # 2. フラグの状態を al (0 or 1) にセット
movzbl %al, %eax       # 3. al を 32bit に拡張
```

`setl`（set if less）、`sete`（set if equal）などの命令がフラグを 0/1 に変換する。

## コード生成の実装

`src/codegen.c` の二項演算部分:

```c
case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    gen_expr(node->lhs);          // 左を評価 → eax
    emit("pushq %%rax");          // 退避
    gen_expr(node->rhs);          // 右を評価 → eax
    emit("movl %%eax, %%ecx");    // 右 → ecx
    emit("popq %%rax");           // 左 → eax

    switch (node->kind) {
    case ND_ADD: emit("addl %%ecx, %%eax"); break;
    case ND_SUB: emit("subl %%ecx, %%eax"); break;
    case ND_MUL: emit("imull %%ecx, %%eax"); break;
    case ND_DIV:
        emit("cltd");             // eax を edx:eax に符号拡張
        emit("idivl %%ecx");      // edx:eax ÷ ecx → 商:eax, 余:edx
        break;
    ...
    }
```

5種類の演算子があるが、骨格は同じ。変わるのは最後の1命令だけだ。

## テスト

```bash
$ echo 'int main() { return 2 + 3 * 4; }' | ./tinyc /dev/stdin > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
14
```

## この章の要点

1. **コード生成の核心は「木を後行順で巡回する」こと**。再帰関数1つで実現できる
2. **スタックが作業台**。左の子の結果をスタックに退避し、右の子を評価した後に合流する
3. **優先順位はパーサの仕事**。コード生成は木の形を信じて辿るだけ

## 次の章へ

今は `return` の中に式を書くしかできない。次の章で変数を導入し、`int x = 5;` のように値に名前をつけられるようにする。
