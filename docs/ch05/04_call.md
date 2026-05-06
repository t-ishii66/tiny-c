# 04 — 関数呼び出しの codegen

`f(a, b, c)` をアセンブリに翻訳する。やることは3つ。

1. 引数 `a, b, c` を評価して `%rdi, %rsi, %rdx` に積む。
2. `%rsp` を 16-aligned に整える。
3. `call f`。

簡単そうに見えて、引数の評価が **互いを壊さない** ようにする工夫と、整列の **動的な調整** が要る。

## 1. 引数評価の難しさ

ABI で決まっている引数とレジスタの対応を再掲しておく:

| 引数番号 | 64ビット | 32ビット |
|---------|---------|---------|
| 1 | `%rdi` | `%edi` |
| 2 | `%rsi` | `%esi` |
| 3 | `%rdx` | `%edx` |
| 4 | `%rcx` | `%ecx` |
| 5 | `%r8`  | `%r8d` |
| 6 | `%r9`  | `%r9d` |

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

これで「触ってはいけないレジスタ問題」が解消する。push でいったんスタックに退避してしまえば、その後どんな関数を呼ばれて `%rdi` が壊されても、最終的に popq で正しい値を取り出せる。

順序に少し工夫が要る。レジスタ順は `%rdi (1番), %rsi (2番), %rdx (3番), ...`。push を「左から」やると、最後に push した = 一番上に乗っているのは N番目の引数。pop すると N番目から取り出される。これを `%rdi` (1番) に入れるのは間違い。

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

- `stack_offset` が偶数 → 8バイトのずれが偶数個なので、`%rsp` は 16の倍数に乗っている（16-aligned）
- `stack_offset` が奇数 → 8バイト単位で奇数個ずれているので、`%rsp` は 16の倍数から 8 バイトずれている（8-misaligned）

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

ここで使う `arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"}` は、節 03 の callee 側で見た `arg_regs32[]`（`%edi`, `%esi`, ...）の **64-bit 版**だ。呼び出し側は `popq`（64-bit 命令）でスタックから取り出すので 64-bit 名、callee 側は `movl`（32-bit 命令）で int をスロットに保存するので 32-bit 名 ── という非対称になる。これで辻褄が合う理由は **節 6（引数 32ビット問題と caller / callee の食い違い）** で説明する。

ここでのキモ:

- **パディングを出すかどうかを示すフラグ `pad`** は、call 開始時の `stack_offset` で決める。
- パディングは push_args の前に出す。push_args は同数の push と pop で釣り合うから、`stack_offset` を変えない（一時的に増えて戻る）。
- call の瞬間、`stack_offset` はパディング後の偶数 → `%rsp` は 16-aligned。

## 5. 入れ子の call で動くか確認

`f(1) + f(2)` の codegen を追う（入れ子の call の典型）。コメントの `so` は `stack_offset` の略、`PAD` はセクション 4 で見たパディング（`subq $8, %rsp` で 16-align に整える操作）の略。**PAD のチェックは `CALL` に入る瞬間にだけ行う**（具体的には `gen_expr` で `NODE_CALL` 分岐に入った直後、引数を push する前） ── 他の場面（メモリロード、加減算など）では `%rsp` が一時的に misalign していても問題にならない（call を跨がないので）。

```c
gen_expr(BINARY +, lhs=f(1), rhs=f(2)):
  gen_expr(rhs = CALL f(2)):       # NODE_CALL 処理開始
    stack_offset = 0 (偶数)、pad なし
    push_args([2]):
      gen_expr(2)                # eax=2
      emit_push                  # so=1
      return 1                   # push した引数の個数
    emit_pop %rdi                # so=0
    movl $0, %eax                # variadic ABI
    call f                       # so=0 偶数 → 16-aligned ✓
  emit_push                      # so=1, f(2) の結果を保存
  gen_expr(lhs = CALL f(1)):     # NODE_CALL 処理開始
    （直前の emit_push で f(2) の結果を退避したので so=1 のまま）
    stack_offset = 1 (奇数) で CALL 処理を開始 → 後で出る call f の前に揃えるため PAD を入れる
    subq $8, %rsp                # so=2 (PAD: 後の call まで保持)
    push_args([1]):
      gen_expr(1)                # eax=1
      emit_push                  # so=3
      return 1                   # push した引数の個数
    emit_pop %rdi                # so=2
    movl $0, %eax                # variadic ABI
    call f                       # so=2 偶数 → 16-aligned ✓
    addq $8, %rsp                # so=1 (PAD 解除)
  emit_pop %rcx                  # so=0
  addl %ecx, %eax                # eax = f(1) + f(2)
```

最初の `call f(2)` は呼び出し開始時 `so=0` で素直に 16-aligned。`f(2)` の結果を `pushq` で保存する瞬間に `so=1` になり、続く `f(1)` の CALL 直前で **奇数を検出してパディング**。`call` の瞬間は確実に 16-aligned。

`stack_offset` という数字を持っているだけで、入れ子の任意の深さで正しく整列できる。

## 6. 引数 32ビット問題と caller / callee の食い違い

ここまで眺めると、面白い不一致に気付く:

- **caller** は引数に対して `pushq %rax` / `popq %rdi`（64-bit op）を使う。
- **callee**（節 03 の spill 部分）は `movl %edi, -off(%rbp)`（32-bit op）で受け取る。

64-bit で渡して 32-bit で受け取って大丈夫なのか? 大丈夫だ。これは **x86-64 の重要なルール**で辻褄が合っている:

> **32-bit レジスタ (`%eax`、`%edi` 等) に書き込むと、対応する 64-bit レジスタ (`%rax`、`%rdi` 等) の上位 32 ビットは自動的にゼロクリアされる。**

int を渡す流れを追うと:

1. caller: `movl $42, %eax` → `%rax = 0x00000000_0000002A`（上位 32 がゼロ拡張）
2. caller: `pushq %rax` → 8 バイト push（上位 4 はゼロ、下位 4 は 42）
3. caller: `popq %rdi` → `%rdi = 0x00000000_0000002A`
4. callee: `movl %edi, -8(%rbp)` → `%edi`（下位 4 バイト = 42）だけを読んでスロットに保存

callee は **`%edi` しか見ない**。上位 32 ビットがゼロなのは「結果としてそうなった」だけで、ABI 的には「整数引数の上位 32 ビットは未定義」── 呼び出し側が何を入れていても callee は無視する。だからこのやり方で完全に正しい。

整理すると:

| 値 | caller | callee の spill |
|---|---|---|
| int (4 バイト) | `pushq %rax`（上位 32 はゼロ拡張）→ `popq %rdi` | `movl %edi, -off(%rbp)` |
| pointer (8 バイト) | `pushq %rax` → `popq %rdi` | `movq %rdi, -off(%rbp)` |

ch06 でポインタが入ると callee 側で `movq` 分岐が必要になるが、ch05 では int だけなので `movl` 一択で済む。

## 7. variadic ABI への対応

`call` の直前に `movl $0, %eax`。これで `%al = 0`（XMM レジスタ使用数 = 0）になり、`printf` のような可変引数関数を呼んでも安全。可変引数でない関数にも害はないので、tiny-c では **常に出す**。

## 8. 再帰呼び出しが「自然に」動く理由

題材として階乗 `fact` を考える:

```c
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
```

`fact(5)` を呼ぶと、関数の中でさらに `fact(4)`、その中で `fact(3)`、... と自分自身を呼んでいく。各呼び出しは「自分の `n`」という独立したローカル変数を持つはずだが、それが正しく機能するのはなぜか?

各関数呼び出しは自分のプロローグで新しいフレームを積み、`%rbp` をそのフレームのベースに更新する。`-8(%rbp)` はその時の `%rbp` 基準なので、**呼び出しごとに別のメモリ位置**を指す。

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

`fact(3)` の `n` も `fact(4)` の `n` も同じ `-8(%rbp)` というアセンブリ表記だが、`%rbp` が違うので別のメモリを指す。プロローグ・エピローグと `%rbp` が ABI 通りに使われている限り、再帰は特別な仕掛けなしで動く。

## 次へ

最後の節 (`05_build.md`) で、完全形を並べ、入れ子の call (`f(1) + f(2)`) の生成アセンブリでパディングが入る瞬間を読む。
