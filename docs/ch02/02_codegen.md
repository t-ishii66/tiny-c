# 02 — スタックで中間値を管理する

`2 + 3 * 4` のように演算子が複数ある式を、レジスタ `%eax` ひとつだけで計算するにはどうするか。答えは **スタックを使う** だ。

## 1. ch01 の codegen を思い出す

ch01 の `gen_expr` は1ケースだけだった。

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    }
}
```

「式の値は最終的に `%eax` に入れて返す」── これが約束事だった。整数リテラルなら `movl $42, %eax` で終わり。

ch02 ではここに `NODE_BINARY` と `NODE_UNARY` のケースを足す。約束事は変わらない。**式を gen_expr すると、結果は `%eax` に入る**。

## 2. 二項演算で困ること

`2 + 3` をアセンブリにするとこうなる。

```
movl $2, %eax       # eax = 2
movl $3, %ecx       # ecx = 3
addl %ecx, %eax     # eax = eax + ecx = 5
```

簡単。だが、これは2つのオペランドが両方とも整数リテラルだから簡単なのだ。

`(1 + 2) + (3 + 4)` のように、両側がさらに式の場合はどうする？

- 左側 `(1 + 2)` を `%eax` で計算 → `%eax = 3`
- 右側 `(3 + 4)` を `%eax` で計算 → `%eax = 7`
- ……でも、左側の `3` はどこに行った？ `%eax` を上書きしてしまった。

「式を gen_expr すると `%eax` に入る」を守ろうとすると、別の式を計算するときに前の値が消えてしまう。レジスタが足りない。

これを解決する道具が **スタック** だ。

## 3. スタックで中間値を退避する

スタックは、関数呼び出しのたびにアドレスが減っていく特別なメモリ領域だ。`%rsp` (stack pointer) が現在の頂点を指している。CPU は `pushq` と `popq` という命令でスタックを操る。

```
pushq %rax     # rsp -= 8;  *(rsp) = rax;
popq  %rcx     # rcx = *(rsp);  rsp += 8;
```

`pushq` はレジスタの値をスタックに保存し、`popq` は取り出す。後入れ先出し（LIFO）。

これを使えば、`%eax` の値を一時的に退避してから別の計算をして、後で取り戻せる。**レジスタの数が足りなくても、スタックの深さは（事実上）無限**。

## 4. 二項演算の定石

二項演算 ── つまり **左(lhs) 演算子 右(rhs)** という形（`a + b` や `x * y`）── の生成パターンはこうなる。

```
gen_expr(rhs)         # %eax = 右オペランドの値
pushq %rax            # 右の値をスタックに退避
gen_expr(lhs)         # %eax = 左オペランドの値
popq %rcx             # 退避していた右の値を %ecx に取り出す
                      # この時点で:  %eax = 左, %ecx = 右
addl %ecx, %eax       # %eax = 左 + 右
```

ポイントは順序。**右を先に計算してスタックに退避し、左を後で計算する**。

### なぜこの順序なのか

そもそも我々がやりたいのは「**どんな二項演算子でも同じパターンで生成する**」ことだ。`+ - * / %` は意味が違うが、コード生成の枠組みは共通にしておきたい ── そうでないと演算子の種類だけ別々の処理を書くはめになる。

そのために決めるのは「**演算の直前に、左オペランドの値・右オペランドの値が、それぞれどのレジスタに入っているか**」を統一すること。tiny-c では:

- 左オペランド (lhs) → `%eax`
- 右オペランド (rhs) → `%ecx`

という配置に統一する。なぜこの配置か。式の値は約束事として `%eax` に保持される。AT&T の `subl %ecx, %eax` は `%eax = %eax - %ecx`、つまり結果が左側のレジスタ（`%eax`）に上書きされる。なので **`%eax` が lhs を保持していれば、`lhs - rhs` の結果がそのまま `%eax` に残る** ── 約束事と一致する。

可換な `+`、`*` だけ考えるなら左右は入れ替わってもいいが、非可換な `-`、`/`、`%`（あとで来る比較演算も）では順序を守らないと意味が変わる。だから配置の規約を演算子に依らず固定する。

### 実装レベル

「演算直前に `%eax = lhs`、`%ecx = rhs`」という状態を作るには、こう順序付ける:

1. 先に `rhs` を計算 → `%eax = rhs` → スタックに退避
2. 次に `lhs` を計算 → `%eax = lhs`（rhs 評価で潰されない、退避済みだから）
3. 退避していた `rhs` を `%ecx` に pop
4. ここで `%eax = lhs`、`%ecx = rhs`。あとは `addl`/`subl`/... を1行で書ける

逆順（lhs 先）にすると最終的な `%eax` に rhs が保持される状態になってしまい、追加で move 命令が要る。だから「rhs から先に計算」が定石になる。

整理。

| 命令 | 意味 (AT&T 構文) |
|------|----------------|
| `addl %ecx, %eax` | `eax = eax + ecx` |
| `subl %ecx, %eax` | `eax = eax - ecx` |
| `imull %ecx, %eax` | `eax = eax * ecx` |

加算・減算・乗算は3つとも同じパターン。命令を変えるだけ。

## 5. 入れ子の式でも壊れない

`(1 + 2) + (3 + 4)` のような場合に、このパターンが本当に効くか？

```c
gen_expr(外側のBINARY)
  // 右オペランド (3+4) を計算
  gen_expr(右BINARY)
    gen_expr(4)         → movl $4, %eax
    pushq %rax            // [4] in stack
    gen_expr(3)         → movl $3, %eax
    popq %rcx             // ecx = 4
    addl %ecx, %eax       // eax = 3+4 = 7
  pushq %rax              // [7] in stack
  // 左オペランド (1+2) を計算
  gen_expr(左BINARY)
    gen_expr(2)         → movl $2, %eax
    pushq %rax            // [2, 7] in stack
    gen_expr(1)         → movl $1, %eax
    popq %rcx             // ecx = 2;  stack: [7]
    addl %ecx, %eax       // eax = 1+2 = 3
  popq %rcx               // ecx = 7
  addl %ecx, %eax         // eax = 3+7 = 10
```

スタックは `[7]` → `[2, 7]` → `[7]` → `[]` と動いていく。

各レベルの `pushq`/`popq` がペアになっているからスタックは釣り合う。**再帰の各階層が自分の使うスタック領域を自前で確保し、自前で解放する**。

## 6. 除算と剰余だけは特殊

`+ - *` までは2オペランド命令で済むが、`/ %` だけは様子が違う。x86 の `idivl` 命令は次のような動きをする。

- ダブルワード（64ビット）を分母で割る。
- 分母（divisor）は引数で指定したレジスタ。例: `idivl %ecx`。
- 分子（dividend）は **`%edx:%eax` に固定で入れる**（`%edx` が上位32ビット、`%eax` が下位32ビット）。
- 結果の **商は `%eax`、剰余は `%edx`** に入る。

我々が扱うのは32ビット整数だが、`idivl` は64ビット ÷ 32ビットの形式しか持たない。だから、`%eax` にある32ビットの分子を、`%edx:%eax` の64ビットに広げる前処理が必要だ。

これをやるのが **`cdq`** (Convert Doubleword to Quadword) という命令。`%eax` の符号ビットを `%edx` 全体に広げる（`%eax` が正なら `%edx = 0`、負なら `%edx = -1` (= 0xFFFFFFFF)）。これで `%edx:%eax` が正しい符号付き64ビット値になる。

```
gen_expr(rhs)         # eax = 分母
pushq %rax
gen_expr(lhs)         # eax = 分子
popq %rcx             # ecx = 分母
cdq                   # eax を edx:eax に符号拡張
idivl %ecx            # eax = 分子/分母,  edx = 余り
```

剰余 `%` の場合は最後に `movl %edx, %eax` を足して、余りを `%eax` に移す。約束ごと「式の値は `%eax`」に従うためだ。

```
cdq
idivl %ecx
movl %edx, %eax       # 剰余を eax に
```

## 7. 単項マイナス

`-x` は `negl %eax` 一発で済む。

```c
case NODE_UNARY:
    gen_expr(node->operand);   // eax = operand の値
    fprintf(out, "  negl %%eax\n");   // eax = -eax
    return;
```

`negl` は2の補数で符号反転する命令。スタックは触らない。

## 8. ch02 の gen_expr 全体

3つの `case` が増えただけ。

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_UNARY:                                                    /* 追加 */
        gen_expr(node->operand);                                        /* 追加 */
        fprintf(out, "  negl %%eax\n");                                 /* 追加 */
        return;                                                         /* 追加 */
    case NODE_BINARY:                                                   /* 追加 */
        gen_expr(node->rhs);                                            /* 追加 */
        fprintf(out, "  pushq %%rax\n");                                /* 追加 */
        gen_expr(node->lhs);                                            /* 追加 */
        fprintf(out, "  popq %%rcx\n");                                 /* 追加 */
        switch (node->op) {                                             /* 追加 */
        case '+': fprintf(out, "  addl %%ecx, %%eax\n"); return;        /* 追加 */
        case '-': fprintf(out, "  subl %%ecx, %%eax\n"); return;        /* 追加 */
        case '*': fprintf(out, "  imull %%ecx, %%eax\n"); return;       /* 追加 */
        case '/':                                                       /* 追加 */
            fprintf(out, "  cdq\n");                                    /* 追加 */
            fprintf(out, "  idivl %%ecx\n");                            /* 追加 */
            return;                                                     /* 追加 */
        case '%':                                                       /* 追加 */
            fprintf(out, "  cdq\n");                                    /* 追加 */
            fprintf(out, "  idivl %%ecx\n");                            /* 追加 */
            fprintf(out, "  movl %%edx, %%eax\n");                      /* 追加 */
            return;                                                     /* 追加 */
        }
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}
```

`gen_stmt` も `codegen` 関数も ch01 から一切変えない。式の評価が増えただけで、文や関数の枠組みは同じだ。

## 9. なぜ `%rax` を push するのに 32-bit 値を扱うのか

`pushq %rax` は64ビットレジスタ全体をプッシュしている。`movl $4, %eax` で書き込むのは下位32ビット (`%eax`) だが、x86-64 では `%eax` への書き込みは **上位32ビットを自動的にゼロにクリアする**。だから `%rax` 全体としても正しい値（上位ゼロ、下位に整数値）が入っている。

スタックの単位は8バイトなので、64ビットの `pushq` を使う。32ビットだけプッシュする `pushl` は x86-64 ではそもそも使えない（オペランドサイズのルールが違う）。

## 次へ

最後の節（`03_build.md`）で、`lexer.l` `parser.y` `ast.h/c` `codegen.c` の完全形を並べ、ビルドして動かす。
