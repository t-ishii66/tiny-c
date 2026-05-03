# 02 — 比較と論理否定の codegen

`a < b` や `!x` といった「真偽値を返す式」を、アセンブリでどう表現するか。新しい命令が3つ登場する: **`cmpl`**、**`setcc`**、**`movzbl`**。

## 1. C には bool 型がない（tiny-c も）

C の比較演算子 `<` `<=` `>` `>=` `==` `!=` の結果は **`int`** だ。真なら 1、偽なら 0。

```c
int x = (3 < 5);       // x = 1
int y = (10 == 11);    // y = 0
```

tiny-c も同じ。比較式の値は `int` で 0 または 1。式の値は **約束通り `%eax`** に入る。

論理否定 `!` も同じ。`!0` は 1、`!nonzero` は 0。

つまり「真偽値を計算する」ことは、「`%eax` に 0 か 1 を書き込む」ことに尽きる。

## 2. 比較命令 `cmpl`

x86 には2つの値を比較するための命令 `cmpl src, dst` がある。

- 動作: 内部で `dst - src` を計算する（結果は捨てる）。
- 副作用: **フラグレジスタ** に結果を反映する。
  - ZF (zero flag): 結果が 0 なら 1 (= `dst == src`)
  - SF (sign flag): 結果が負なら 1 (signed)
  - OF (overflow), CF (carry) などもセットされる

つまり「実際の引き算はしないが、引き算した **ように** フラグだけ更新する」命令。

例:

```
movl $5, %eax      # eax = 5
movl $3, %ecx      # ecx = 3
cmpl %ecx, %eax    # 内部で: eax - ecx = 5 - 3 = 2
                   #   ZF=0, SF=0  (結果は正)
```

このフラグを後続の命令が読み取ることで、「比較結果」を活用できる。

## 3. 条件代入 `setcc`

`setXX` 系の命令は、フラグの状態に応じて **8ビットのレジスタを 0 か 1 にセット** する。`XX` の部分が条件名。

| 命令 | 条件 | 真ならセット |
|------|------|------------|
| `sete`  | ZF == 1 (等しい) | `==` の結果 |
| `setne` | ZF == 0 (等しくない) | `!=` の結果 |
| `setl`  | signed less than | `<` の結果 (signed) |
| `setle` | signed less or equal | `<=` の結果 |
| `setg`  | signed greater than | `>` の結果 |
| `setge` | signed greater or equal | `>=` の結果 |

`setl` の "l" は "less"。`setle` は "less or equal"。`setg` は "greater"。

これらの命令は **8ビットレジスタにしか書けない**。32ビット `%eax` の代わりに、その下位8ビットを指す `%al` を使う:

```
cmpl %ecx, %eax    # 比較
setl %al           # %al = 1 if %eax < %ecx else 0
```

## 4. ゼロ拡張 `movzbl`

`%al` だけ更新しても、`%eax` の上位24ビットには **古いゴミ** が残っている。`%eax` 全体を 0 か 1 にするには、`%al` を `%eax` にゼロ拡張する。

```
movzbl %al, %eax   # %eax = (32-bit unsigned extension of %al)
```

`movzbl` = "**mov** **z**ero-extend **b**yte to **l**ong"。下位8ビットを 32ビットレジスタにコピーし、上位24ビットを 0 で埋める。

これで `%eax` には正確に 0 か 1 が入る。約束事「式の値は `%eax`」に対応できた。

## 5. 比較演算3点セットを並べる

`a < b` のフルパターン:

```
gen_expr(rhs)         # eax = b
pushq %rax
gen_expr(lhs)         # eax = a
popq %rcx             # ecx = b
                      # 今: eax = a, ecx = b
cmpl %ecx, %eax       # eax - ecx = a - b、フラグ更新
setl %al              # %al = 1 if a < b
movzbl %al, %eax      # %eax = 1 or 0
```

ch02 で確立した「rhs 評価 → push → lhs 評価 → pop」のパターンはそのまま。最後の演算が `addl` から `cmpl + setl + movzbl` の3命令に変わるだけ。

`<= > >= == !=` も全く同じ形で、`setl` の部分だけ別命令に置き換える。

```c
case '<':   emit_compare("setl");  return;
case OP_LE: emit_compare("setle"); return;
case '>':   emit_compare("setg");  return;
case OP_GE: emit_compare("setge"); return;
case OP_EQ: emit_compare("sete");  return;
case OP_NE: emit_compare("setne"); return;
```

`emit_compare` は3命令を出す小さなヘルパー:

```c
static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}
```

ヘルパーひとつで6つの比較演算がきれいにまとまる。

## 6. cmpl のオペランド順に注意

AT&T 構文の `cmpl src, dst` は **`dst - src`** を計算する。`addl` `subl` などと同じ順番（演算は右側のレジスタ）。

`cmpl %ecx, %eax` は `eax - ecx` を計算する。我々のコード生成では `eax = lhs`、`ecx = rhs` なので、これは `lhs - rhs` の符号でフラグが立つ。

- `lhs < rhs` のとき `lhs - rhs < 0`、フラグから `setl %al` で 1。
- `lhs > rhs` のとき `lhs - rhs > 0`、`setg %al` で 1。
- `lhs == rhs` のとき `lhs - rhs == 0`、`sete %al` で 1。

意図通り。ここを「rhs を先に評価する → ecx に rhs」というch02の規約と組み合わせると、自然に意味が合う。

逆に `cmpl %eax, %ecx` と書いてしまうと `ecx - eax`、つまり `rhs - lhs` の符号でフラグが立ち、`setl` の意味が `rhs < lhs`、つまり `lhs > rhs` になってしまう。**ch02 で確立した順序を維持するから、比較も自然に書ける**。

## 7. 論理否定 `!`

`!x` は `x == 0` と等価。実装も同じ:

```
gen_expr(operand)     # eax = x
cmpl $0, %eax         # eax - 0 のフラグ更新
sete %al              # %al = 1 if eax == 0
movzbl %al, %eax      # %eax = 1 or 0
```

`cmpl $0, %eax` は「`%eax` を 0 と比較」。続く `sete` で「等しければ 1」。

二項比較とほぼ同じパターン。違うのは右オペランドが即値 `$0` であることと、スタックの push/pop が要らないことだけ。

```c
case '!':
    fprintf(out, "  cmpl $0, %%eax\n");
    fprintf(out, "  sete %%al\n");
    fprintf(out, "  movzbl %%al, %%eax\n");
    return;
```

unary 演算子の switch に1ケース足すだけ。

## 8. 比較演算の codegen 全体（gen_expr の追加分）

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    /* ... ch03 のケース ... */
    case NODE_UNARY:
        gen_expr(node->operand);
        switch (node->op) {
        case '-':
            fprintf(out, "  negl %%eax\n");
            return;
        case '!':                                    /* 追加 */
            fprintf(out, "  cmpl $0, %%eax\n");       /* 追加 */
            fprintf(out, "  sete %%al\n");            /* 追加 */
            fprintf(out, "  movzbl %%al, %%eax\n");   /* 追加 */
            return;                                   /* 追加 */
        }
        ...
    case NODE_BINARY:
        gen_expr(node->rhs);
        fprintf(out, "  pushq %%rax\n");
        gen_expr(node->lhs);
        fprintf(out, "  popq %%rcx\n");
        switch (node->op) {
        /* ... ch02 の +-*/% ... */
        case '<':   emit_compare("setl");  return;   /* 追加 */
        case OP_LE: emit_compare("setle"); return;   /* 追加 */
        case '>':   emit_compare("setg");  return;   /* 追加 */
        case OP_GE: emit_compare("setge"); return;   /* 追加 */
        case OP_EQ: emit_compare("sete");  return;   /* 追加 */
        case OP_NE: emit_compare("setne"); return;   /* 追加 */
        }
        ...
    }
}
```

純粋に **追加だけ** で済む。

## 次へ

次のサブ章（`03_control.md`）では、その真偽値をどう **分岐** に使うか ── `je` `jmp` ジャンプ命令と **ラベル** を扱う。
