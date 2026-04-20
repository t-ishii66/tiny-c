# 章7: コード生成 — 制御構造

## この章で学ぶこと

```c
int main() {
    int i = 0;
    while (i < 3) {
        i = i + 1;
    }
    return i;
}
```

`if` と `while` のコード生成を見る。CPU にとって `if` も `while` も存在しない。あるのは**ジャンプ命令**だけだ。

## 核心: 構造化プログラミングの正体はジャンプ

CPU が知っている制御の仕組みは2つしかない:

| 命令 | 意味 |
|------|------|
| `jmp .L0` | 無条件に `.L0` へ飛ぶ |
| `je .L0` | 直前の比較結果が「等しい」なら `.L0` へ飛ぶ |

`if`、`while`、`for`、`switch`——どんな制御構造も、この2つの組み合わせに帰着する。

## if 文

### else なし

```c
if (x > 5) {
    return 1;
}
```

変換パターン:

```asm
    <条件式を計算>           # 結果は eax に 0 or 1
    cmpl  $0, %eax          # eax == 0 ?
    je    .L0               # 偽(0)ならスキップ
    <then 節のコード>
.L0:                         # ← スキップ先
```

フロー:

```
    ┌──────────┐
    │ 条件を評価 │
    └────┬─────┘
     真 │     │ 偽
    ┌───┘     └──→─┐
    ▼               │
 ┌──────┐           │
 │ then │           │
 └──┬───┘           │
    └───────┬───────┘
            ▼
         (.L0)
```

### else あり

```c
if (x > 5) {
    return 1;
} else {
    return 0;
}
```

```asm
    <条件式を計算>
    cmpl  $0, %eax
    je    .L0               # 偽なら else へ
    <then 節のコード>
    jmp   .L1               # else を飛ばす
.L0:                         # ← else の入口
    <else 節のコード>
.L1:                         # ← 合流地点
```

フロー:

```
        ┌──────────┐
        │ 条件を評価 │
        └────┬─────┘
         真 │     │ 偽
        ┌───┘     └───┐
        ▼             ▼
   ┌─────────┐   ┌─────────┐
   │ then 節  │   │ else 節  │
   └────┬────┘   └────┬────┘
        └──────┬──────┘
               ▼
            (.L1)
```

生成されるアセンブリの実例（`if (x > 5) { return 1; } else { return 0; }`）:

```asm
    # 条件: x > 5
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax          # eax = x
    pushq %rax
    movl  $5, %eax
    movl  %eax, %ecx
    popq  %rax
    cmpl  %ecx, %eax            # x と 5 を比較
    setg  %al                   # x > 5 → al = 1
    movzbl %al, %eax

    cmpl  $0, %eax              # 結果が 0 (偽) か？
    je    .L0                   # 偽なら .L0 (else) へ

    # then
    movl  $1, %eax
    leave
    ret

    jmp   .L1                   # else を飛ばす
.L0:
    # else
    movl  $0, %eax
    leave
    ret
.L1:
```

## while 文

```c
while (i < 3) {
    i = i + 1;
}
```

`while` は `if` + 逆方向の `jmp` で作れる:

```asm
.L0:                         # ← ループ先頭
    <条件式を計算>
    cmpl  $0, %eax
    je    .L1               # 偽ならループ脱出
    <本体のコード>
    jmp   .L0               # ← 先頭に戻る
.L1:                         # ← ループ脱出先
```

フロー:

```
        ┌──→ ┌──────────┐
        │    │ 条件を評価 │
        │    └────┬─────┘
        │     真 │     │ 偽
        │    ┌───┘     └───→ (.L1)
        │    ▼
        │ ┌─────────┐
        │ │  本体    │
        │ └────┬────┘
        └──────┘ (jmp .L0)
```

`jmp .L0` が先頭に戻ることでループが生まれる。条件が偽になれば `je .L1` で脱出する。

生成されるアセンブリの実例（`while (i < 3) { i = i + 1; }`）:

```asm
.L0:                            # ループ先頭
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax          # eax = i
    pushq %rax
    movl  $3, %eax
    movl  %eax, %ecx
    popq  %rax
    cmpl  %ecx, %eax            # i < 3 ?
    setl  %al
    movzbl %al, %eax

    cmpl  $0, %eax
    je    .L1                   # 偽(i >= 3)なら脱出

    # i = i + 1
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax
    pushq %rax
    movl  $1, %eax
    movl  %eax, %ecx
    popq  %rax
    addl  %ecx, %eax            # eax = i + 1
    pushq %rax
    leaq  -8(%rbp), %rax
    movq  %rax, %rcx
    popq  %rax
    movl  %eax, (%rcx)          # i = eax

    jmp   .L0                   # 先頭に戻る
.L1:                            # ループ脱出先
```

## ラベル生成

ラベル名は `.L0`, `.L1`, `.L2`, ... と連番で生成する。衝突しなければ何でもよい:

```c
static int label_count = 0;

static int new_label(void) {
    return label_count++;
}
```

## 実装コード

`gen_stmt` の if/while 部分:

```c
case ND_IF: {
    int lelse = new_label();
    int lend  = new_label();
    gen_expr(node->lhs);                 // 条件式を評価 → eax
    emit("cmpl $0, %%eax");
    if (node->extra) {                   // else 節あり
        emit("je .L%d", lelse);          //   偽なら else へ
        gen_stmt(node->rhs);             //   then 節
        emit("jmp .L%d", lend);          //   else を飛ばす
        emit_label(".L%d", lelse);
        gen_stmt(node->extra);           //   else 節
        emit_label(".L%d", lend);
    } else {                             // else 節なし
        emit("je .L%d", lend);           //   偽なら末尾へ
        gen_stmt(node->rhs);             //   then 節
        emit_label(".L%d", lend);
    }
    break;
}

case ND_WHILE: {
    int lbegin = new_label();
    int lend   = new_label();
    emit_label(".L%d", lbegin);          // ループ先頭
    gen_expr(node->lhs);                 // 条件式を評価
    emit("cmpl $0, %%eax");
    emit("je .L%d", lend);              // 偽なら脱出
    gen_stmt(node->rhs);                // 本体
    emit("jmp .L%d", lbegin);           // 先頭に戻る
    emit_label(".L%d", lend);           // 脱出先
    break;
}
```

if と while を合わせて20行ほどだ。ラベル2つとジャンプ2つ、それだけで制御構造が実現できる。

## for は while で代替できる

tiny-c に `for` 文がないのは、`while` で同じことが書けるからだ:

```c
// for (int i = 0; i < 10; i++) { body; }
// ↓ 等価
int i = 0;
while (i < 10) {
    // body
    i = i + 1;
}
```

コンパイラの実装から見れば、`for` は `while` の構文糖衣にすぎない。

## この章の要点

1. **if = 条件ジャンプ + ラベル**。else があれば無条件ジャンプも加わる
2. **while = ラベル + 条件ジャンプ + 無条件ジャンプ**。先頭に戻る `jmp` がループを作る
3. すべての制御構造は**ラベルとジャンプの組み合わせ**に帰着する。CPU にとって `if` も `while` も存在しない

## 次の章へ

ここまでで式、変数、制御構造を実装した。しかしすべてが `main` の中にある。次の章で関数を導入し、コードを分割・再利用できるようにする。
