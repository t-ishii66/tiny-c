# 02 — lvalue と rvalue ── `gen_addr` の世界

`x = 5` の左辺と右辺、`*p = 10` の左辺と右辺、`a[i]` ── これらを統一的に扱える視点が **lvalue / rvalue** だ。ch06 の codegen はこの2つを別々の関数で計算する。

## 1. 復習: rvalue は ch05 までの世界

ch05 までの `gen_expr` は「**式の値** を `%eax`（または `%rax`）に置く」ことが約束だった。`x + 1` でも `f(2)` でも `42` でも、終わったあと `%eax` に値が保持されている ── これが rvalue。

ch03 で唯一 lvalue っぽい操作があった: `NODE_ASSIGN` で `IDENT` の場合に `find_local(name)` でオフセットを求めて `movl %eax, -off(%rbp)` で書き込む、という流れ。これは **「変数というlvalue 限定」** の特殊処理だった。

ch06 では lvalue が増える:

- `x` （変数、ch03 から）
- `*p` （ポインタの間接参照、新登場）
- `a[i]` （配列添字、新登場）

これらすべてを「**アドレスを計算する関数**」で扱いたい。それが **`gen_addr`** だ。

## 2. 二つの関数の対称性

ch06 の codegen は次の2つの関数で構成される:

```c
/* gen_expr: 式の VALUE を %eax/%rax に置く（rvalue） */
static void gen_expr(Node *node);

/* gen_addr: lvalue の ADDRESS を %rax に置く */
static void gen_addr(Node *node);
```

**`gen_stmt` から呼ばれるのは常に `gen_expr`**（`gen_addr` が直接呼ばれることはない）。`gen_addr` は `gen_expr` の内側で **アドレスが必要になる場面** に出会ったときに、サブルーチンとして呼ばれる。具体的には次の 3 場面:

| 場面 | ソース例 | 最初に降りてくる経路 |
|------|---------|--------------------|
| 代入の左辺 | `x = 5;` `*p = 10;` `a[i] = 7;` | `gen_expr(ASSIGN)` → `gen_addr(lhs)` |
| `&` でアドレスを取る | `int *p = &x;` | `gen_expr(&x)` → `gen_addr(x)` |
| 識別子を値として読む | `return x;` ; `int y = x;` ; `f(x)` ; `if (x)` ; | `gen_expr(x)` → `gen_addr(x)` → load |

両者は**互いを呼び合う**。それぞれがどんなソースコードから呼ばれるかを整理しておく:

- **`gen_expr(x)`** ── `x` を **値として読む** 場面で呼ぶ。例: `int y = x;`、`return x;`、`f(x)`。内部で `gen_addr(x)` でアドレスを得て、そこから値をロード。
- **`gen_expr(&x)`** ── `&x` を **値として読む** 場面（`int *p = &x;` のように）。`gen_addr(x)` を呼ぶだけで、得たアドレス自体が値。
- **`gen_expr(*p)`** ── `*p` を **値として読む** 場面（`int y = *p;`、`return *p;`）。`gen_expr(p)` でポインタの値（= アドレス）を得て、そこから load。
- **`gen_addr(*p)`** ── `*p` を **書き込み先として** 使う場面（`*p = 10;` の左辺）。`gen_expr(p)` でアドレスを得たらそれが書き込み先。

```
gen_expr(x)         = gen_addr(x); 得たアドレスから load;   ← x の値が欲しい (int y = x;)
gen_expr(&x)        = gen_addr(x);                         ← &x の値が欲しい (int *p = &x;)
gen_expr(*p)        = gen_expr(p); 得たアドレスから load;   ← *p の値が欲しい (int y = *p;)
gen_addr(*p)        = gen_expr(p);                         ← *p の書き込み先 (*p = 10;)
```

`&` は **load を取り去る** 操作、`*` は **load を足す** 操作。**`&*p == p`** が成り立つのもこの対称性の現れ。

## 3. `gen_addr` の本体

`gen_addr` の約束: **対象 (lvalue) のアドレスを `%rax` に置く**。下の switch 文に並ぶ 3 つの case すべて、この約束を守って return する。

```c
static void gen_addr(Node *node) {
    switch (node->kind) {
    case NODE_IDENT: {
        LVar *v = find_var(node->name);
        if (v->is_global)
            fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);    /* %rax = グローバル変数のアドレス */
        else
            fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset); /* %rax = ローカル変数のアドレス */
        return;
    }
    case NODE_UNARY:
        if (node->op == '*') {
            /* *p のアドレスは「p の値（ポインタが指す先のアドレス）」そのもの。
               gen_expr(p) を呼べば %rax に p の値が入り、それがそのまま *p のアドレスになる。
               これで `*p = 10;` の左辺評価も、`&(*p)` の処理も同じ経路で扱える。 */
            gen_expr(node->operand);
            return;
        }
        break;
    case NODE_INDEX: {
        /* &a[i] = (a の base address) + i * elem_size。
           この計算結果のアドレスを %rax に置いて return する（詳細は節 7）。 */
        ...
        return;
    }
    default:
        fprintf(stderr, "not an lvalue\n");
        exit(1);
    }
}
```

3 ケース対応:

- **`IDENT`**: 変数の場所を `leaq` で取る。ローカルなら `-off(%rbp)`、グローバルなら `name(%rip)`（PC 相対）。
- **`*p`**: `gen_expr(p)` で「p に格納されているアドレス」を `%rax` に得る。それが `*p` のアドレスそのもの。
- **`a[i]`**: 配列のベースアドレス + `i × elem_size`。次のセクションで詳しく。

`x = 5`、`*p = 10`、`a[3] = 7` ── どれも左辺のアドレスを計算したい。それらは全て `gen_addr` の同じ switch 文で処理できる。

## 4. `&` の codegen

`&x` の値は「`x` のアドレス」。これは `gen_addr(x)` そのもの:

```c
/* gen_expr の switch 内 */
case NODE_UNARY:
    if (node->op == '&') {
        gen_addr(node->operand);    /* %rax = x のアドレス */
        return;
    }
    ...
```

`gen_expr` の中で `gen_addr` を呼ぶだけ。普段の識別子読み出し（`gen_expr(x)`）なら `gen_addr` で得たアドレスからさらに値を load する（= `%rax` に入っているアドレスを参照して、そのアドレスが指すメモリの内容を読み出し、`%rax` に上書きする）が、`&x` ではその load をスキップする。結果として **`%rax` にはアドレスがそのまま残り、そのアドレスが指す先のデータには触らない**。つまり **`&` は load を取り去る** といえる。

## 5. `*` の codegen

`*p` は文脈によって扱いが大きく違うので、最初に区別しておく:

- **`*p = 123;` の左辺** ── `*p` の **アドレス** が欲しい。前節 3 の `gen_addr` の case `*p` がこれを扱う。`gen_expr(p)` で `%rax` に p の値（= 書き込み先アドレス）を置くだけ。そのアドレスに 123 を store する（load はしない）。
- **`x = *p;` の右辺** ── `*p` の **値** が欲しい。`%rax` に p の値（= 読み出し元アドレス）を置いたあと、**さらに load して** メモリの中身を `%rax` に読み込む。

本節で扱うのは後者、`gen_expr(*p)` の codegen。値は「p に格納されているアドレスから読み出した値」になる。

```c
/* gen_expr の switch 内 */
case NODE_UNARY:
    if (node->op == '*') {
        gen_expr(node->operand);    /* %rax = p の値（= ポインタ先のアドレス） */
        Type *t = expr_type(node->operand);
        emit_load(t->base_size);    /* そのアドレスから読み出す */
        return;
    }
    ...
```

`emit_load` のサイズは「p が指す先の要素サイズ」。`int *p` なら 4 バイト、`char *s` なら 1 バイト。

```c
static void emit_load(int sz) {
    if (sz == 1)      fprintf(out, "  movsbl (%%rax), %%eax\n");
    else if (sz == 4) fprintf(out, "  movl (%%rax), %%eax\n");
    else              fprintf(out, "  movq (%%rax), %%rax\n");
}
```

`movsbl` (move-sign-extend-byte-to-long) は 1 バイトを符号拡張して 4 バイト `%eax` に入れる。char の値は signed extension されるが、tiny-c の文字列処理ではこの選択で問題ない（ASCII 範囲の値しか扱わない）。

## 6. `IDENT` の `gen_expr`: 配列の decay

ここで扱うのは識別子を **値として読む**（rvalue 文脈）ときの `gen_expr(IDENT x)`。識別子が配列の場合は `int *p = a;` や `foo(a)` のように **配列名そのまま**（添字なし）で式に出るケースを指す。`a[2] = 3;` や `int b = a[1];` のように添字をつけて使うのは AST 上 `NODE_INDEX` になり、本節ではなく次節 7 の担当。

ch03 では単純に `movl -off(%rbp), %eax` でよかった。ch06 では型を見て分岐する:

```c
/* gen_expr の switch 内 */
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->type->is_array) {
        /* 配列名は decay する: 値 = 先頭要素のアドレス */
        gen_addr(node);
        return;
    }
    /* スカラー or ポインタ: アドレスを取って load */
    gen_addr(node);
    emit_load(type_size(v->type));
    return;
}
```

ここで C の悪名高い **配列 → ポインタ decay** が表現されている: 配列名を式の中で使うと、自動的に「先頭要素のアドレス」になる。

```c
int a[5];
int *p = a;       /* a が decay して &a[0] になる */
strlen(s);        /* s が char* なら、s 自身がアドレス */
```

実装上は **`gen_expr(IDENT 配列)` を `gen_addr(IDENT 配列)` にショートカットする** だけ。配列の「値」と「アドレス」は同一視される。

## 7. 添字 `a[i]` の codegen

`a[i]` の AST は `NODE_INDEX(lhs = a, rhs = i)`。`*p` と同じく文脈で扱いが分かれる:

- **rvalue**（`int b = a[2];` の右辺など、値として読む）── 入口は `gen_expr(NODE_INDEX)`。要素のアドレスを `gen_addr` で計算したあと、そのアドレスから **load** して値を `%eax` に読み込む。
- **lvalue**（`a[2] = 3;` の左辺など、書き込み先）── 入口は ASSIGN 側からの `gen_addr(NODE_INDEX)`。要素のアドレスを `%rax` に置くだけ（load しない）。続いて呼び出し側 (ASSIGN) が右辺の値を計算したあと、退避しておいたアドレスへ **store** する。

どちらの経路も **アドレス計算 = `gen_addr(NODE_INDEX)`** を共通して使う。これは概念的に `*(a + i*elem_size)` のうちの `(a + i*elem_size)` 部分。

### rvalue 経路 ── 値を読む `gen_expr(NODE_INDEX)`

```c
/* gen_expr の switch 内 */
case NODE_INDEX: {
    /* gen_addr で要素アドレスを %rax に置いてから、そこから load */
    gen_addr(node);                            /* %rax = &a[i] */
    Type *t = expr_type(node->lhs);
    emit_load(t->base_size);                   /* %rax = a[i] の値 */
    return;
}
```

### 共通のアドレス計算 ── `gen_addr(NODE_INDEX)`

rvalue 経路からも、lvalue 経路（ASSIGN）からも呼ばれる。**結果は `%rax` に要素のアドレス**。

```c
/* gen_addr の switch 内 */
case NODE_INDEX: {
    Type *t = expr_type(node->lhs);
    int es = t->base_size;                     /* 要素サイズ */
    /* ベースアドレスを取る */
    if (lhs is array IDENT)
        gen_addr(node->lhs);                   /* 配列: そのアドレス */
    else
        gen_expr(node->lhs);                   /* ポインタ: その値（アドレス）*/
    emit_push();                               /* ベースを退避 */
    gen_expr(node->rhs);                       /* %eax = i */
    fprintf(out, "  movslq %%eax, %%rax\n");   /* 64ビット符号拡張 */
    fprintf(out, "  imulq $%d, %%rax\n", es);  /* %rax = i * elem_size */
    emit_pop("%rcx");                          /* %rcx = ベース */
    fprintf(out, "  addq %%rcx, %%rax\n");     /* %rax = base + i*elem */
    return;
}
```

### `int a[5];` の `a[2]` を rvalue / lvalue で対比

| ソース | 経路 | `%rax` の最終値 | 後続処理 |
|--------|------|--------------|---------|
| `int b = a[2];`（右辺、rvalue） | `gen_expr(NODE_INDEX)` → 内部で `gen_addr` → さらに load | `a[2]` の **値** | `b` のスロットに store |
| `a[2] = 3;`（左辺、lvalue） | ASSIGN から `gen_addr(NODE_INDEX)` を直接呼ぶ | `a[2]` の **アドレス** | 右辺 `3` を計算してそのアドレスへ store |

どちらも **アドレス計算までは完全に同じ**。違いは「上に load を乗せて値にするか / 上に store を乗せて書き込むか」だけ。lvalue / rvalue の対称性が `gen_addr` を共通部品として浮かび上がらせる。

### ポインタ算術の具体値

- `int a[5]` で **`a[2]` のアドレス**: C で書くなら `&a[2]` または `a + 2`（`a` が `int *` に decay し、ポインタ算術で `+2` は 2 × `sizeof(int)` = 8 バイト進む）。バイト単位で見れば配列先頭 + 8 バイト。
- `char *s` で **`s[3]` のアドレス**: `s + 3`（`s` は `char *`、ポインタ算術で `+3` は 3 × `sizeof(char)` = 3 バイト進む）。バイト単位でも +3。

C のポインタ算術は型に応じて自動でスケーリングする（`int *` の `+1` は 4 バイト、`char *` の `+1` は 1 バイト）。我々の codegen はそれを **バイト数を直接計算して `addq` で足す** 形で実現している ── `imulq $4, %rax`（int の場合）か `imulq $1`（char、実装上は省略）でインデックスを **base_size** 倍してからアドレスに足す。

## 8. ASSIGN の codegen: gen_addr で lvalue 統一

**ASSIGN は常に左辺のアドレスを必要とする** ── そこに値を書き込むのが代入の仕事だから。左辺の形（`x` / `*p` / `a[i]`）が何であれ、まず `gen_addr(node->lhs)` を呼んで lvalue のアドレスを取り、それを退避しておいてから右辺を計算する。`x = 5`、`*p = 10`、`a[i] = 7` ── すべて同じパターンに集約される:

```c
/* gen_expr の switch 内 */
case NODE_ASSIGN: {
    gen_addr(node->lhs);    /* lhs のアドレスを計算 */
    emit_push();             /* スタックに退避 */
    gen_expr(node->rhs);    /* rhs の値を %eax に */
    emit_pop("%rcx");        /* %rcx = lhs アドレス */
    Type *t = expr_type(node->lhs);
    int sz = t->is_pointer ? 8 : t->base_size;
    emit_store(sz);          /* *(%rcx) = %eax / %rax */
    return;
}
```

`gen_addr` がどんな lvalue かを吸収してくれるので、ASSIGN の codegen は1パスで済む。これが lvalue / rvalue 分離の威力だ。ch03 では `if (lhs->kind != NODE_IDENT) error` という特別判定があったが、もう要らない。**lvalue かどうかは `gen_addr` が「not an lvalue」エラーを出す形に集約される**。

## 9. サイズ別 store

```c
static void emit_store(int sz) {
    if (sz == 1)      fprintf(out, "  movb %%al, (%%rcx)\n");
    else if (sz == 4) fprintf(out, "  movl %%eax, (%%rcx)\n");
    else              fprintf(out, "  movq %%rax, (%%rcx)\n");
}
```

- `movb`: 1 バイト store（char）
- `movl`: 4 バイト store（int）
- `movq`: 8 バイト store（pointer）

`%al` は `%rax` の最下位 1 バイト、`%eax` は下位 4 バイト、`%rax` 全体は 8 バイト。同じレジスタの違う部分を使い分けて適切なサイズで書き込む。

## 10. まとめ

- `gen_expr` は **rvalue（値）** を計算、`gen_addr` は **lvalue（アドレス）** を計算。
- `&x` は `gen_addr(x)`、`*p` は `gen_expr(p) + load`、`&*p == p`。
- 配列名は `gen_expr` 内で **decay** する: `gen_addr` を呼ぶだけ。
- `a[i]` のアドレスは `base + i × base_size`。`base_size` で配列/ポインタの種類を区別。
- ASSIGN は `gen_addr(lhs)` で lvalue を統一的に扱える。
- load/store のサイズ（`movb`/`movl`/`movq`、`movsbl` で読み込み符号拡張）は型から決まる。

## 次へ

次の節（`03_globals_strings.md`）では、グローバル変数（`.bss`）と文字列リテラル（`.rodata`）── つまり **スタックの外** にあるデータをどう扱うかを見る。
