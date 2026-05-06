# 03 — スタックフレームの再設計

ABI の整列規則を満たすため、ch04 までの lazy なフレーム確保を**事前確保 (pre-pass)** に変える。

## 1. ch04 までの方式とその問題

ch03 と ch04 では、変数宣言ごとに `subq $8, %rsp` をその場で出していた。

```c
int x = 1;     /* subq $8, %rsp; movl $1, %eax; movl %eax, -8(%rbp) */
int y = 2;     /* subq $8, %rsp; movl $2, %eax; movl %eax, -16(%rbp) */
```

これだと変数1個につき `%rsp` が 8 ずつ動く。1個のときは 8-misaligned、2個で 16-aligned、3個で 8-misaligned... と整列状態が変わる。

ch04 まではこれで動いていた。なぜなら **call をしなかった** から。call さえしなければ、`%rsp` の 16-aligned 制約は問題にならない。

ch05 では call が入る。call の前に必ず `%rsp` が 16-aligned でなければならない。**いつ call が来るかは事前にはわからない**（変数宣言の途中、入れ子の if の中、再帰の最中、いろいろ）。だから lazy 方式は通用しない。

## 2. 解決策 — 事前確保

関数全体を見渡して、**ローカル変数とパラメータが何個あるか** を数え、その合計を `subq` で1回確保する。

```
プロローグ:
  pushq %rbp
  movq %rsp, %rbp
  subq $aligned_frame_size, %rsp     # ← 16 の倍数!
```

`aligned_frame_size` は変数の合計サイズを 16 の倍数に切り上げた値。これで:

- `pushq %rbp` 後: 16-aligned
- `subq $aligned_frame_size, %rsp` 後: 16-aligned (subq した量も 16 の倍数)
- 関数本体に入った直後: 16-aligned ✓

各 `var_decl` は **`subq` を出さなくていい** ── 場所はすでに確保済み。初期化式を評価して、変数のスロットに値を `movl` で書き込むだけになる（例: `int x = 5;` なら `movl $5, %eax; movl %eax, -8(%rbp)` のみ）。

## 3. 二相 codegen — pre-pass で構造を理解する

事前確保のためには、関数本体に入る前に「変数が何個あるか」を知る必要がある。これは **AST を一度走査** すればわかる。

実装では2つの段階に分ける。

### Phase 1: 名前を集める (collect_locals)

関数のパラメータと、本体内のすべての `var_decl` の名前をシンボルテーブルに登録する。コードは **何も出力しない**。各変数にオフセットを割り当てるだけ。

```c
static void collect_locals(Node *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_VAR_DECL:
        add_local(node->name);    /* シンボルテーブルに登録、オフセットを割り当て */
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            collect_locals(l->node);
        break;
    case NODE_IF:
        collect_locals(node->then_body);
        collect_locals(node->else_body);
        break;
    case NODE_WHILE:
        collect_locals(node->body);
        break;
    default:
        break;     /* 式やその他はローカル宣言を含まない */
    }
}
```

`NODE_BLOCK`、`NODE_IF`、`NODE_WHILE` の子も含めて再帰的に走査する。同じ関数内の **どの位置にあっても** すべての `var_decl` を集める。

`add_local` 自体は ch03 と同じく、シンボルテーブル（連結リスト）にエントリを追加する。同時に **グローバル変数 `frame_size` を 8 だけ増やす** ので、走査が終わる頃には `frame_size` がこの関数のローカル領域の合計サイズを示している。

### Phase 2: コードを出す (gen_func)

シンボルテーブルとフレームサイズが揃ったら、プロローグを出して本体を生成する。

```c
static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    stack_offset = 0;

    /* Phase 1: パラメータと変数を登録 */
    for (NodeList *l = fn->params; l; l = l->next)
        add_local(l->node->name);
    collect_locals(fn->body);

    /* 16 の倍数に切り上げ */
    int aligned = (frame_size + 15) & ~15;

    /* Phase 2: プロローグ */
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    if (aligned > 0)
        fprintf(out, "  subq $%d, %%rsp\n", aligned);

    /* パラメータをスタックスロットに退避 */
    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        int off = find_local(l->node->name);
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);
        i++;
    }

    /* Phase 3: 本体 */
    gen_stmt(fn->body);

    /* 暗黙の return 0 (落っこち防止) */
    fprintf(out, "  movl $0, %%eax\n");
    fprintf(out, "  leave\n");
    fprintf(out, "  ret\n");
}
```

切り上げ式 `(frame_size + 15) & ~15` は、`frame_size` を 16 の倍数に丸める常用テクニック。`& ~15` で下位 4 ビットを 0 にする。

補足:

- `arg_regs32` は引数渡し用レジスタの 32 ビット版を並べた配列 `{"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"}`（定義は `05_build.md` の codegen.c 全体にある）。`i` 番目の引数を `arg_regs32[i]` で取り出す。
- シンボルテーブルのリンクリスト `locals` は明示的には解放しない。`gen_func` 冒頭の `locals = NULL` で前の関数のテーブルへの参照を捨て、ノード本体はプロセス終了時に OS が回収する。生成コード側のスタック領域は、エピローグの `leave` で自動的に解放される。

### NODE_VAR_DECL の codegen

事前確保によって、var_decl の処理は **値の書き込みだけ** になる。`subq` は出さない。

```c
case NODE_VAR_DECL: {
    int off = find_local(node->name);   /* 変更: ch03 は add_local + subq $8 だった */
    gen_expr(node->expr);
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
```

`add_local` ではなく `find_local` を使う。Phase 1 で全変数を登録済みなので **名前の重複は起きない設計** であり、ローカル領域の確保もプロローグの `subq` で **すでに済んでいる**。

## 4. パラメータの spill

レジスタ渡しの引数は、関数の本体に入った瞬間 **`%rdi, %rsi, ...`** に乗っている。だが、これらは caller-saved レジスタ ── 関数本体内で別の関数を呼ぶと **壊される**。

そこで、関数の最初に **パラメータの値をスタックスロットにコピー** しておく。これを **spill** と呼ぶ。

```
movl %edi, -8(%rbp)      # 1番目の引数を -8(%rbp) のスロットに保存
movl %esi, -16(%rbp)     # 2番目の引数を -16(%rbp) に
...
```

以降、関数本体は引数を **ローカル変数と同様に** `-8(%rbp)` などからアクセスする。`%edi` などのレジスタ自体は気にしなくていい。再帰呼び出しで上書きされても、こっちのスロットは安全。

シンボルテーブル上は、パラメータも普通の `LVar` として登録される。コード生成時に `IDENT a` を見たら `find_local("a")` でオフセット（例えば `8`）を取得し、`movl -8(%rbp), %eax` のように load する ── 普通のローカル変数と全く同じ扱い。

## 5. フレームのレイアウト

ある関数 `int fact(int n) { ... 中で他の変数や呼び出し ... }` のフレームレイアウトはこうなる。

```
高アドレス
 +---------------+
 | 戻り番地       |  ← caller の call が積んだ
 +---------------+
 | 旧 %rbp       |  ← 自分の pushq %rbp が積んだ。続く movq %rsp, %rbp で %rbp はここを指す
 +---------------+
 | n (= %edi)    |  -8(%rbp)   ← パラメータ spill
 +---------------+
 | (使われない)   |  -16(%rbp)  ← 16-aligned のためのパディング。subq $16 で %rsp はここを指す
 +---------------+
 |               |
 | (一時値の     |
 |  push/pop で  |
 |  伸び縮み)    |
 |               |
低アドレス
```

`fact` のフレームサイズは `n` 1個で 8バイトだが、16 の倍数に切り上げて 16バイト確保する。1スロットは無駄になるが、整列のため。

ローカル変数 0個でパラメータ 0個の関数（`int main() { return fact(5); }`）の場合、`frame_size = 0`、`aligned = 0`、つまり `subq` を **出さない**。プロローグが `pushq %rbp; movq %rsp, %rbp` だけになる。

## 6. 「変数を再利用しない」の限界

この事前確保方式は ch03 と同じくフラットなシンボルテーブルで通すので、ch05 時点では別ブロックで同名 `int y = ...;` を再宣言するとエラーになる。**ch06 でブロックスコープを導入** すると、別ブロックの同名宣言が許されるようになる（フレームサイズは累積、スロット再利用なし ── append-only リスト + active フラグで実装）。実プロダクションのコンパイラはさらにスロット再利用までやるが、tiny-c はそこまで踏み込まない。

## 次へ

次の節 (`04_call.md`) では **呼び出す側** の codegen を見る。引数を整え、整列を保ち、`call` する ── `stack_offset` というアライメント追跡変数を導入する。
