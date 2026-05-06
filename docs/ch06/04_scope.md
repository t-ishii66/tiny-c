# 04 — ブロックスコープ

C の `{ ... }` は単なる文のグルーピングではなく、その中で宣言した変数の生存範囲を区切る **スコープ** でもある。`{ int x = 1; } { int x = 2; }` のように別ブロックなら同名変数を再宣言できる ── ch05 までの tiny-c はこれを「再宣言エラー」として弾いていた。本節でその制限を取り除く。

しかも、**フラグなどの仕掛けは要らない**。`locals` 連結リスト自体をスコープに合わせて伸び縮みさせるだけ ── スコープを抜けたら、そこで宣言された変数たちはリストから単純に切り離される。さらにこの設計の副産物として **スタックスロットの再利用**（兄弟ブロックが同じスロットを共有）も自然に実現できる。

## 1. 何が問題だったか

ch03〜ch05 の codegen は、関数内のローカル変数を **関数につき 1 つのフラットなリンクリスト** で管理していた:

```c
int main() {
    { int x = 1; }      // 1 つ目の int x
    { int x = 2; }      // ← redeclared variable: x  ← エラーになる
    return 0;
}
```

`add_local` がリスト全体を走査して同名を見つけたら即エラー。ブロック境界を意識しない。C の意味とは食い違う。

ch06 でスコープを導入する動機は単純: **C と同じ `{}` の意味で書きたい**、加えて **スタック使用量も小さくしたい**。

## 2. 設計の選び方

実装方式は大きく 2 通り:

| 方式 | 内容 | 長所 / 短所 |
|------|------|-----------|
| A: テーブルを入れ子で組む | ブロックごとに新しいシンボルテーブルを `malloc`、抜けたら解放 | C 教科書的。確保/解放が増える |
| B: locals 自身を伸び縮みさせる | スコープ入る時 head を保存、抜ける時 head を復元（= 切り戻し）| 状態最少。スロット再利用も自然 |

tiny-c では **B** を採る。`locals` 連結リストの head を保存・復元するだけで、スコープの可視性とスロット再利用が両方手に入る ── 一番シンプルで、一番得るものが多い。

## 3. データ構造

`LVar` 構造体は ch05 までと **同じ**:

```c
struct LVar {
    char *name;
    int offset;
    Type *type;
    int is_global;
    LVar *next;
};
```

`locals` の挙動が新しい。`locals` は **ローカル変数に対応したオブジェクト `LVar` の連結リスト** で、`locals` 変数自体がそのリストの **先頭ノード**（以下 **head** と呼ぶ）を指している。`add_local` は新しい LVar を head に **prepend**（= 連結し直して新しい head に据える）するので、head は常に最新の宣言。ch06/04 では:

- **`locals` は「いま見えている in-scope な変数だけ」** を保持する（head が最も新しい宣言）
- スコープを **抜けると `locals` は head が切り戻される** ── ブロック内で宣言された変数は locals リストから外れる
- **LVar オブジェクト自体はメモリ上に残る**。後述する AST のバックポインタ（`Node->lvar`）から引き続き参照できるので、Phase 2 で再リンクするときに使える

スコープ管理には次の状態を持つ:

```c
#define MAX_SCOPE 64
static LVar *scope_top[MAX_SCOPE];     /* スコープに入る時の locals head */
static int frame_save[MAX_SCOPE];      /* 同じく frame_size の保存（Phase 1 で使う）*/
static int scope_depth;                 /* 入れ子の深さ */

static int frame_size;       /* 現在のフレーム消費量 */
static int max_frame_size;   /* 関数全体での frame_size の最大値 */
```

`scope_top[scope_depth]` は「**そのスコープに入った瞬間の `locals` head**」── 抜ける時はこの値を `locals` に戻すだけで OK。`frame_save[scope_depth]` も同じ発想で `frame_size` を退避し、抜ける時に復元 → 兄弟スコープが **同じスロットを再利用** できる。`max_frame_size` がプロローグ `subq` の量。

それから AST の `Node` 構造体に **`LVar *lvar`** を追加 ── Phase 1（変数収集）が登録した LVar への back-pointer を、AST のノード自身に持たせる。Phase 2（codegen）で名前検索なしに直接たどれるようにするため。

```c
struct Node {
    /* ... 既存のフィールド ... */
    LVar *lvar;   /* NODE_VAR_DECL & param NODE_IDENT */
};
```

`LVar` の定義は `codegen.c` に閉じたまま、`ast.h` は **前方宣言だけ** 持つ:

```c
typedef struct LVar LVar;   /* defined in codegen.c */
```

**ch05 までに見てきた AST の構造はそのまま** ── ノードに `LVar *` フィールドを 1 つ足しただけで、スコープのロジックは `codegen.c` 側に集約できる。

## 4. スコープ操作の 2 関数

スコープの出入りはこれだけ:

```c
static void enter_scope(void) {
    scope_top[scope_depth] = locals;
    frame_save[scope_depth] = frame_size;
    scope_depth++;
}

static void exit_scope(void) {
    scope_depth--;
    locals = scope_top[scope_depth];      /* 切り戻し → ブロック内変数は不可視に */
    frame_size = frame_save[scope_depth]; /* スロットも返却（再利用可能に）*/
}
```

挙動の核心:

- **`enter_scope`** は `locals` head と `frame_size` を退避するだけ
- **`exit_scope`** はそれらを復元するだけ ── これで「このスコープで宣言された変数」は `locals` から自動的に消え、その分の `frame_size` も解放される

`locals` リストは **常に「いま見えている変数だけ」** を表す ── スコープ離脱でリストが切り戻されると、そのスコープで作られた LVar はリストから外れて、find_var からも届かなくなる。

frame_size の save/restore は **slot 再利用** のため:

```c
{ int a = 1; int b = 2; }    // frame_size: 0 → 8 → 16, 抜けると 0 に戻る
{ int c = 3; int d = 4; }    // frame_size: 0 → 8 → 16, c は a と同じスロット, d は b と同じスロット
```

最終的にこの関数のフレームは 16 バイトで足りる（旧設計なら 32 バイト必要だった）。`max_frame_size` がこのピーク値を覚えておく。

### `add_local` の redeclared 検査

`add_local` の中で、`locals` の head から `scope_top[depth-1]` まで歩いて同名チェックする:

```c
static LVar *add_local(char *name, Type *type) {
    LVar *bound = (scope_depth > 0) ? scope_top[scope_depth - 1] : NULL;
    for (LVar *v = locals; v != bound; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    int sz = round_up_8(type_size(type));
    frame_size += sz;
    if (frame_size > max_frame_size) max_frame_size = frame_size;

    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    v->type = type;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v;
}
```

走査範囲は **head ↓ scope_top[depth-1]** の半開区間 ── これは「このスコープで既に宣言された変数たち」だけを覆う。それより外（外側スコープの変数）には触れないので、**shadowing**（内側スコープで、外側にある同名の変数と同じ名前で別の変数を宣言したとき、外側の変数が一時的に隠される現象）は正しく機能する（外側の同名変数があっても redeclared エラーにならず、別スロットに新しい変数として登録される）。

### `find_var` はシンプルに

find_var はそのままリストを走査するだけ:

```c
static LVar *find_var(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    for (LVar *v = globals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}
```

`locals` には常に **in-scope の変数だけ** が並んでいるので、追加の絞り込みは不要。head が最新（= 最も内側で宣言されたもの）なので、自然に **innermost shadowing が勝つ**。

### 内部状態を図で追う

例として次のソースの Phase 1 各ステップを追う:

```c
int main() {
    int x = 5;          // 外側スコープ (= main の本体)
    { int x = 10; }     // 内側スコープ。同じ名前 x で別の変数（図中では X と表記）
    return x;           // 外側 x を読む → 5
}
```

**外側 / 内側の `x` は別の LVar オブジェクト**。図中では区別のため、内側の `x` を **大文字 `X`** で表記する（実際のコードではどちらも `name = "x"`、ただし別の `LVar` ノード）。

表の `scope_top` 列は配列 `scope_top[]` の中身を **top-of-stack（最新）から先頭** に並べたもの。`scope_top[i]` は「**スコープ `i` に入る直前の `locals` head**」を保存している ── `exit_scope` はここから値を読み戻して `locals` を切り戻す。`frame_save[]` も同じパターンで `frame_size` を保存しているが、表からは省略する（`scope_top` と完全に並行に動くだけの付随情報）。

```
ステップ                       locals (head→tail)         scope_top              frame_size  scope_depth
─────────────────────────────────────────────────────────────────────────────────────────
0. gen_func 開始              [NULL]                      []                     0           0
1. enter_scope (関数)          [NULL]                      [NULL]                 0           1
2. (params なし)               [NULL]                      [NULL]                 0           1
3. enter_scope (本体)          [NULL]                      [NULL, NULL]           0           2
4. add_local x → frame=8       [x, NULL]                   [NULL, NULL]           8           2
5. enter_scope (内側)          [x, NULL]                   [x, NULL, NULL]        8           3
6. add_local x → frame=16      [X, x, NULL]                [x, NULL, NULL]        16          3
7. exit_scope (内側)           [x, NULL]                   [NULL, NULL]           8           2
8. exit_scope (本体)           [NULL]                      [NULL]                 0           1
```

Phase 1 終了時、`max_frame_size = 16`。プロローグでは `subq $16, %rsp`（16 バイト境界整列で同じく 16）。

ここで注目したいのは **ステップ 6** ── 内側で `int x = 10;` を宣言する時、外側にすでに `x` がいるのに redeclared エラーにならない理由は、検査の走査範囲が `head` から **`scope_top[2] = (外側) x`** までの半開区間だから。loop の 1 イテレーション目で `v == bound` になって即終了 ── つまり内側スコープではまだ何も登録されていないので、衝突はあり得ない。外側の `x` は走査対象から外れているので、shadowing として扱われる。

ステップ 6 の状態を絵にすると:

```
  locals:                       scope_top[]
  ┌──────┐                    ┌──────┐
  │  X   │ ← head           2 │  x   │ ← 内側入る直前 (= 外側 x)
  │  x   │                  1 │ NULL │ ← 外側入る直前
  │ NULL │ ← tail            0 │ NULL │ ← main() 開始時
  └──────┘                    └──────┘
                              scope_depth = 3, frame_size = 16
```

`X` は内側の `x`（offset 16）、`x` は外側の `x`（offset 8）。`find_var("x")` はこの時点で head から走るので **innermost の `X` が先に見つかる** ── shadowing が自然に成立。

`exit_scope` (内側) ではこの 3 つを 1 段戻して外側の環境に復帰するだけ:

- `locals` ← `scope_top[2] = x` → `[x]`（内側 X は外れる）
- `frame_size` ← `frame_save[2] = 8`（X のスロット -16 が解放、再利用可能に）
- `scope_depth` ← 2

これで内側のブロックを抜けたあとは `find_var("x")` が外側の `x`（offset 8、値 5）を見つける。`return x;` は 5 を返す。

LVar `X`（内側の `x`）はリスト外のオブジェクトになり、以降アクセスされない。slot 再利用の効果は、次に他の変数を `add_local` するとき `frame_size` が 8 から伸びる（つまり `X` が居たスロット (-16) を新しい変数が使う）形で現れる。

## 5. 二相 codegen への組み込み

ch05 までと同じく **Phase 1（収集）→ Phase 2（生成）** の二相構成を保つ。両 Phase で **同じ scope 操作** を使うが、frame_size が動くのは Phase 1 のみ（Phase 2 では `frame_size` は 0 のままで、すでに固定された offset を使うだけ）。

### 二相の実体: 同じ AST を 2 回歩く

Phase 1 と Phase 2 は **同じ AST に対する別々の再帰関数の起動**:

- **Phase 1** = `collect_locals(fn->body)` という再帰関数。`NODE_VAR_DECL` / `NODE_BLOCK` / `NODE_IF` / `NODE_WHILE` の各ノードに反応してアクションを取り、それ以外のノード（式、`return` など）は無視する。アクションの中身は **スロット確保 (`add_local`) + AST への back-pointer 設定 + scope の出入り**。**Phase 1 を Phase 2 から分離する最大の動機は `max_frame_size` の確定** ── プロローグの `subq $N, %rsp` で N が決まっていないと書けないため、まず本体を歩き切って N を確定する必要がある。
- **Phase 2** = `gen_stmt(fn->body)` という再帰関数。同じく VAR_DECL / BLOCK / IF / WHILE 等に反応するが、ここでは **アセンブリ出力** がアクション。式系の文（`return expr;` や代入など）も処理する。

両者は AST のルート（関数本体）から **同じ深さ優先順序で歩く** ので、訪問順が完全に一致する。NODE_BLOCK / NODE_VAR_DECL の入れ子構造は AST 上に固定されているので:

- Phase 1 で `enter_scope` / `exit_scope` が呼ばれる箇所
- Phase 2 で `enter_scope` / `exit_scope` が呼ばれる箇所

これらは **同じ位置・同じ順序で発生する** ── スコープ階層は自動的に同期する（明示的な「Phase 1 の状態を Phase 2 に渡す」操作は不要）。

そして Phase 1 が各 VAR_DECL ノードに **`node->lvar = LVar*` を書き残している** ので、Phase 2 が同じ NODE_VAR_DECL を訪れたときに「**Phase 1 が用意した、まさにこのスコープに所属するべき LVar**」を直接取れる。名前検索も同名突き合わせも要らず、ただ `node->lvar` を読むだけ。

これで Phase 2 は `locals` を空からゼロベースで再構築しても、**結果として Phase 1 のスコープ構造と完全に一致する** ── AST の形がそのまま「正しい筋書き」を保っている、という設計。

### Phase 1: collect_locals

スコープを意識して走り、各 `NODE_VAR_DECL` のスロット確保 + AST への back-pointer 設定。NODE_BLOCK で `enter_scope` / `exit_scope` を呼ぶ:

```c
static void collect_locals(Node *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_VAR_DECL:
        node->lvar = add_local(node->name, node->type);   /* ★ */
        break;
    case NODE_BLOCK:
        enter_scope();
        for (NodeList *l = node->stmts; l; l = l->next)
            collect_locals(l->node);
        if (frame_size > max_frame_size) max_frame_size = frame_size;
        exit_scope();
        break;
    case NODE_IF:
        collect_locals(node->then_body);
        collect_locals(node->else_body);
        break;
    case NODE_WHILE:
        collect_locals(node->body);
        break;
    default: break;
    }
}
```

ブロックを抜ける直前に `max_frame_size` を更新するのがポイント ── 次の兄弟ブロックがスロット再利用しても、ピーク値は失われない。

### Phase 2: gen_stmt

**Phase 1 と Phase 2 の間で `locals` はリセットされる**（後述の `gen_func` で `locals = NULL; scope_depth = 0;`）。Phase 2 が始まる時点で `locals` は空 ── ただし各 LVar オブジェクト自体はメモリ上に残っており、AST の `node->lvar` バックポインタから **だけ** 参照可能な状態。

Phase 1 で各 LVar には offset が確定しているので、Phase 2 は名前で検索する必要がなく、**`node->lvar` を直接読んで `locals` に再リンクするだけ**。これを NODE_VAR_DECL を訪れるたびに繰り返すことで、`locals` は再び育っていく:

```c
case NODE_VAR_DECL: {
    LVar *v = node->lvar;
    v->next = locals;
    locals = v;                    /* ★ scope に入る */
    if (!node->expr) return;       /* 配列宣言、初期化なし */
    /* ... 初期化コード生成 ... */
    return;
}

case NODE_BLOCK:
    enter_scope();
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    exit_scope();
    return;
```

`v->next = locals; locals = v;` で **prepend** する点に注目 ── これで Phase 2 でも head が最新になり、find_var が innermost を見つけられる。

`int x = x + 1;` のように右辺で自分自身を参照する場合（C では未初期化の値を読む）も、`locals` への再リンクが初期化コード生成より前に行われるので、find_var はその x を見つけられる ── C のセマンティクスと整合する。

## 6. パラメータと関数レベルスコープ

関数のパラメータも **スコープに属するべきもの**。tiny-c では **関数全体を外側スコープ、関数本体ブロックを内側スコープ** とする 2 段構造で扱う。これにより、本体内の `int x = 1;` がパラメータ `x` を shadow できる（C の規則と一致）。

```c
static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    max_frame_size = 0;
    scope_depth = 0;

    /* Phase 1: scope 込みで全変数を集める */
    enter_scope();   /* ★ 関数スコープ */
    for (NodeList *l = fn->params; l; l = l->next)
        l->node->lvar = add_local(l->node->name, l->node->type);
    collect_locals(fn->body);
    if (frame_size > max_frame_size) max_frame_size = frame_size;
    /* exit_scope は呼ばない。Phase 2 のために状態を作り直す */

    int aligned_frame = (max_frame_size + 15) & ~15;

    /* ... プロローグ出力 ... subq $aligned_frame, %rsp */

    /* Phase 2 リセット */
    locals = NULL;
    scope_depth = 0;

    enter_scope();   /* ★ 関数スコープ */
    for (param) {
        /* v を locals に再リンクし、register → スタックに spill */
        v->next = locals; locals = v;
        ...
    }
    gen_stmt(fn->body);  /* body NODE_BLOCK は自前で enter/exit する */
    exit_scope();

    /* ... エピローグ ... */
}
```

入れ子の状態は実行時にこう動く:

```
gen_func 開始
└─ enter_scope (depth=1, 関数スコープ)
   └─ params を locals に追加
   └─ gen_stmt(body):  body は NODE_BLOCK
      └─ enter_scope (depth=2, 本体スコープ)
         └─ body の文を順に処理
            └─ NODE_VAR_DECL で locals に追加
            └─ NODE_BLOCK でさらに enter/exit (depth=3)
            └─ ...
      └─ exit_scope (本体内で追加された変数が locals から消える)
└─ exit_scope (params が locals から消える)
```

## 7. 動作確認

すべて `tinyc` でコンパイルし、生成された assembly が意図通りになるかを見る。

### 別ブロックで同名 OK & スロット再利用

```c
int main() {
    { int x = 1; }
    { int x = 2; return x + 3; }   // → 5
}
```

両方の `x` が **同じスロット `-8(%rbp)`** を共有。フレームサイズは 8 バイト（旧設計なら 16 バイト）。

### 内側で外側を shadow（別スロット）

```c
int main() {
    int x = 5;
    { int x = 10; return x; }      // → 10
}
```

外側 x が `-8(%rbp)`、内側 x は外側と並列に存在する必要があるので **新しいスロット `-16(%rbp)`**。フレーム 16 バイト。`return x` の時点で内側 x が `locals` head にあるので 10 が返る。

### 内側ブロック離脱で外側に戻る

```c
int main() {
    int x = 5;
    { int x = 10; }
    return x;                       // → 5
}
```

内側 x は exit_scope で `locals` から消える。`return x` は外側 x (`-8(%rbp)`) を見て 5 を返す。

### パラメータの shadow

```c
int f(int x) {
    { int x = 100; return x; }     // → 100
}
int main() { return f(7); }
```

パラメータ x は関数スコープ、ブロック内 x は本体スコープ ── 別スコープなので shadow が成立する。

### redeclared エラーは保たれる

```c
int main() {
    int x = 1;
    int x = 2;       // ← 同一スコープ、redeclared variable: x
    return x;
}
```

`add_local` がスコープ内で同名を検出してエラー。

## 8. 制約と将来課題

- **`MAX_SCOPE` 定数**: 64 段の入れ子は教育用としては十分だが、超えれば `exit(1)`。本気でやるなら動的確保。
- **`for` 文のスコープ**: tiny-c に `for` はないが、もし入れるなら `for (int i = 0; ...; ...)` の `i` は for のスコープに属するという C の規則も同じパターンで実装できる ── enter_scope を追加するだけ。

## 9. 次へ

最後の節（`05_build.md`）で、ch06 の全ファイルの完全形を ch05 との差分とともに並べ、Hello, world と strlen のデモを動かす。
