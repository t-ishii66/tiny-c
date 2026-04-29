# 03 — if と while の codegen

「CPU は条件ジャンプしか知らない」── これがこの章で一番伝えたいこと。

C で書かれた `if` も `while` も、最終的には次の2種類だけに翻訳される。

- **無条件ジャンプ** (`jmp`): いつでも別の場所に飛ぶ。
- **条件ジャンプ** (`je`, `jne`, `jl`, ...): フラグの状態に応じて飛ぶ／飛ばない。

`for`、`do-while`、`switch`、`?:` ── どんな高級な制御構造もすべて、ジャンプとラベルの組み合わせに分解される。コンパイラの仕事は「`if` だの `while` だの という言葉」を「ジャンプとラベル」に翻訳することにある。

## 1. ラベルとジャンプ

**ラベル** はアセンブリ上の「飛び先の名前」だ。

```
.Lhello:           # ここがラベル「.Lhello」
   ...命令...
   jmp .Lhello     # .Lhello に無条件で飛ぶ
```

`.Lhello:` の行は命令ではなく「ここに名前をつけた」だけ。`jmp .Lhello` は CPU の `%rip`（命令ポインタ）をその位置に書き換える命令。

`.L` で始まる名前は **ローカルラベル**（リンカに見せない、ファイル内で完結する目印）の慣例。これに番号を足して、関数内でユニークにする。

```c
/* codegen.c */
static int label_count;
static int new_label(void) { return label_count++; }
```

`new_label()` を呼ぶたびにユニークな整数が返る。これを `.Lelse_3` `.Lendif_3` のようにラベル名に組み込めば、衝突しない。

## 2. 条件ジャンプの中で一番使うやつ — `je`

`je` (jump if equal) は **ZF (zero flag) が立っているとき** にジャンプする命令。`cmpl` や `testl` の直後に置くと、「等しかった」または「結果がゼロだった」場合だけ飛ぶ。

```
cmpl $0, %eax       # %eax を 0 と比較。等しければ ZF=1
je   .Lzero         # ZF が立っていれば .Lzero へ
```

これだけで「`%eax` が 0 ならジャンプ」が表現できる。

我々の codegen では、比較式や条件式の結果は **`%eax` に 0 か 1** で入っている。`if (cond)` の意味は「`cond` が 真（非ゼロ）なら then 部、偽（0）なら else 部」だから:

- 0 のときに「他の場所へ飛ぶ」 = `cmpl $0, %eax; je 飛び先`

これだけで if が始まる。

## 3. if (else なし) の翻訳

```c
if (cond)
    body;
```

を翻訳するとこうなる:

```
   gen_expr(cond)         # eax = cond の値
   cmpl $0, %eax           # 0 と比較
   je   .Lendif_N           # cond が 0 なら body をスキップ
   gen_stmt(body)
.Lendif_N:
```

`cond` が 0 なら `je` で `.Lendif_N` に飛んで body を飛ばす。0 でなければ `je` は素通りして body を実行し、自然に `.Lendif_N:` に到達する。

ラベル番号 `N` はこの if のためだけに発行する。複数の if が同じ関数にあっても、それぞれ別の番号を持つから衝突しない。

## 4. if-else の翻訳

```c
if (cond)
    then_body;
else
    else_body;
```

翻訳:

```
   gen_expr(cond)
   cmpl $0, %eax
   je   .Lelse_N             # 偽なら else へ
   gen_stmt(then_body)
   jmp  .Lendif_N             # then の後は end へ
.Lelse_N:
   gen_stmt(else_body)
.Lendif_N:
```

then を実行したら、必ず `jmp .Lendif_N` で「else を飛ばす」必要がある。これがないと、then の直後にある else の本体まで連続実行してしまう。

ラベルは2つ: `.Lelse_N` と `.Lendif_N`。同じ番号 `N` を使うので、一つの if 文に属する2ラベルが視覚的に対応する。

実装:

```c
case NODE_IF: {
    int n = new_label();
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    if (node->else_body) {
        fprintf(out, "  je .Lelse_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, "  jmp .Lendif_%d\n", n);
        fprintf(out, ".Lelse_%d:\n", n);
        gen_stmt(node->else_body);
        fprintf(out, ".Lendif_%d:\n", n);
    } else {
        fprintf(out, "  je .Lendif_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, ".Lendif_%d:\n", n);
    }
    return;
}
```

`else` 部の有無で形を変える。`else` なしのときは `.Lelse_N` を使わず、直接 `.Lendif_N` に飛ぶ。

## 5. while の翻訳

```c
while (cond)
    body;
```

翻訳:

```
.Lbegin_N:
   gen_expr(cond)
   cmpl $0, %eax
   je   .Lendwhile_N          # 偽なら脱出
   gen_stmt(body)
   jmp  .Lbegin_N             # 本体を実行したら戻って再度判定
.Lendwhile_N:
```

ループの構造は3つの要素でできている:

1. **`.Lbegin_N`**: ループの先頭。条件チェック前。
2. **`.Lendwhile_N`**: ループの脱出先。条件が偽になったらここに飛ぶ。
3. **`jmp .Lbegin_N`**: 本体の最後で、先頭に戻る。

無条件ジャンプ `jmp` で「上に戻る」のがループの本質。条件ジャンプは「脱出のため」の道具にすぎない。

実装:

```c
case NODE_WHILE: {
    int n = new_label();
    fprintf(out, ".Lbegin_%d:\n", n);
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    fprintf(out, "  je .Lendwhile_%d\n", n);
    gen_stmt(node->body);
    fprintf(out, "  jmp .Lbegin_%d\n", n);
    fprintf(out, ".Lendwhile_%d:\n", n);
    return;
}
```

## 6. 入れ子になっても壊れない

if の中に while を入れても、while の中に if を入れても、ラベル番号が **`new_label()` で毎回ユニーク** だから衝突しない。

```c
while (i < 10) {
    if (i == 5) {
        x = 100;
    }
    i = i + 1;
}
```

から生成されるアセンブリは大体こんな構造になる。

```
.Lbegin_0:               # while の先頭
    ; ... cond 評価 ...
    je .Lendwhile_0
    ; ... if の cond 評価 ...
    cmpl $0, %eax
    je .Lendif_1         # ← if 用に new_label() で 1 が発行された
    ; ... x = 100 ...
.Lendif_1:               # ← 同じ 1
    ; ... i = i + 1 ...
    jmp .Lbegin_0
.Lendwhile_0:            # ← while 用の 0
```

外側 while の `0` と、内側 if の `1`。**入れ子の各構造が独自のラベル番号空間を持つ**ので、自然に正しく動く。

## 7. 階乗の例を最後まで追ってみる

入力:

```c
int main() {
    int n = 5;
    int result = 1;
    while (n > 1) {
        result = result * n;
        n = n - 1;
    }
    return result;
}
```

生成アセンブリ（コメント付きで読みやすくしたもの）:

```
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $8, %rsp                     # int n
  movl $5, %eax
  movl %eax, -8(%rbp)               # n = 5
  subq $8, %rsp                     # int result
  movl $1, %eax
  movl %eax, -16(%rbp)              # result = 1

.Lbegin_0:                          # while 先頭
  movl $1, %eax                     # rhs (1) を先に
  pushq %rax
  movl -8(%rbp), %eax               # lhs (n) を後に
  popq %rcx
  cmpl %ecx, %eax                   # n - 1 でフラグ
  setg %al                          # n > 1 ?
  movzbl %al, %eax
  cmpl $0, %eax                     # 結果が 0 なら脱出
  je .Lendwhile_0

  movl -8(%rbp), %eax               # rhs (n) を先
  pushq %rax
  movl -16(%rbp), %eax              # lhs (result) を後
  popq %rcx
  imull %ecx, %eax                  # result * n
  movl %eax, -16(%rbp)              # result = ...

  movl $1, %eax
  pushq %rax
  movl -8(%rbp), %eax
  popq %rcx
  subl %ecx, %eax                   # n - 1
  movl %eax, -8(%rbp)

  jmp .Lbegin_0                     # ループ先頭へ
.Lendwhile_0:

  movl -16(%rbp), %eax              # return result
  leave
  ret
```

while のループ先頭 `.Lbegin_0` から、本体の最後の `jmp .Lbegin_0` で戻ってくる。条件 `n > 1` が偽になると `je` で `.Lendwhile_0` に脱出し、`return result` に進む。

**5回ループする**: n = 5 → 4 → 3 → 2 → 1 と減り、`1 > 1` が偽になって脱出。result は `1 * 5 * 4 * 3 * 2 = 120` になる。

## 8. 「すべてはジャンプ」を実感する

ここで一度立ち止まって眺めてみよう。

| C のかたち | アセンブリの骨格 |
|-----------|----------------|
| `if (cond) body` | `cond評価; je 終;  body; 終:` |
| `if (cond) A else B` | `cond評価; je else;  A; jmp 終; else: B; 終:` |
| `while (cond) body` | `先頭: cond評価; je 終;  body; jmp 先頭; 終:` |

すべて「ラベル + ジャンプ」だけで構成される。CPU 視点では、`if` も `while` も特別なものではない ── ただの「条件付きで命令ポインタを書き換える」操作の組み合わせ。

将来 `for` や `break`、`continue`、`switch` を加えるとしても、本質は同じ。`break` は「ループの終了ラベルへの jmp」、`continue` は「ループ先頭ラベルへの jmp」、`switch` は「複数の `cmp + je` を並べたもの」。

ジャンプとラベルさえあれば、構造化プログラミングのほぼすべては表現できる。逆にいうと、**高級言語の構造はすべて、低レベルの jmp/cmp に **「翻訳」** されるだけ**だ。これがコンパイラの基本姿勢。

## 9. まとめ

- ラベル `.LXXX_N` は関数内でユニークな名前。`new_label()` で番号発行。
- 条件分岐は `cmpl $0, %eax; je 飛び先` の組み合わせ。「結果が 0 なら飛ぶ」。
- if-else は2ラベル (`.Lelse_N`、`.Lendif_N`)、while は2ラベル (`.Lbegin_N`、`.Lendwhile_N`)。
- 本体内に再び if/while を入れ子にしても、ラベル番号が独立に発行されるから衝突しない。
- すべての制御構造は **ラベル + ジャンプ** に分解される ── これが CPU の唯一の制御手段。

## 次へ

最後のサブ章（`04_build.md`）で、ch04 の全ファイルの完全形を ch03 との差分とともに並べ、ビルドして動かす。階乗計算と if-else の例で生成アセンブリを追って締めくくる。
