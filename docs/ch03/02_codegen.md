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

`leave` は `movq %rbp, %rsp; popq %rbp` と等価。これで `%rsp` が prologue 直後の状態に戻り、`%rbp` も呼び出し元の値に戻る。**関数が使ったスタック領域はきれいに解放される**。

ch01・ch02 ではここに何も足さなかった。ローカル変数がなかったからだ。ch03 では、プロローグと最初の `leave` の間に **ローカル変数のための領域** を確保する。

## 2. ローカル変数の置き場所

スタックは下方向（アドレスが減る方向）に伸びる。プロローグ直後、`%rbp` は呼び出し元の `%rbp` を保存した場所を指している。

```
            高アドレス
           +---------+
           |  ...    |
           +---------+
           | 戻り番地 |   call 命令で積まれた（call 直後は %rsp がここを指す）
           +---------+
   %rbp →  | 旧 %rbp  |   pushq %rbp で積まれた（その後 movq %rsp, %rbp で %rbp はここを指す）
           +---------+
   %rsp →  |         |   （空きスタック）
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

`name` と `offset` のペアを保持する単方向リスト。

新しい変数を追加する関数:

```c
static int add_local(char *name) {
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
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

シンボルテーブルは **codegen.c の中だけで使う** 静的な道具だ。AST と parser.y は名前のままで、アドレスへの解決は触らない。`tinyc` は「関数 1 つに 1 つの平らなテーブル」という単純さで済む（ch04 で if/while が来ても同じ。ch06 でブロックスコープを入れるときに、シンボルテーブルに `active` フラグと scope_stack を追加して可視性を管理する形に拡張する）。

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
2. `subq $8, %rsp` で `%rsp` を8バイト下げる（理由は下記参照）。
3. 初期化式 (`node->expr`) を `gen_expr` で評価する。結果は `%eax` に入る。
4. `movl %eax, -off(%rbp)` でその値を割り当てた場所に書き込む。

`subq $8, %rsp` は重要だ。なぜか。プロローグ直後は `%rsp` が `%rbp` と同じ位置を指している。この状態のまま `x` を `-8(%rbp)` に書き込んだあとで `pushq` をすると、`%rsp` が 8 バイト下がって `-8(%rbp)` と同じ場所を指し、push されるデータがちょうど `x` のスロットを上書きしてしまう。

そこで `subq $8, %rsp` で **`%rsp` を `x` のスロットより下に下げておく**。すると後の `pushq` の書き込み先は `-16(%rbp)` 以下になり、`x` の領域は壊されない。`subq` は「ここまでが変数領域、これより下は一時領域」という境界を `%rsp` で示している。

## 5. NODE_IDENT の codegen

例えば `y = x + 1;` の右辺を計算するとき、`x` の値を読み出す部分。これは 1 命令で済む。

```c
case NODE_IDENT: {
    int off = find_local(node->name);
    fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
    return;
}
```

`find_local` で名前を解決し、その場所から `%eax` に load する。約束事 **「式の値は %eax」** はそのまま守られる。

## 6. NODE_ASSIGN の codegen

前節と同じ `y = x + 1;` の例で言えば、右辺 `x + 1` を計算した結果を `y` に書き込む部分。代入は「rhs を計算して、lhs の場所に書く」のがすべてだ。

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

ここで lvalue チェックを入れる。`node->lhs` が `NODE_IDENT` でなければエラー。

順序は **rhs を先に計算する**。代入は `=` の右辺を評価した後で左辺に書き込む、という C の意味そのまま。`gen_expr(node->rhs)` の後、`%eax` には書き込むべき値が入っている。それを `movl %eax, -off(%rbp)` で目的地に書くだけ。

代入式の **「式の値」** は、**書き込んだ値そのもの**。`a = (b = 7)` のような連鎖代入では、内側の代入の結果（7）が外側の rhs として使われる。我々のコードでは、movl の後で `%eax` を変えていないから、自動的に **`%eax` に書いた値が残る** ── 結果として、代入式自体の値も `%eax` に乗っている。約束事を守れる。

## 7. NODE_EXPR_STMT の codegen

前節の `ASSIGN` は **式** のノードで、値を計算して `%eax` に残す。一方 `EXPR_STMT` は **文** のノードで、式を実行はするが結果値を捨てる役割を持つ。`y = x + 1;` のように `;` で終わる代入は、AST 上では `EXPR_STMT` が `ASSIGN` を包む二段構造になる ── 「計算する」のが `ASSIGN`、「結果を捨てて次の文へ進む」のが `EXPR_STMT`。

`NODE_EXPR_STMT` は **「式を文として置いただけ」** のノードだ。たとえば:

```c
x = x + 5;
```

これ自体が C では「式」(`x = x + 5` という代入式) で、最後に `;` を付けることで「文」になる。AST はこんな形:

```
EXPR_STMT
└─ ASSIGN
   ├─ IDENT x
   └─ BINARY +
      ├─ IDENT x
      └─ INT_LIT 5
```

codegen は内側の式を評価するだけ。

```c
case NODE_EXPR_STMT:
    gen_expr(node->expr);
    return;
```

このとき `gen_expr` の中では `%eax` がフル活用される ── `x + 5` を計算して `%eax` に置き、それを `-off(%rbp)` の `x` のスロットに書き込む。**側面効果（メモリへの書き込み）は確かに起きる**。

「捨てる」のは式の **戻り値** のほう。代入式 `x = x + 5` には値があり（書き込んだ値）、それは `gen_expr` 終了時に `%eax` に残っている。だが `EXPR_STMT` の codegen はその `%eax` を誰にも渡さない。次の文の codegen が始まると、その文の最初の操作で `%eax` は上書きされる。

つまり:

- **`return x = x + 5;`** のように代入式が `return` の対象になっていれば、`%eax` の値はそのまま戻り値として使われる ── 捨てない。
- **`x = x + 5;`** のように単独の文として置けば、計算と代入は行われるが、式としての値（`%eax`）は誰も読まずに次の文で潰される ── 捨てる。

`NODE_EXPR_STMT` は後者のパターンを表すノードだ。

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

以下、`off_a` は `a` のオフセット（`find_local("a")` の戻り値）、`off_b` は `b` のオフセット。

1. `lhs` (IDENT a) の lvalue チェック OK。`off_a` を取得。
2. `gen_expr(node->rhs)` ── 内側の `ASSIGN` を評価。
   - `lhs` (IDENT b) の lvalue チェック OK。`off_b` を取得。
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
    case NODE_INT_LIT: { /* ch02 と同じ */ }
    case NODE_UNARY:   { /* ch02 と同じ */ }
    case NODE_BINARY:  { /* ch02 と同じ */ }
    case NODE_IDENT: {                                         /* 追加 */
        int off = find_local(node->name);                      /* 追加 */
        fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);       /* 追加 */
        return;                                                /* 追加 */
    }                                                          /* 追加 */
    case NODE_ASSIGN: {                                        /* 追加 */
        if (node->lhs->kind != NODE_IDENT) { ... error ... }   /* 追加 */
        int off = find_local(node->lhs->name);                 /* 追加 */
        gen_expr(node->rhs);                                   /* 追加 */
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);       /* 追加 */
        return;                                                /* 追加 */
    }                                                          /* 追加 */
    ...
    }
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN: { /* ch01 と同じ */ }
    case NODE_BLOCK:  { /* ch01 と同じ */ }
    case NODE_VAR_DECL: {                                      /* 追加 */
        int off = add_local(node->name);                       /* 追加 */
        fprintf(out, "  subq $8, %%rsp\n");                    /* 追加 */
        gen_expr(node->expr);                                  /* 追加 */
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);       /* 追加 */
        return;                                                /* 追加 */
    }                                                          /* 追加 */
    case NODE_EXPR_STMT:                                       /* 追加 */
        gen_expr(node->expr);                                  /* 追加 */
        return;                                                /* 追加 */
    ...
    }
}
```

## 10. lvalue / rvalue

- **rvalue** = 式の **値**。`%eax` に保存されるもの。`gen_expr` の結果。
- **lvalue** = **メモリの場所**。`x = 5` の `x`。`-8(%rbp)` のような番地。

ch03 では lvalue は変数だけなので、`find_local(name)` でオフセットを引けば済む。ch06 で `*p`、`a[i]`、`&x` が登場すると、lvalue 計算専用の `gen_addr` 関数が登場する。`gen_expr(IDENT)` が load、`NODE_ASSIGN` が store ── 対称的な操作になっている。

## 次へ

最後の節（`03_build.md`）で全ファイルの完全形を並べ、ビルドして動かす。
