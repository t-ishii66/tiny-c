# 章4: 制御構造

## ゴール

```c
int main() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

0 + 1 + 2 + ... + 9 = 45 を計算する。

## 核心: 構造化プログラミングの正体はジャンプ

CPU が知っている制御構造は、じつは1つしかない。**ジャンプ**だ。

- 無条件ジャンプ: `jmp` — 指定アドレスに飛ぶ
- 条件ジャンプ: `je`（等しければ飛ぶ）、`jne`（等しくなければ飛ぶ）、etc.

`if`、`while`、`for`、`switch` ——どんな制御構造も、最終的にはこの2つの組み合わせになる。

## if 文

```c
if (x > 5) {
    return 1;
} else {
    return 0;
}
```

これは次のように変換される:

```
    <x > 5 を計算>
    cmpl $0, %eax        # 結果が 0 (偽) かチェック
    je   .L_else         # 偽なら else へジャンプ
    <then 節>
    jmp  .L_end          # else を飛ばす
.L_else:
    <else 節>
.L_end:
```

フローチャートで見ると:

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
          (続き)
```

生成されるアセンブリ:

```asm
    # if (x > 5)
    leaq  -8(%rbp), %rax
    movl  (%rax), %eax          # eax = x
    pushq %rax
    movl  $5, %eax              # eax = 5
    movl  %eax, %ecx
    popq  %rax                  # eax = x
    cmpl  %ecx, %eax            # x と 5 を比較
    setg  %al                   # x > 5 なら al = 1
    movzbl %al, %eax

    cmpl  $0, %eax              # 結果は 0 (偽) か？
    je    .L0                   # 偽なら .L0 (else) へ

    # then: return 1
    movl  $1, %eax
    leave
    ret

    jmp   .L1                   # else を飛ばす
.L0:
    # else: return 0
    movl  $0, %eax
    leave
    ret
.L1:
```

## while 文

```c
while (i < 10) {
    // body
}
```

`while` は `if` + `jmp` で作れる:

```
.L_begin:
    <条件を計算>
    cmpl $0, %eax
    je   .L_end          # 偽ならループ脱出
    <本体>
    jmp  .L_begin        # 先頭に戻る
.L_end:
```

フローチャート:

```
        ┌──→ ┌──────────┐
        │    │ 条件を評価 │
        │    └────┬─────┘
        │     真 │     │ 偽
        │    ┌───┘     └───→ (ループの後)
        │    ▼
        │ ┌─────────┐
        │ │  本体    │
        │ └────┬────┘
        └──────┘
```

生成されるアセンブリ:

```asm
.L0:                            # ← ループ先頭
    # i < 10
    leaq  -16(%rbp), %rax
    movl  (%rax), %eax          # eax = i
    pushq %rax
    movl  $10, %eax
    movl  %eax, %ecx
    popq  %rax
    cmpl  %ecx, %eax            # i と 10 を比較
    setl  %al
    movzbl %al, %eax

    cmpl  $0, %eax
    je    .L1                   # 偽(i >= 10)なら脱出

    # sum = sum + i;
    # ... (代入のコード)

    # i = i + 1;
    # ... (代入のコード)

    jmp   .L0                   # ← 先頭に戻る
.L1:                            # ← ループ脱出先
```

## ラベル生成

ラベルは `.L0`, `.L1`, `.L2`, ... と連番で生成する。衝突しなければ何でもよい:

```c
static int label_count = 0;

static int new_label(void) {
    return label_count++;
}
```

## コード生成の実装

`gen_stmt` の if/while 部分:

```c
case ND_IF: {
    int lelse = new_label();
    int lend  = new_label();
    gen_expr(node->lhs);               // 条件を評価
    emit("cmpl $0, %%eax");            // 偽か？
    if (node->extra) {                  // else 節あり
        emit("je .L%d", lelse);
        gen_stmt(node->rhs);           // then 節
        emit("jmp .L%d", lend);
        emit_label(".L%d", lelse);
        gen_stmt(node->extra);         // else 節
        emit_label(".L%d", lend);
    } else {                            // else 節なし
        emit("je .L%d", lend);
        gen_stmt(node->rhs);
        emit_label(".L%d", lend);
    }
    break;
}

case ND_WHILE: {
    int lbegin = new_label();
    int lend   = new_label();
    emit_label(".L%d", lbegin);        // ループ先頭
    gen_expr(node->lhs);              // 条件を評価
    emit("cmpl $0, %%eax");
    emit("je .L%d", lend);            // 偽なら脱出
    gen_stmt(node->rhs);              // 本体
    emit("jmp .L%d", lbegin);         // 先頭に戻る
    emit_label(".L%d", lend);         // 脱出先
    break;
}
```

if と while 、合わせても20行ほどだ。ラベル2つとジャンプ2つ、それだけで制御構造が実現できる。

## for はいらない

tiny-c には `for` 文がない。なぜなら、`while` で同じことが書けるからだ:

```c
// for (int i = 0; i < 10; i++) { body; }
// ↓ 等価
int i = 0;
while (i < 10) {
    // body
    i = i + 1;
}
```

コンパイラの実装から見ると、`for` は `while` の構文糖衣にすぎない。

## テスト

```bash
$ ./tinyc test/ch3_while.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s && /tmp/test; echo $?
45
```

## この章の要点

1. **if = 条件ジャンプ + ラベル**。else があれば無条件ジャンプも加わる
2. **while = ラベル + 条件ジャンプ + 無条件ジャンプ**。先頭に戻るジャンプがループを作る
3. どんな制御構造も **ラベルとジャンプの組み合わせ** に帰着する。CPU にとって `if` も `while` も存在しない

## 次の章へ

ここまでで式、変数、制御構造を実装した。次の章では関数を導入し、コードを分割して再利用できるようにする。
