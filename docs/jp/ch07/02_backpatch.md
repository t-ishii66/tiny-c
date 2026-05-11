# 02 — バックパッチで Phase 1 を消す

ch06 の codegen は **Phase 1（収集）→ Phase 2（生成）** の二相構成だった。Phase 1 を分離する唯一の理由は **プロローグの `subq $N, %rsp` で `frame_size` が確定していないと書けない** から ── 関数本体を歩き切らないと N が分からない以上、本体を生成する前に一度走査が要る、という制約だ。

ch07 ではこの制約を **バックパッチ** で外す。「先に N が決まらない部分はプレースホルダで書いておき、後から書き戻す」── これでコード生成は **単一パス** になる。

最適化（節 01 の AST 畳み込み、節 03 のピープホール）とは別の話だが、ch07 で導入する **メモリバッファ** がバックパッチを可能にする ── 導入する価値があるテーマ。

## 1. 何が問題だったか

ch06 の `gen_func` は次のような順序で動いていた:

```c
gen_func(fn) {
    /* Phase 1: 全 VAR_DECL を歩いて frame_size を確定 */
    for params: add_local(...);
    collect_locals(fn->body);    /* AST を歩くだけ（コードは出さない）*/

    /* Phase 2: frame_size が分かったのでプロローグを書ける */
    fprintf("subq $%d, %%rsp", aligned(frame_size));
    /* 本体の codegen（locals は Phase 1 で構築済み）*/
    gen_stmt(fn->body);
    ...
}
```

AST を **2 回走査** する。最初は frame size 計算だけ、2 回目で実際にアセンブリを書き出す。

なぜ 2 回必要か? **`fprintf` で即時書き出しているから**。プロローグ `subq $N` を書いた後で N が分かっても戻って書き換える術がない。だから先に N を確定する Phase 1 が必要だった。

## 2. メモリバッファがあれば書き換えられる

ch07 ではアセンブリ出力を **メモリバッファ**（`open_memstream`）に溜める。これは元々ピープホール最適化（節 03）のために導入する仕組みだが、副産物として **バッファ上で `ftell` / `fseek` が使える** ── つまり書いた位置に戻って上書きできる。

```c
/* main.c */
char *buf = NULL;
size_t len = 0;
FILE *mem = open_memstream(&buf, &len);
codegen(program, mem);     /* mem に書き出す */
fclose(mem);                /* この時点で buf に全アセンブリが入っている */

if (no_opt)
    fputs(buf, stdout);
else
    peephole(buf, len, stdout);
```

`mem` は `FILE *` のインタフェースを持つが、実体はメモリ。`ftell(mem)` で現在の書き込み位置（バイトオフセット）を取り、`fseek(mem, pos, SEEK_SET)` で過去の位置に戻れる。**stdout は seekable でない** が、memstream なら seekable。

ch07 では **`--no-opt` でもバッファを使う**（`fputs` でそのまま流す）。バックパッチは最適化の有無に関わらず必須なので、バッファ自体は常に必要。

## 3. プロローグのバックパッチ

`gen_func` は次のように変わる:

```c
#define SUBQ_FRAME_WIDTH 10   /* プレースホルダの数値部の桁数（固定）*/

static void gen_func(Node *fn) {
    locals = NULL; frame_size = 0;

    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");

    /* ★ プロローグの subq をプレースホルダで書く。位置を覚えておく */
    long subq_pos = ftell(out);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, 0);   /* 0 はダミー */

    /* params を add_local + spill */
    for params: { LVar *v = add_local(...); /* spill 命令を出す */ }

    /* 本体を生成（frame_size がここで育つ）*/
    gen_stmt(fn->body);

    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");

    /* ★ ここで frame_size が確定した。プロローグに戻って上書き */
    int aligned_frame = (frame_size + 15) & ~15;
    long here = ftell(out);
    fseek(out, subq_pos, SEEK_SET);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, aligned_frame);
    fseek(out, here, SEEK_SET);
}
```

ポイント:

- **プレースホルダは固定幅** で書く。`%-*d` の `*` には `SUBQ_FRAME_WIDTH = 10` が入り、「左寄せ・10 桁・残りはスペース埋め」になる。 `subq $0         , %rsp` のような形（数字 + パディングスペース）。
- **書き戻しも同じ固定幅** で行う。バイト長が変わると後続のコードを上書きしてしまうので、**長さを変えない** のが必須条件。10 桁あれば最大 10 GB のフレームサイズまで表現できるので tiny-c では十分。
- アセンブラ（`as` / GAS / LLVM as）は `subq $16        , %rsp` のように **オペランド内の余分な空白** を許容する ── `,` の前のスペースは数字の一部としては読まれず、空白として無視される。

## 4. 単一パスで済むようになった codegen

Phase 1 が消えると、`gen_func` は本体生成中に直接 LVar を割り当てる。`NODE_VAR_DECL` の処理:

```c
case NODE_VAR_DECL: {
    /* 単一パス: その場で LVar を割り当て、name を可視にし、初期化コードを出す */
    LVar *v = add_local(node->name, node->type);
    if (!node->expr) return;
    fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
    emit_push();
    gen_expr(node->expr);
    emit_pop("%rcx");
    int sz = v->type->is_pointer ? 8 : v->type->base_size;
    emit_store(sz);
    return;
}
```

ch06 では Phase 1 (`collect_locals`) で全 VAR_DECL を歩いて `add_local` を呼び、locals テーブルを構築してから、Phase 2 で本体を生成していた。**ch07 では Phase 1 が消える** ── 本体生成中に VAR_DECL に出会った時点で `add_local` を呼ぶだけで済む。

`NODE_BLOCK` も特別な処理は要らず、stmt を順に生成するだけ:

```c
case NODE_BLOCK:
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    return;
```

## 5. ch06 vs ch07 の比較

| 項目 | ch06 (二相) | ch07 (単一パス + バックパッチ) |
|------|-----------|---------------------------|
| AST 走査回数 | 2 回（collect_locals + gen_stmt）| **1 回**（gen_stmt のみ） |
| 出力先 | `stdout` 直書き | `open_memstream` バッファ経由 |
| プロローグの `subq $N` | 先に N を確定してから書く | 先にプレースホルダで書き、後で N を埋める |
| `add_local` の呼び場所 | Phase 1（collect_locals 内）| 本体生成中（NODE_VAR_DECL 内）|

## 6. 次へ

次の節（`03_scope.md`）で、単一パス化の上に **ブロックスコープ** を乗せる。`{ int x=1; }{ int x=2; }` のような同名再宣言が通るようになる ── 二相設計だとスコープ状態を両 phase で同期する複雑さがあったが、単一パスなら codegen の流れに自然に組み込めるだけ。
