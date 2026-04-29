# 04 — 関数呼び出しの codegen

`f(a, b, c)` をアセンブリに翻訳する。やることは3つ。

1. 引数 `a, b, c` を評価して `%rdi, %rsi, %rdx` に積む。
2. `%rsp` を 16-aligned に整える。
3. `call f`。

簡単そうに見えて、引数の評価が **互いを壊さない** ようにする工夫と、整列の **動的な調整** が要る。

## 1. 引数評価の難しさ

最初に思いつくのは「順番に評価して直接レジスタに置く」だ。

```c
gen_expr(args[0]); movl %eax, %edi
gen_expr(args[1]); movl %eax, %esi
gen_expr(args[2]); movl %eax, %edx
```

しかしこれは壊れる。`gen_expr(args[1])` の中身が **別の関数呼び出し** だったら、その呼び出しが `%rdi` を上書きしてしまう。`%rdi` は caller-saved だから、関数を呼んだら使われた可能性がある。

```c
f(g(1), 2)
```

評価:
1. `g(1)` を計算 → `%eax = g(1)`、`movl %eax, %edi`
2. `2` を計算 → `%eax = 2`、`movl %eax, %esi`
3. `call f`

ここでは問題ない。だが:

```c
f(2, g(1))
```

評価:
1. `2` を計算 → `%eax = 2`、`movl %eax, %edi` (`%rdi = 2`)
2. `g(1)` を計算 → これが `g` を呼ぶ過程で **`%rdi` を上書き** してしまう
3. `call f` ── でも `%rdi` はもう壊れている

これでは引数が正しく渡らない。

## 2. 解決策 — 全部スタックに積んでから popq

順序立ててこうする:

```
全引数を評価し、評価のたびに スタックに pushq する。
全部評価し終わったら、popq でレジスタに取り出す。
```

スタックは「触っちゃいけないレジスタ問題」と無関係。push しておけば、その後どんな関数を呼ばれても `%rdi` は気にしなくていい。

順序にちょっと工夫が要る。レジスタ順は `%rdi (1番), %rsi (2番), %rdx (3番), ...`。push を「左から」やると、最後に push した = 一番上に乗っているのは N番目の引数。pop すると N番目から取り出される。これを `%rdi` (1番) に入れるのは間違い。

正しくは: **引数を逆順に push** する。最後に push されたのが 1番目の引数になり、popq で先頭から `%rdi`、`%rsi`、... の順にきれいに収まる。

```
push args[N-1]    # 最初に N番目を push (一番下になる)
push args[N-2]
...
push args[1]
push args[0]      # 最後に 1番目を push (一番上)

popq %rdi         # 1番目 → %rdi
popq %rsi         # 2番目 → %rsi
...
popq arg_regs[N-1]
```

実装は再帰で書くと素直になる。

```c
static int push_args(NodeList *l) {
    if (!l) return 0;
    int n = push_args(l->next);   /* 末尾を先に push */
    gen_expr(l->node);
    emit_push();
    return n + 1;
}
```

NodeList の末端まで再帰してから戻りながら push するので、結果として **末尾の引数から先に積まれる**。スタックの一番上に最初の引数が乗る。

## 3. アライメント追跡 — `stack_offset`

call の前に `%rsp` を 16-aligned にする。だが、現時点で `%rsp` がどれだけずれているかを **静的に** 追跡しないといけない。

引数 push のたびに `%rsp` が 8 バイト動く。binary op の `pushq %rax` でも動く。そのたびに「アライメントが偶数か奇数か」を覚えておく必要がある。

そこで `stack_offset` という整数変数を導入する。**関数のプロローグ後を 0 とした、現在の `%rsp` の差分を 8バイト単位で表す数値**。

```c
static int stack_offset;

static void emit_push(void) {
    fprintf(out, "  pushq %%rax\n");
    stack_offset++;
}
static void emit_pop(const char *reg) {
    fprintf(out, "  popq %s\n", reg);
    stack_offset--;
}
```

関数開始時 `stack_offset = 0`。`%rsp` は 16-aligned。

- `stack_offset` が偶数 → `%rsp` は 16-aligned (`8 * 偶数 = 16N`)
- `stack_offset` が奇数 → `%rsp` は 8-misaligned (16N - 8)

## 4. call 時のパディング

`call` を出す前に、`stack_offset` が奇数（つまり `%rsp` が 8-misaligned）なら、`subq $8, %rsp` で 8 バイト下げて 16-aligned に直す。call のあとは `addq $8, %rsp` で元に戻す。

```c
case NODE_CALL: {
    int pad = (stack_offset % 2) != 0;
    if (pad) {
        fprintf(out, "  subq $8, %%rsp\n");
        stack_offset++;       /* 追跡値も更新 */
    }

    int n_args = push_args(node->args);
    if (n_args > 6) { /* ... エラー ... */ }
    for (int i = 0; i < n_args; i++)
        emit_pop(arg_regs64[i]);

    fprintf(out, "  movl $0, %%eax\n");    /* variadic ABI */
    fprintf(out, "  call %s\n", node->name);

    if (pad) {
        fprintf(out, "  addq $8, %%rsp\n");
        stack_offset--;
    }
    return;
}
```

ここでのキモ:

- pad は **call 開始時** の `stack_offset` で決める。
- パディングは push_args の前に出す。push_args は同数の push と pop で釣り合うから、`stack_offset` を変えない（一時的に増えて戻る）。
- call の瞬間、`stack_offset` はパディング後の偶数 → `%rsp` は 16-aligned。

## 5. 入れ子の call で動くか確認

`fib(n-1) + fib(n-2)` の codegen を追う（再帰の典型）。

```c
gen_expr(BINARY +, lhs=fib(n-1), rhs=fib(n-2)):
  gen_expr(rhs = CALL fib(n-2)):
    stack_offset = 0 (偶数)、pad なし
    push_args([n-2]):
      gen_expr(BINARY -, lhs=n, rhs=2):
        gen_expr(2)             # eax=2
        emit_push               # so=1
        gen_expr(n)             # eax=n
        emit_pop %rcx           # so=0
        subl                    # eax=n-2
      emit_push                 # so=1
      return 1
    emit_pop %rdi               # so=0
    movl $0, %eax; call fib     # so=0 偶数 → 16-aligned ✓
  emit_push                     # so=1, fib(n-2) の結果を保存
  gen_expr(lhs = CALL fib(n-1)):
    stack_offset = 1 (奇数)、PAD!
    subq $8, %rsp; so=2
    push_args([n-1]):
      gen_expr(BINARY -):
        ... 同様 ...
        eax=n-1
      emit_push                 # so=3
      return 1
    emit_pop %rdi               # so=2
    movl $0, %eax; call fib     # so=2 偶数 → 16-aligned ✓
    addq $8, %rsp; so=1
  emit_pop %rcx                 # so=0
  addl %ecx, %eax               # eax = fib(n-1) + fib(n-2)
```

最初の `call fib(n-2)` は素直に 16-aligned。`fib(n-2)` の結果を `pushq` で保存する瞬間に `stack_offset = 1` になり、続く `fib(n-1)` の呼び出しでは **奇数を検出してパディング**。`call` の瞬間は確実に 16-aligned。

`stack_offset` という数字を持っているだけで、入れ子の任意の深さで正しく整列できる。

## 6. 引数 32ビット問題は気にしないでよい

push_args は `pushq %rax` で 64 ビット push する。だが `%eax` (32ビット) に値を書くと、**x86-64 のルールで上位 32 ビットは自動的に 0** になる。だから 32 ビットの int を push しても、上位 32 はゼロ拡張済み。pop で `%rdi` (64ビット) に戻すと、下位 32 に正しい値、上位 32 は 0。

ABI 的には「整数引数は 32 ビットレジスタ部分 (%edi など)」を読む。上位 32 は未定義（呼び出し側が何を入れていても OK）。だから tiny-c のこのやり方で完全に正しい。

ch06 でポインタ（64 ビット値）を引数に渡せるようにするときは、`movq` で push するように調整するが、ch05 では int だけなので問題ない。

## 7. variadic ABI への対応

`call` の直前に `movl $0, %eax`。これで `%al = 0` になる。

```
movl $0, %eax     # XMM regs used = 0 (we don't pass floats)
call f
```

可変引数関数 (`printf` など) を呼ぶときは ABI 上の必要性、それ以外でも害はない。tiny-c では **常に出す**。条件分岐を省ける。

## 8. 呼び出される側のプロローグとの噛み合い

呼び出された関数 `f` は、自分のプロローグでこうする:

```
pushq %rbp                    # rsp -= 8、rsp は 16-aligned に戻る (caller が 16-aligned で call、+8 戻り番地で off、+8 pushq %rbp で揃う)
movq %rsp, %rbp
subq $aligned_frame, %rsp     # aligned_frame は 16 の倍数、整列維持
movl %edi, -8(%rbp)           # パラメータ spill
...
```

呼び出し側の整列努力 (`%rsp = 16N` at `call`) と、呼ばれる側の整列計算 (`pushq %rbp` で 16N に戻り、`subq` も 16 の倍数) が **整合** している。両者で守らないと崩れる。両者で守れば、関数本体内ずっと 16-aligned。

## 9. 再帰呼び出しが「自然に」動く理由

ここまで来たら、再帰の話はすぐ済む。

`fact(5)` を例にとる。`fact` 関数は自分自身 `fact(n-1)` を呼ぶ。これがどう動くか?

各 `fact` 呼び出しは:
1. 自分のプロローグで新しいフレームを積む（独立した `%rbp`、独立したローカル領域）。
2. `n` を `-8(%rbp)` に保存する。**この `-8(%rbp)` はそのフレーム固有の番地**。別の `fact` 呼び出しは別のフレームを持っているので、お互いの `n` は別物。
3. 自分の本体を実行。
4. 自分のエピローグで自分のフレームを片付け、戻り番地に飛ぶ。

`fact(5)` は自分のフレーム上で `n=5` を持ち、`fact(4)` を呼ぶ。`fact(4)` はさらに自分のフレームを積み、`n=4` を持ち、`fact(3)` を呼ぶ... 同じ関数 `fact` だが、**呼び出しごとにフレームが独立** だから、各 `n` は混ざらない。

```
高アドレス
 +-------------+
 | main の rbp |
 +-------------+
 | rip = main  |
 | rbp の保存  |
 | n=5         |   ← fact(5) のフレーム
 +-------------+
 | rip = fact  |
 | rbp の保存  |
 | n=4         |   ← fact(4) のフレーム
 +-------------+
 | rip = fact  |
 | rbp の保存  |
 | n=3         |   ← fact(3) のフレーム
 +-------------+
 | ...         |
低アドレス
```

各 `n` は別のスタック位置に住んでいる。`fact(3)` の `n` を読むには `%rbp` を `fact(3)` のフレームに合わせて `-8(%rbp)` を読む。エピローグで `fact(3)` が戻ると、`%rbp` は `fact(4)` のものに戻り、`-8(%rbp)` は `fact(4)` の `n` を指すようになる。

**`%rbp` は時刻によって異なるフレームを指している**。これが再帰の正体。コンパイラに特別な再帰サポートは要らない。プロローグ・エピローグが ABI 通りに書かれていて、`%rbp` がフレームベースとして使われていれば、関数を自分自身呼んでも、相互再帰しても、どんな深さでも、正しく動く。

ABI と prologue/epilogue は、再帰のためにあるようなものとも言える。

## 10. まとめ

- 引数は **逆順に push** してからレジスタに pop。call の途中で他の呼び出しが起きても壊れない。
- `stack_offset` 変数で `%rsp` のアライメントを静的追跡。call の瞬間に奇数なら `subq $8`/`addq $8` でパディング。
- `movl $0, %eax` を call の直前に常に出す（variadic ABI への保険）。
- 呼び出し側と呼ばれる側で整列規則を守れば、関数呼び出しは入れ子も再帰もタダで動く。
- 再帰は「特別」ではない。フレームが独立しているから自然に動く。

## 次へ

最後のサブ章 (`05_build.md`) で、ch04 との差分をまとめた完全形を並べ、再帰の階乗・フィボナッチを動かして、生成アセンブリを読む。
