# 02 — スタックフレームとシンボルテーブル

変数 `x` `y` を、CPU が読める **メモリ番地** に変換する。それがこの章の codegen の中身だ。

新しく登場する道具は2つ。

- **スタックフレーム** — 関数ごとの局所メモリ領域。`%rbp` を基準にしたアドレスで管理する。
- **シンボルテーブル** — 名前 → オフセット の変換表。codegen 内でだけ使う。

## 1. スタックフレームの復習

ch01 で書いた関数のプロローグはこうだった。

```
pushq %rbp           # 呼び出し元の %rbp をスタックに退避
movq %rsp, %rbp      # 現在の %rsp を %rbp にコピー
```

このたった2行が「関数のスタックフレームの土台」を作っている。`%rbp` (base pointer) は、この関数のローカル領域の **基準点** を指すようになった。

そして、エピローグはこう。

```
leave                # %rbp を %rsp に戻し、保存していた %rbp を復元
ret                  # 戻り番地に飛ぶ
```

`leave` は `movq %rbp, %rsp; popq %rbp` と等価。これで `%rsp` が prologue 直後の状態に戻り、`%rbp` も呼び出し元の値に戻る。**関数が触ったスタックは完璧にきれいに片付く**。

ch01・ch02 ではここに何も足さなかった。ローカル変数がなかったからだ。ch03 では、プロローグと最初の `leave` の間に **ローカル変数のための領域** を確保する。

## 2. ローカル変数の置き場所

スタックは下方向（アドレスが減る方向）に伸びる。プロローグ直後、`%rbp` は呼び出し元の `%rbp` を保存した場所を指している。

```
            高アドレス
           +---------+
           |  ...    |
           +---------+ ← 呼び出し元の %rbp が指していた場所
           | 戻り番地 |   (call 命令でここに積まれた)
           +---------+
   %rbp →  | 旧 %rbp  |   (pushq %rbp で積まれた)
           +---------+
   %rsp →  |         |   (空きスタック)
           |         |
           | ...     |
           低アドレス
```

ローカル変数は、この `%rbp` より **下** に置く。`-8(%rbp)`、`-16(%rbp)`、`-24(%rbp)` ... と、8バイト刻みで下に伸ばしていく。

```
   %rbp →    | 旧 %rbp  |
             +---------+
   -8(%rbp)  |    x    |     int x  (1つ目)
             +---------+
   -16(%rbp) |    y    |     int y  (2つ目)
             +---------+
   -24(%rbp) |    z    |     int z  (3つ目)
             +---------+
   %rsp →    |         |
```

それぞれの変数は、`%rbp` を基準にした **負のオフセット** で常にアクセスできる。`%rsp` が動いても（`pushq` で一時値を積んでも）、`%rbp` は不動だから、変数の位置は変わらない。これが `%rbp` を別建てに持つ理由だ。

### なぜ8バイトずつか

`int` は4バイトで十分なはずだ。なぜ8バイトずつ取るのか？

理由は単純で、**スタックの単位が8バイトだから**。x86-64 では `pushq` `popq` が8バイト操作で、スタックポインタも8バイト境界で動かすのが自然。各ローカル変数を8バイトずつにしておけば、`subq $8` を1回ずつ撃てば領域が広がるし、`-8`、`-16`、`-24`...というきれいなオフセットで並ぶ。

実プロダクションのコンパイラはアラインメントとサイズを変数の型ごとに細かく管理するが、tiny-c では「全部8バイト」で割り切る。

## 3. シンボルテーブル

「`x` と書いてあったら `-8(%rbp)` を意味する」── このマッピングを覚えておく構造が必要だ。それがシンボルテーブル。

最小限の実装はこう。

```c
typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;     /* 正の数。実際は -offset(%rbp) として使う */
    LVar *next;
};

static LVar *locals;       /* 連結リストの先頭 */
static int frame_size;     /* 現在までに割り当てたバイト数 */
```

シンプルな単方向リスト。`name` と `offset` のペア。連結リストにしているのは、検索が線形探索でいい（変数の数が少ない）から。ハッシュテーブルや AVL 木にする必要はない。

新しい変数を追加する関数:

```c
static int add_local(char *name) {
    /* 既にあるか確認 */
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    /* 新しく追加 */
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    frame_size += 8;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v->offset;
}
```

オフセットは「これまでの累計サイズ」になる。1つ目の変数は `frame_size += 8` で 8、2つ目は 16、というふうに。これが後で `-8(%rbp)`、`-16(%rbp)` として使われる。

検索する関数:

```c
static int find_local(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v->offset;
    fprintf(stderr, "undeclared variable: %s\n", name);
    exit(1);
}
```

リストを舐めて、見つかればオフセットを返す。なければエラー。

シンボルテーブルは **codegen.c の中だけで使う** 静的な道具だ。AST にも、parser.y にも漏らさない。`tinyc` は「関数1つに1つの平らなテーブル」という単純さで済む（ch04 で if/while が来ても同じ。ch08 でブロックスコープを真面目にやり始めると、テーブルが入れ子になる）。

## 4. NODE_VAR_DECL の codegen

`int x = 1;` をコンパイルする手順。

```c
case NODE_VAR_DECL: {
    int off = add_local(node->name);          /* シンボルテーブルに追加、オフセット取得 */
    fprintf(out, "  subq $8, %%rsp\n");       /* スタックを8バイト広げる */
    gen_expr(node->expr);                     /* 初期化式を計算 → eax */
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);  /* eax を保存 */
    return;
}
```

3ステップ。

1. `add_local` で名前を登録し、オフセットをもらう。
2. `subq $8, %rsp` で `%rsp` を8バイト下げる。**これがないと、後で `pushq` した時にこの変数が上書きされる**。
3. 初期化式 (`node->expr`) を `gen_expr` で評価する。結果は `%eax` に入る。
4. `movl %eax, -off(%rbp)` でその値を割り当てた場所に書き込む。

`subq $8, %rsp` は重要だ。`%rbp` 基準のアドレッシングは `%rsp` の位置とは独立だが、**`%rsp` より上の領域** は「これから push されるかもしれない一時領域」だから、変数を置いておくと壊される。`subq` で `%rsp` を下げて、変数領域を「使用中」とマークするわけだ。

### 別解: 一括して subq する

実プロダクションのコンパイラは、関数本体に登場する全変数の数を **事前に数えて**、プロローグで `subq $N, %rsp` を一発で撃つ。我々の lazy 方式（変数1つごとに `subq $8`）よりも効率がいい。

tiny-c では教育目的なので、lazy で書いている。「宣言と確保が1対1」のほうが、コードも読みやすい。`subq $8` を毎回出すのは数命令の損だが、可読性が勝つ。

## 5. NODE_IDENT の codegen

`x` を式として読むのは1命令。

```c
case NODE_IDENT: {
    int off = find_local(node->name);
    fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
    return;
}
```

`find_local` で名前を解決し、その場所から `%eax` に load する。約束事 **「式の値は %eax」** はそのまま守られる。

## 6. NODE_ASSIGN の codegen

代入は「rhs を計算して、lhs の場所に書く」。

```c
case NODE_ASSIGN: {
    if (node->lhs->kind != NODE_IDENT) {
        fprintf(stderr, "lhs of '=' must be a variable\n");
        exit(1);
    }
    int off = find_local(node->lhs->name);
    gen_expr(node->rhs);                      /* eax = rhs の値 */
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
```

ここで lvalue チェックを入れる。`node->lhs` が `NODE_IDENT` でなければエラー。これが文法レベルで「lvalue は限られる」と言わなかった代わりの判定だ。

順序は **rhs を先に計算する**。代入は `=` の右辺を評価した後で左辺に書き込む、という C の意味そのまま。`gen_expr(node->rhs)` の後、`%eax` には書き込むべき値が入っている。それを `movl %eax, -off(%rbp)` で目的地に書くだけ。

代入式の **「式の値」** は、**書き込んだ値そのもの**。`a = (b = 7)` のような連鎖代入では、内側の代入の結果（7）が外側の rhs として使われる。我々のコードでは、movl の後で `%eax` を変えていないから、自動的に **`%eax` に書いた値が残る** ── 結果として、代入式自体の値も `%eax` に乗っている。約束事を守れる。

## 7. NODE_EXPR_STMT の codegen

「式を計算して、値は捨てる」。

```c
case NODE_EXPR_STMT:
    gen_expr(node->expr);
    return;
```

`gen_expr` を呼ぶだけ。`%eax` に何が入っても気にしない。次の文がやってきたら `%eax` を上書きするだけだ。

## 8. 連鎖代入を見てみる

`a = b = 7` のコード生成を辿ろう。

AST:

```
ASSIGN
├─ IDENT a
└─ ASSIGN
   ├─ IDENT b
   └─ INT_LIT 7
```

外側 `ASSIGN` の codegen:

1. `lhs` (IDENT a) の lvalue チェック OK。`off_a` 取得。
2. `gen_expr(node->rhs)` ── 内側の `ASSIGN` を評価。
   - `lhs` (IDENT b) の lvalue チェック OK。`off_b` 取得。
   - `gen_expr(INT_LIT 7)` → `movl $7, %eax`
   - `movl %eax, -off_b(%rbp)` → b に 7 書き込み。`%eax` はまだ 7。
3. `movl %eax, -off_a(%rbp)` → a に 7 書き込み。`%eax` はまだ 7。

これがまさに `a = (b = 7)` の意味だ。両方に 7 が入る。

実際にコンパイラに食わせると、こんなアセンブリになる。

```
movl $7, %eax              # 7 を計算
movl %eax, -16(%rbp)       # b = 7  (%eax はまだ 7)
movl %eax, -8(%rbp)        # a = 7
```

3命令で連鎖代入が表現できる。これは右結合の効果でもある（左結合だと `(a = b) = 7` になり、文法もコードも壊れる）。

## 9. codegen.c 全体（gen_expr / gen_stmt 抜粋）

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:    /* ch02 と同じ */
    case NODE_UNARY:      /* ch02 と同じ */
    case NODE_BINARY:     /* ch02 と同じ */
    case NODE_IDENT: {
        int off = find_local(node->name);
        fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
        return;
    }
    case NODE_ASSIGN: {
        if (node->lhs->kind != NODE_IDENT) { ... error ... }
        int off = find_local(node->lhs->name);
        gen_expr(node->rhs);
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
        return;
    }
    ...
    }
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:    /* ch01 と同じ */
    case NODE_BLOCK:     /* ch01 と同じ */
    case NODE_VAR_DECL: {
        int off = add_local(node->name);
        fprintf(out, "  subq $8, %%rsp\n");
        gen_expr(node->expr);
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
        return;
    }
    case NODE_EXPR_STMT:
        gen_expr(node->expr);
        return;
    ...
    }
}
```

`codegen` 関数本体（プロローグを出すところ）は ch01 と同じ。ローカル領域の確保は `gen_stmt` で `VAR_DECL` を見つけたとき lazy にやるので、プロローグはいじらない。

## 10. lvalue / rvalue という見方

ここで一度立ち止まって、整理しておこう。

- **rvalue** （right value、読み取り） = 式の **値**。`x + 1` の `x` も、`5` も、`a + b` も rvalue。`%eax` に乗ってくるもの。
- **lvalue** （left value、左辺値、書き込み可能） = **メモリの場所**。`x = 5` の `x` は lvalue。`-8(%rbp)` のような番地。

我々の codegen は、ほぼ常に **rvalue** を生成している（`gen_expr` の結果は `%eax` に値を入れる）。例外は `=` の左辺で、ここだけ「場所」が必要になる。

ch03 では lvalue といえば変数だけなので、`find_local(name)` でオフセット1つを取れば済んだ。ch06 で **`*p`**、**`a[i]`**、**`&x`** が登場すると、lvalue を計算するための専用関数 `gen_addr` が必要になる。「アドレスを計算する gen_addr」と「値を計算する gen_expr」を別々に持ち、`*` で gen_expr が gen_addr を呼び出して `movl (%rax), %eax` で間接 load する ── という構造になる。

ch03 ではここまで踏み込まない。だが、`gen_expr(NODE_IDENT)` が「load する」ことと、`NODE_ASSIGN` が「store する」ことが、対称的に対応している ── この感覚を頭の隅に置いておくと、後の章の見通しがよくなる。

## 11. まとめ

- ローカル変数は **`%rbp` 相対** の負のオフセットで管理する（`-8(%rbp)`、`-16(%rbp)` ...）。
- 8バイトずつ取る。スタックの単位が8バイトなので素直。
- **シンボルテーブル**（連結リスト）が名前 → オフセット を覚える。codegen.c 内に閉じる。
- 変数宣言ごとに `subq $8, %rsp` で領域を取る (lazy allocation)。
- `=` の左辺は lvalue である必要があり、ch03 では `IDENT` のみ。codegen 時にチェックする。
- `gen_expr(IDENT)` は load、`NODE_ASSIGN` は store。**rvalue** と **lvalue** の対称性。

## 次へ

最後のサブ章（`03_build.md`）で、全ファイルの完全形と差分を並べ、ビルドして動かす。生成アセンブリを `int x = 1; int y = 2; return x + y;` の例で1行ずつ追う。
