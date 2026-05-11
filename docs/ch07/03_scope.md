# 03 — ブロックスコープ

C の `{ ... }` は単なる文のグルーピングではなく、その中で宣言した変数の生存範囲を区切る **スコープ** でもある。`{ int x = 1; } { int x = 2; }` のように別ブロックなら同名変数を再宣言できる ── ch06 までの tiny-c はこれを「再宣言エラー」として弾いていた。本節でその制限を取り除く。

**前節（バックパッチ）が前提**。ch06 の二相 codegen にスコープを足すと、Phase 1（収集）と Phase 2（生成）の両方で同じスコープ状態を **同期して** 追跡する複雑さが発生する。前節で単一パス化したことで、スコープ操作は codegen の流れに **一度だけ** 組み込めばよくなる。

## 1. データ構造 ── スコープ追跡

スコープ管理に追加するのは次の状態:

```c
#define MAX_SCOPE 64
static LVar *scope_top[MAX_SCOPE];   /* スコープに入る時の locals head */
static int frame_save[MAX_SCOPE];    /* スコープに入る時の frame_size */
static int scope_depth;

static int max_frame_size;           /* これまでに到達した最大の frame_size */
```

考え方はシンプル:

- **`locals` 連結リスト** が常に「いま見えている変数だけ」を表す
- スコープに **入る** 時、現在の `locals` head と `frame_size` を退避
- スコープを **抜ける** 時、退避していた値に戻す（= locals は切り戻され、frame_size も元に戻る）

`scope_top[scope_depth]` は「**そのスコープに入った瞬間の locals head**」 ── 抜ける時はこの値を `locals` に戻すだけで、ブロック内で宣言された変数たちはリストから自動的に外れる。`frame_save[scope_depth]` も同じ発想で、兄弟ブロックが **同じスタックスロットを再利用** できるようにする。

ただし `frame_size` を巻き戻すと、プロローグの `subq $N` に使うべき値が消えてしまう ── そこで **`max_frame_size`** で「これまで一度でも到達した最大の `frame_size`」を別途追跡する。バックパッチではこの `max_frame_size` を使って `subq` を書き換える。

## 2. スコープ操作の 2 関数

```c
static void enter_scope(void) {
    scope_top[scope_depth] = locals;
    frame_save[scope_depth] = frame_size;
    scope_depth++;
}

static void exit_scope(void) {
    scope_depth--;
    locals = scope_top[scope_depth];      /* 切り戻し → ブロック内変数は不可視に */
    frame_size = frame_save[scope_depth]; /* 巻き戻し → 兄弟ブロックがスロット再利用 */
}
```

たった 6 行。**`enter_scope`** は現在の状態を退避し、**`exit_scope`** はそれらを復元するだけ。これで「このスコープで宣言された変数」は `locals` から自動的に消え、その分の `frame_size` も解放される。

## 3. `add_local` のスコープ境界

スコープ内での再宣言を弾きつつ、別スコープでの同名宣言は許す ── これは `add_local` の重複検査の **走査範囲** を制御するだけで実現する:

```c
static LVar *add_local(char *name, Type *type) {
    LVar *bound = (scope_depth > 0) ? scope_top[scope_depth - 1] : NULL;
    for (LVar *v = locals; v != bound; v = v->next) {   /* 現スコープだけスキャン */
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    int sz = round_up_8(type_size(type));
    frame_size += sz;
    if (frame_size > max_frame_size) max_frame_size = frame_size;  /* peak を更新 */
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name; v->type = type; v->offset = frame_size;
    v->next = locals; locals = v;
    return v;
}
```

走査範囲は **head ↓ scope_top[depth-1]** の半開区間 ── 「このスコープで既に宣言された変数たち」だけを覆う。それより外（外側スコープの変数）には触れないので、**shadowing**（内側スコープで、外側にある同名の変数と同じ名前で別の変数を宣言したとき、外側の変数が一時的に隠される現象）は正しく機能する（外側の同名変数があっても redeclared エラーにならず、別スロットに新しい変数として登録される）。

## 4. codegen への組み込み

スコープ操作は 3 箇所で呼ぶ:

```c
case NODE_BLOCK:
    enter_scope();
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    exit_scope();
    return;
```

そして `gen_func` の頭で関数スコープに入り、終わりで抜ける:

```c
static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    max_frame_size = 0;
    scope_depth = 0;
    /* ... プロローグ + subq プレースホルダ ... */

    enter_scope();   /* function scope: パラメータがここに住む */
    /* ... パラメータの add_local + spill ... */
    gen_stmt(fn->body);
    exit_scope();

    /* ... epilogue ... */

    /* バックパッチ: max_frame_size で subq を埋める */
    int aligned_frame = (max_frame_size + 15) & ~15;
    ...
}
```

ポイント:

- バックパッチには **`frame_size` ではなく `max_frame_size`** を使う。スコープを抜けると `frame_size` は巻き戻るが、`max_frame_size` は退避先として「一度でも到達した最大値」を保持し続けるから。
- 単一パスなので、スコープ操作も codegen の一直線の流れに乗る。ch06 の二相設計だと「Phase 1 のスコープ状態を Phase 2 で再現する」ような複雑さがあったが、それは無い。

## 5. 例: スロット再利用

```c
int main() {
    { int a = 1; int b = 2; }   /* a, b */
    { int c = 3; int d = 4; }   /* c, d (← a, b と同じスロット) */
    return 0;
}
```

実行の様子:

| ステップ | locals (head→) | frame_size | max_frame_size |
|---------|---------------|-----------|----------------|
| function scope に入る | (空) | 0 | 0 |
| 1つ目の `{` に入る | (空) | 0 | 0 |
| `int a` | a | 8 | 8 |
| `int b` | b → a | 16 | 16 |
| 1つ目の `}` を抜ける | (空) | 0 | 16 ← peak は保持 |
| 2つ目の `{` に入る | (空) | 0 | 16 |
| `int c` | c | 8 | 16 |
| `int d` | d → c | 16 | 16 |
| 2つ目の `}` を抜ける | (空) | 0 | 16 |

最終的に `max_frame_size = 16`。バックパッチが `subq $16, %rsp` を埋める ── 4変数なのにスタックは 16 バイトで済む。

## 6. 外側変数のシャドーイング

```c
int main() {
    int x = 5;          // 外側スコープ
    { int x = 10;       // 内側スコープ。外側の x を隠す
      return x;         // → 10
    }
    return x;           // ここは届かないが、届けば 5
}
```

内側で `int x = 10;` を宣言するとき、`add_local` の重複検査は **scope_top[1] = 外側 x までの半開区間**（= 内側の locals が空）を走査するので、衝突は検出されない。新しい LVar が別スロット（offset = 16）に作られ、`locals` の head に積まれる。

内側の `return x;` では `find_var("x")` が **locals の先頭から** 検索するので、内側の x（offset 16, 値 10）が先に見つかる。

内側の `}` で `exit_scope` が走ると、`locals` は外側 x まで切り戻され、`frame_size` も 8 に戻る。これ以降の `find_var("x")` は外側の x（offset 8, 値 5）を返す。

## 7. ch06 の選択 vs ch07 の選択

| | ch06 (本書) | ch07 (本節) |
|---|------------|-----------|
| ブロックスコープ | 扱わない（フラットなテーブル） | あり |
| `{ int x=1; }{ int x=2; }` | 再宣言エラー | OK |
| スロット再利用 | なし | あり（兄弟ブロックで） |
| `add_local` の検査範囲 | locals 全体 | 現スコープのみ |

## 8. 次へ

次の節（`04_peephole.md`）で、最適化の最後のピース ── 生成 asm を眺めて隣接命令を書き換える **ピープホール最適化** を見る。これも単一パス（バックパッチ）と同じく、メモリバッファ上で動く仕掛けだ。
