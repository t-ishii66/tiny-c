# 02 — lvalue と rvalue ── `gen_addr` の世界

`x = 5` の左辺と右辺、`*p = 10` の左辺と右辺、`a[i]` ── これらを統一的に扱える視点が **lvalue / rvalue** だ。ch06 の codegen はこの2つを別々の関数で計算する。

## 1. 復習: rvalue は ch05 までの世界

ch06/00 で導入したように、判別の本質は **「アドレスを持つかどうか」** だ:

- **rvalue** ── アドレスを持たない、ただの値（`42`、`x + 1`、`f(2)` など）
- **lvalue** ── アドレスを持つ場所（`x`、`*p`、`a[i]` など）

ch05 までの `gen_expr` は「**式の値** を `%eax`（または `%rax`）に置く」ことが約束だった。`x + 1` でも `f(2)` でも `42` でも、終わったあと `%eax` に値が保持されている ── これが rvalue（アドレスは関係なく、値だけ）。

ch03 で唯一 lvalue っぽい操作があった: `NODE_ASSIGN` で `IDENT` の場合に `find_local(name)` でオフセットを求めて `movl %eax, -off(%rbp)` で書き込む、という流れ。これは **「変数という lvalue 限定」** の特殊処理だった。

ch06 では **アドレスを持つ場所**（= lvalue）が増える:

- `x` （変数のスロットのアドレス、ch03 から）
- `*p` （p が指す先のメモリ、新登場）
- `a[i]` （配列の i 番目要素のアドレス、新登場）

これらすべてを「**アドレスを計算する関数**」で扱いたい。それが **`gen_addr`** だ。

## 2. lvalue が必要になる場面 ── tiny-c では 2 つだけ

ch06 の codegen は次の2つの関数で構成される:

```c
/* gen_expr: 式の VALUE を %eax/%rax に置く（rvalue） */
static void gen_expr(Node *node);

/* gen_addr: lvalue の ADDRESS を %rax に置く */
static void gen_addr(Node *node);
```

**「アドレスが必要」になるのは tiny-c では次の 2 箇所だけ**（= lvalue を要求する場面）:

1. **`NODE_ASSIGN` の左辺** ── 書き込み先のアドレスが必要
2. **`&` 演算子のオペランド** ── アドレスそのものを値として返したい

それ以外の文脈（`return x;`、`y = x + 1;`、`f(x)` の中の `x` など）はすべて rvalue を扱う ── 値だけ見ればよく、アドレスはどこにあるか気にしない。

`gen_stmt` から呼ばれるのは常に `gen_expr` で、`gen_addr` が直接呼ばれることはない。

## 3. lvalue / rvalue の対応表 ── `int x` と `char *p` の違いに注目

ポインタや配列が入ると、**アドレスを持つ場所が複数** になる。`int x` のように場所が 1 つだけの型と、`char *p` のように場所が 2 つある型（ポインタ変数自身と、それが指す先）の違いが、lvalue / rvalue の組み合わせ数を変える。

### `int x;` の場合 ── アドレスを持つ場所はひとつ

| 記述 | lvalue (アドレス) | rvalue (値) |
|------|------------------|-------------|
| `x` | `&x`（x 変数のスロットのアドレス） | x のスロットから load した int |

### `char *p;` の場合 ── アドレスを持つ場所はふたつ

ポインタ変数 `p` には **アドレスを持つ場所が 2 つ** 関わる:
- **`p` 自身のスロット** ── ポインタ変数として確保された 8 バイトの領域。そのアドレスは `&p`
- **`p` が指す先** ── `p` の中身（= 別の場所のアドレス）が示すメモリ。そのアドレスは `p` の値そのもの

それぞれに lvalue / rvalue がある:

| 記述 | lvalue (アドレス) | rvalue (値) | 例 |
|------|------------------|-------------|----|
| `p` | `&p`（p 変数のスロットのアドレス） | p のスロットから load したアドレス値 | lvalue: `p = some_addr;` / rvalue: `q = p;` |
| `*p` | `p` の rvalue そのもの（= 指す先のアドレス）| そのアドレスから load した char | lvalue: `*p = 'x';` / rvalue: `c = *p;` |

注目点: **`*p` の lvalue は `p` の rvalue と一致する**。これが「`&` と `*` は対称」の正体だ ── `*` は load を 1 段足し、`&` は load を 1 段取り去る。

### `int a[5];` の場合 ── 配列特有のルール（decay）

> **decay（配列 → ポインタの暗黙変換）**: 配列名を式の中で **添字をつけずに使う** と、自動的に「先頭要素のアドレス」（= ポインタ値）として扱われる、という C の言語仕様。`int a[5]; int *p = a;` の `a` は `&a[0]` に変換されて `p` に代入される。配列そのものは「rvalue としての値」を持たず、rvalue 文脈ではアドレスに化ける ── これが decay の中身。

| 記述 | lvalue (アドレス) | rvalue (値) |
|------|------------------|-------------|
| `a` | `&a` = 配列先頭アドレス | rvalue は **アドレスとして decay**（= lvalue と同じ）|
| `a[i]` | base + i × elem_size | そのアドレスから load した値 |

### 規則のまとめ

- **`gen_addr` がアドレス計算までを担当**、その上に **load を重ねたものが `gen_expr`**
- ポインタ変数では `p` と `*p` の **2 階層** を意識する必要がある
- 配列名の rvalue は decay によって lvalue と同じになる ── これが C の特殊事情

これ以降の節では、まず **lvalue 側** で各記述のアドレス取得を見たあと（節 4）、**lvalue を使う側**（節 5）、最後に **rvalue 側**（節 6）と進む。

## 4. lvalue 側 ── アドレス取得 (`gen_addr`)

`gen_addr` の約束: **対象 (lvalue) のアドレスを `%rax` に置く**。3 つの case ── `IDENT`、`*`、`a[i]` ── すべてこの約束を守って return する。

```c
static void gen_addr(Node *node) {
    switch (node->kind) {
    case NODE_IDENT: { ... }                  /* 4.1 */
    case NODE_UNARY: if (node->op == '*') ... /* 4.2 */
    case NODE_INDEX: { ... }                  /* 4.3 */
    default:
        fprintf(stderr, "not an lvalue\n");
        exit(1);
    }
}
```

`default` ケースに来ると「lvalue でないノードのアドレスを取ろうとした」エラー。

### 4.1 `IDENT` のアドレス（`x`、`p`、`a` などの変数）

```c
/* gen_addr の switch 内 ── lvalue 側、アドレスを %rax に置く */
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->is_global)
        fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);    /* %rax = グローバル変数のアドレス */
    else
        fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset); /* %rax = ローカル変数のアドレス */
    return;
}
```

`int x` も `int *p` も `int a[5]` も、IDENT としては **同じ 1 命令** で扱う。

なお tiny-c では **どの変数もスタック上に 8 バイト単位で確保**する（オフセット計算をシンプルに保つため、`add_local` の中で `round_up_8` する）。型サイズが 8 の倍数でない場合はその分パディングで拡張される:

| 宣言 | 型サイズ | 8 バイト単位に拡張後 | `gen_addr(IDENT)` の結果 (`%rax`) |
|------|----------|--------------------|----------------------------------|
| `int x` | 4 | 8 | `x` のスロット先頭アドレス |
| `int *p` | 8 | 8 | `p` のスロット先頭アドレス（ポインタ自身を格納する場所） |
| `int a[5]` | 20 | 24 | 配列の先頭バイトのアドレス（= `&a[0]`）|

ローカル変数は `-off(%rbp)`、グローバル変数は `name(%rip)`（PC 相対参照）で取る。

**配列の場合のからくり**: `int a[5]` は型サイズ 20 バイト、8 バイト単位に拡張して 24 バイトを占有する。変数登録時の `add_local` が `frame_size` を 24 バイトに増やし、`v->offset` を「配列の先頭バイトと %rbp の差」にセットする。よって `leaq -off(%rbp), %rax` だけで `&a[0]` が得られる。`a + i * 要素サイズ` のような添字計算は出てこない ── それは `a[i]` を扱う `NODE_INDEX` の仕事（節 4.3）。

### 4.2 `*p` のアドレス

```c
/* gen_addr の switch 内 ── lvalue 側、*p のアドレスを %rax に置く */
case NODE_UNARY:
    if (node->op == '*') {
        /* *p のアドレスは「p の値（ポインタが指す先のアドレス）」そのもの。
           gen_expr(p) を呼べば %rax に p の値が入り、それがそのまま *p のアドレスになる。
           これで `*p = 10;` の左辺評価も、`&(*p)` の処理も同じ経路で扱える。 */
        gen_expr(node->operand);
        return;
    }
    break;
```

`*p` の **アドレス** が欲しいなら、`p` の **値** を取ればいい。なぜなら p の値そのものがそのアドレスだから（節 3 の対応表で「`*p` の lvalue は `p` の rvalue と一致」と書いた通り）。

### 4.3 `a[i]` のアドレス（`[]` 構文 ── 配列でもポインタでも）

ここで扱うのは AST 上の `NODE_INDEX`、つまり **`[]` 構文** が使われたとき。lhs が **配列名 `a`** でも **ポインタ `p`** でも同じ `NODE_INDEX` ノードになり、この 4.3 に来る:

- `a[i]` → `NODE_INDEX(a, i)` → 4.3
- `p[i]` → `NODE_INDEX(p, i)` → 4.3（lhs がポインタの分岐）
- `*(p + i)` → `NODE_UNARY * → NODE_BINARY +` → **4.3 には来ない**（後述）

下のコードの `if/else` は、4.3 に来た時点で「lhs が配列か / ポインタか」を分けている。配列名なら `gen_addr` で配列のアドレスを取り、ポインタ名なら `gen_expr` でポインタの値（= 指す先のアドレス）を取る。どちらも「ベースアドレス」として揃う。

```c
/* gen_addr の switch 内 ── lvalue 側、a[i] のアドレスを %rax に置く */
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

ここで `i` は **任意の式** ── 整数リテラル（`a[5]`）でも、変数（`a[n]`）でも、計算式（`a[j+1]`）でも、関数呼び出し（`a[f()]`）でも何でも入る。`gen_expr(node->rhs)` が実行時に i の値を `%eax` に求めるので、コンパイル時に index が決まっていなくても動的に対応できる。

lhs も **配列名でもポインタでもよい**。`Type` 構造体（`ast.h`）の `base_size` フィールドが「ポインタが指す先のサイズ」または「配列の要素サイズ」を保持しているので、どちらの場合も同じ式 `t->base_size` で正しい要素サイズが取れる:

- 配列名 `a`（型 `int[5]`）── `base_size` = 4（配列要素 `int` のサイズ）。ベースは `&a[0]`、`gen_addr` で取る。
- ポインタ `p`（型 `int *`）── `base_size` = 4（指す先 `int` のサイズ）。ベースは `p` の値、`gen_expr` で取る。

そのうえで `(base) + i × elem_size` を計算してアドレスを `%rax` に置く。

### 4.3 に至る経路 ── `a[i]` と `*(p+i)` で違う

`gen_expr` が入口、というのは ch06/02 全体の前提（節 2 参照）。そこから `case NODE_INDEX`（この 4.3）に至るのは **`a[i]` 構文（`[]`）のときだけ**。`*(p+i)` は構文が違うので別の case を経由し、4.3 には来ない。

**`a[i] = X;` の場合**（lvalue 経路、ASSIGN の左辺）:

```
gen_stmt(EXPR_STMT)
└─ gen_expr(NODE_ASSIGN)        ← 入口
    ├─ gen_addr(node->lhs)       ← lhs = NODE_INDEX(a, i)
    │  └─ case NODE_INDEX        ← ★ 4.3 に到達
    │     ├─ gen_addr(IDENT a)   ← 4.1（配列のベースアドレス）
    │     ├─ gen_expr(IDENT i)
    │     ├─ imulq $4, %rax     ← スケーリング!
    │     └─ addq → %rax = &a[i]
    └─ ... store
```

**`*(p+i) = X;` の場合**（lvalue 経路、同じ ASSIGN の左辺）:

```
gen_stmt(EXPR_STMT)
└─ gen_expr(NODE_ASSIGN)        ← 入口
    ├─ gen_addr(node->lhs)       ← lhs = NODE_UNARY '*'
    │  └─ case NODE_UNARY '*'    ← 4.2 に到達（4.3 ではない!）
    │     └─ gen_expr(operand)   ← operand = NODE_BINARY '+'
    │        └─ case NODE_BINARY ← ここの処理は 4.3 にも 4.2 にも入らず、ただの addl
    │           ├─ gen_expr(rhs = i)
    │           ├─ gen_expr(lhs = p)
    │           └─ addl %ecx, %eax  ← スケーリングなし!
    └─ ... store
```

決定的な違いは **`NODE_BINARY '+'` の codegen** にある（`steps/ch06/src/codegen.c` の `case NODE_BINARY` 内）── tiny-c はここで型を見ずに `addl` を出すだけ。だから `*(p+i)` は「p のバイト + i」になり、要素サイズで進まない。一方 `a[i]` は `NODE_INDEX` 経由でこの 4.3 に来るので、ちゃんと `imulq $elem_size` でスケーリングされる。

### C 標準との対比

C 標準では `p` が `T *` のとき、**`p + i` は i × sizeof(T) バイト進む**（型に応じた自動スケーリング）。だから `a[i]` と `*(a + i)` は完全に等価で、どちらも i 番目の要素にアクセスできる。tiny-c は簡略化のためそのスケーリングを `NODE_INDEX`（`[]` 構文）でしか実装していない:

| 構文 | tiny-c の経路 | スケーリング | C 標準との関係 |
|------|--------------|------------|--------------|
| `a[i]` | `NODE_INDEX` (4.3) → `imulq $elem_size` | あり | 標準通り |
| `*(p + i)` | `NODE_UNARY '*'` (4.2) → `NODE_BINARY '+'` → `addl` | なし（バイト単位）| **標準と異なる** |

つまり tiny-c で要素アクセスを書くときは `a[i]` 構文を使うのが正解。`*(p + i)` のような書き方は意図通りに動かない。

> **読者への課題**: 教育用の tiny-c は「`a[i]` / `p[i]` でスケーリングが効くなら要素アクセスには十分」と割り切って `+` のポインタ算術を実装していない。これを C 標準どおりに動かす拡張は読者の宿題として残しておく ── `case NODE_BINARY '+'` の中で lhs / rhs の型を見て、ポインタ側に応じて `imulq $base_size` を挟むだけ。実装の難易度はそれほど高くない。

具体値の例:
- `int a[5]` で `a[2]` のアドレス: `&a[0] + 2 × 4 = &a[0] + 8` バイト
- `char *s` で `s[3]` のアドレス: `s + 3 × 1 = s + 3` バイト

これらはコンパイル時計算ではなく、生成されたアセンブリが実行時に同じ式を評価する。`a[2]` のように index が定数でも、tiny-c はあえて畳み込まず `imulq $4, %rax` を出す（最適化は ch07 で導入）。

## 5. lvalue を使う側 ── `NODE_ASSIGN` と `&`

節 4 で得た「アドレス」を実際に使うのが ASSIGN と `&`。

### 5.1 `NODE_ASSIGN`

**ASSIGN は常に左辺のアドレスを必要とする** ── そこに値を書き込むのが代入の仕事だから。左辺の形（`x` / `*p` / `a[i]`）が何であれ、まず `gen_addr(node->lhs)` を呼んで lvalue のアドレスを取り、それを退避してから右辺を計算する。

```c
/* gen_expr の switch 内 ── lvalue を内部で使って書き込む */
case NODE_ASSIGN: {
    gen_addr(node->lhs);    /* lhs のアドレスを計算（節 4） */
    emit_push();            /* スタックに退避 */
    gen_expr(node->rhs);    /* rhs の値を %eax に */
    emit_pop("%rcx");       /* %rcx = lhs アドレス */
    Type *t = expr_type(node->lhs);
    int sz = t->is_pointer ? 8 : t->base_size;
    emit_store(sz);         /* *(%rcx) = %eax / %rax */
    return;
}
```

`x = 5;`、`p = some_addr;`、`*p = 10;`、`a[i] = 7;` ── すべて同じパターン。**lvalue かどうかは `gen_addr` が「not an lvalue」エラーを出す形に集約される**。

### 5.2 `&` 演算子

`&x` の値は「`x` のアドレス」。これは `gen_addr(x)` そのもの:

```c
/* gen_expr の switch 内 ── &x の rvalue（アドレス値）を返す */
case NODE_UNARY:
    if (node->op == '&') {
        gen_addr(node->operand);    /* %rax = x のアドレス */
        return;
    }
    ...
```

`gen_expr` の中で `gen_addr` を呼ぶだけ。普段の識別子読み出し（`gen_expr(x)`、節 6.1）なら `gen_addr` で得たアドレスからさらに値を load する（= `%rax` に入っているアドレスを参照して、そのアドレスが指すメモリの内容を読み出し、`%rax` に上書きする）が、`&x` ではその load をスキップする。結果として **`%rax` にはアドレスがそのまま残り、そのアドレスが指す先のデータには触らない**。

## 6. rvalue 側 ── 値の取得 (`gen_expr`)

各記述（`x` / `*p` / `a[i]`）の **値** を `%eax` / `%rax` に置く側。基本パターンは「`gen_addr` でアドレスを取って、そこから load」。

### 6.1 `IDENT` を値として読む（配列の decay も）

ここで扱うのは **識別子だけが式に出ている** ケース ── `x`、`p`、`a` のように添字や演算子をつけない形。`a[i]` のような添字つきは parser がそもそも `NODE_INDEX` として組み立てるので、この `case NODE_IDENT` には来ない（節 6.3 の担当）。

| ソース表記 | parser が作る AST | 担当 case |
|-----------|------------------|----------|
| `a` 単体 | `NODE_IDENT("a")` | この 6.1 |
| `a[i]` | `NODE_INDEX(a, i)` | 節 6.3 |

つまり「添字あり / なし」の区別は **AST 構築の時点ですでに済んでいる**ので、codegen の各 case はそれぞれの形に集中できる。

ch03 では `gen_expr(IDENT x)` は単純に `movl -off(%rbp), %eax` でよかった。ch06 では型を見て分岐する:

```c
/* gen_expr の switch 内 ── rvalue 側、識別子の値を %eax/%rax に置く */
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

ここで C の悪名高い **配列 → ポインタ decay** が表現されている: 配列名を **添字をつけずに式の中で使う**（`int *p = a;` `foo(a)` など）と、自動的に「先頭要素のアドレス」になる。

```c
int a[5];
int *p = a;       /* a が decay して &a[0] になる */
strlen(s);        /* s が char* なら、s 自身がアドレス */
```

実装上は **`gen_expr(IDENT 配列)` を `gen_addr(IDENT 配列)` にショートカットする** だけ。配列の「値」と「アドレス」は同一視される ── これが節 3 の表で `a` の rvalue が「アドレスとして decay」と書かれていた理由。

`a[i]` のように添字つきで使うのは AST 上 `NODE_INDEX` で別経路（節 6.3）。

### 6.2 `*p` を値として読む

`*p` の値は「p に格納されているアドレスから読み出した値」。

```c
/* gen_expr の switch 内 ── rvalue 側、*p の値を %eax/%rax に置く */
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

節 4.2 で見た `gen_addr(*p)` と比べると、違いは「load を上に乗せるか乗せないか」だけ。これが「`*` は load を足す」の意味。

### 6.3 `a[i]` を値として読む

```c
/* gen_expr の switch 内 ── rvalue 側、a[i] の値を %eax/%rax に置く */
case NODE_INDEX: {
    /* gen_addr で要素アドレスを %rax に置いてから、そこから load */
    gen_addr(node);                            /* %rax = &a[i]（節 4.3） */
    Type *t = expr_type(node->lhs);
    emit_load(t->base_size);                   /* %rax = a[i] の値 */
    return;
}
```

節 4.3 で見た `gen_addr` の上に load を 1 段乗せるだけ。

### 同じ `a[2]` を rvalue / lvalue で対比

| ソース | 経路 | `%rax` の最終値 | 後続処理 |
|--------|------|--------------|---------|
| `int b = a[2];`（右辺、rvalue） | `gen_expr(NODE_INDEX)` → 内部で `gen_addr` → さらに load | `a[2]` の **値** | `b` のスロットに store |
| `a[2] = 3;`（左辺、lvalue） | ASSIGN から `gen_addr(NODE_INDEX)` を直接呼ぶ | `a[2]` の **アドレス** | 右辺 `3` を計算してそのアドレスへ store |

**アドレス計算までは完全に同じ**。違いは「アドレス計算のあとに load を続けて値を読むか / store を続けて書き込むか」だけ。lvalue / rvalue の対称性が `gen_addr` を共通部品として浮かび上がらせる。

## 7. サイズ別 load / store

### `emit_load`

```c
static void emit_load(int sz) {
    if (sz == 1)      fprintf(out, "  movsbl (%%rax), %%eax\n");
    else if (sz == 4) fprintf(out, "  movl (%%rax), %%eax\n");
    else              fprintf(out, "  movq (%%rax), %%rax\n");
}
```

`movsbl` (move-sign-extend-byte-to-long) は 1 バイトを符号拡張して 4 バイト `%eax` に入れる。char の値は signed extension されるが、tiny-c の文字列処理ではこの選択で問題ない（ASCII 範囲の値しか扱わない）。

### `emit_store`

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

## 8. まとめ

- lvalue が要求されるのは **`NODE_ASSIGN` の左辺** と **`&` のオペランド** の 2 箇所だけ。それ以外は rvalue 文脈。
- `gen_expr` は **rvalue（値）**、`gen_addr` は **lvalue（アドレス）** を計算 ── どちらも結果は `%rax`（or `%eax`）に置く。
- ポインタ変数 `p` は **2 つの場所**（`p` 自身と `*p` が指す先）を持つ。`int x` のような単一の場所しか持たない型と違って、`p` と `*p` それぞれに lvalue / rvalue がある。
- 配列名は rvalue 文脈で **decay**（先頭要素のアドレスになる）── `gen_expr(IDENT 配列)` は `gen_addr` にショートカット。
- ASSIGN は `gen_addr(lhs)` で左辺の形（`x` / `*p` / `a[i]`）を吸収。lhs の場合分けが消える。
- load/store のサイズ（`movb`/`movl`/`movq`、`movsbl` で読み込み符号拡張）は型から決まる。
- **規則一行**: rvalue = lvalue + load。`&` は load を取り去る、`*` は load を足す。

## 次へ

次の節（`03_globals_strings.md`）では、グローバル変数（`.bss`）と文字列リテラル（`.rodata`）── つまり **スタックの外** にあるデータをどう扱うかを見る。
