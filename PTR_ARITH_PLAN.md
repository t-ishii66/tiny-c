# PTR_ARITH_PLAN — ポインタ算術（`p + i`）を直すときの見積もりと変更案

`PROBLEMS.md` の先頭の問題「ポインタと整数の `+` / `-` が 32 ビット演算で、アドレスの上位 32 ビットが消える」について、直すとしたらどこがどう変わるかをまとめた。**本文もソースもまだ変更していない。** ここに書いたソースの差分は、作業用のコピーに当てて動作を確かめたものである。

## 1. 結論

- **作業量の見積もり: 約 3.5 時間（半日弱）**。内訳は次のとおり。

| 作業 | 対象 | 規模 | 目安 |
|------|------|------|------|
| ソース修正 | `steps/ch06/src/codegen.c`、`steps/ch07/src/codegen.c` | 各 +46 行、削除 0 行。両章とも同じ差分 | 30 分 |
| テスト追加 | `test/cases/06_ptr_arith.c`（新規） | 1 ファイル、10 行 | 15 分 |
| 本文（日本語） | `docs/jp/ch06/02_lvalue.md` の 6 か所、`docs/jp/ch06/00_overview.md` の 1 か所、`docs/jp/ch06/04_build.md` の 2 か所（5 節） | 段落の書き換えと追加 | 1.5 時間 |
| 図 6-2 | `tools/diagrams/gen_trees.py` の定義を直して、日本語版・英語版を作り直す | 定義 10 行ほど | 15 分 |
| 確認 | Docker（x86-64 Linux）で `make test`、`check-github-render` | — | 30 分 |
| 英語版 | `docs/en/` | 翻訳パス（`doc-translator-ja-en`）に任せる | 待つだけ |

- **第7章の本文は変えなくてよい**。7.5 の差分はバックパッチとスコープの部分だけで、ch06 で直したものがそのまま引き継がれる。最適化（`x + 0 → x` など）も、ポインタ算術と干渉しない（4 節）。
- **`PROBLEMS.md` に書かれていない問題を 1 つ見つけた**。`expr_type` が `NODE_BINARY` に対して常に `int` を返すので、`+` を 64 ビットにするだけでは `*(s + 1)`（`char *s`）が 1 バイトではなく 4 バイト読みになる。`expr_type` も一緒に直す必要がある（3.1 節）。

## 2. いまの挙動（再現結果）

次のプログラムを今の ch06 でコンパイルし、Docker（`gcc:13`、`linux/amd64`）で動かした。

```c
int main() {
    int a[3];
    int *p = a;
    a[1] = 7;
    return *(p + 1);
}
```

`*(p + 1)` の部分の出力はこうなる。

```asm
  movl $1, %eax
  pushq %rax
  leaq -24(%rbp), %rax
  movq (%rax), %rax       # %rax = p（8 バイトのアドレス）
  popq %rcx
  addl %ecx, %eax         # 下位 32 ビットで足し、結果を %rax にゼロ拡張して書く
                          # → p の上位 32 ビット（0x00007ffd など）が消える
  movl (%rax), %eax       # 上位が消えたアドレスを読む
```

`addl` は「下位 32 ビットだけ計算して、上位はそのまま残す」のではない。x86-64 では、32 ビットレジスタ（`%eax`）に書くと、64 ビットレジスタ（`%rax`）の上位 32 ビットが必ず 0 になる（本文の 2.2.9 と 5.4.6 で説明済みの規則）。8 ビット・16 ビットのレジスタに書いたときは上位が残るので、32 ビットだけの動きである。

```
実行前   %rax = 0x00007ffd_e8a01230   (p)
         %rcx = 0x00000000_00000001   (1)
addl 後  %rax = 0x00000000_e8a01231   ← 上位の 0x00007ffd が消える
```

結果は **Segmentation fault（終了コード 139）**。スケーリングがない（`+ 1` が `+ 4` にならない）以前に、アドレスそのものが壊れている。

## 3. ソースコードの変更

ch06 と ch07 の `codegen.c` に、**同じ差分**を当てる。下は ch06 の行番号で書いた（ch07 では `expr_type` が 149 行目、`case NODE_BINARY` が 339 行目にある）。

考え方は 1 つだけで、**片側がポインタ（または配列）の `+` / `-` には、6.2.4.3 の `NODE_INDEX` と同じ命令列を出す**。`movslq` で `i` を 64 ビットに広げ、`imulq $elem_size` を掛けて、`addq` で足す。

### 3.1 `expr_type` ── `p + i` をポインタ型にする

いまは `NODE_BINARY` が常に `int` を返す。そのため `*(p + 1)` の load サイズは `int` の 4 バイトに決め打ちになり、`char *s` の `*(s + 1)` が 4 バイト読みになる。

```diff
@@ -112,6 +112,11 @@
+/* Pointers and arrays both act as an address in + and -. */
+static int is_addr_type(Type *t) {
+    return t && (t->is_pointer || t->is_array);
+}
+
 /* Type of an expression — needed for choosing load size and array decay.
    We track variable types in symbol tables; for derived expressions we
    infer from operator structure. */
 static Type *expr_type(Node *node) {
@@ -146,6 +151,13 @@
     case NODE_ASSIGN:
         return expr_type(node->lhs);
     case NODE_BINARY:
+        /* p + i, i + p, p - i keep the pointer's type */
+        if (node->op == '+' || node->op == '-') {
+            Type *l = expr_type(node->lhs), *r = expr_type(node->rhs);
+            if (is_addr_type(l)) return type_ptr(l->base_size);
+            if (node->op == '+' && is_addr_type(r)) return type_ptr(r->base_size);
+        }
+        return type_int();
     case NODE_CALL:
     default:
         return type_int();
```

### 3.2 `gen_ptr_arith` ── 新しい関数

`push_args` のあと、`gen_expr` の前に置く。中身は `gen_addr` の `case NODE_INDEX` とほぼ同じで、違うのは `-` のときに `subq` を使うところだけ。

```c
/* Pointer arithmetic: p + i, i + p, p - i.
   Same steps as a[i] in gen_addr: scale i by the element size,
   then add in 64 bits so the upper half of the address survives. */
static void gen_ptr_arith(Node *node) {
    Node *ptr = node->lhs, *idx = node->rhs;
    if (!is_addr_type(expr_type(ptr))) {    /* i + p */
        ptr = node->rhs;
        idx = node->lhs;
    }
    if (is_addr_type(expr_type(idx)) || (node->op == '-' && ptr == node->rhs)) {
        fprintf(stderr, "unsupported pointer arithmetic\n");
        exit(1);
    }
    int es = expr_type(ptr)->base_size;
    gen_expr(ptr);                       /* %rax = address */
    emit_push();
    gen_expr(idx);                       /* %eax = i */
    fprintf(out, "  movslq %%eax, %%rax\n");          /* sign-extend to 64-bit */
    if (es != 1)
        fprintf(out, "  imulq $%d, %%rax\n", es);     /* %rax = i * elem_size */
    emit_pop("%rcx");                    /* %rcx = address */
    if (node->op == '+') {
        fprintf(out, "  addq %%rcx, %%rax\n");        /* %rax = p + i*elem */
    } else {
        fprintf(out, "  subq %%rax, %%rcx\n");        /* %rcx = p - i*elem */
        fprintf(out, "  movq %%rcx, %%rax\n");
    }
}
```

### 3.3 `gen_expr` の `case NODE_BINARY` ── 入口で振り分ける

```diff
@@ -302,6 +343,11 @@
     case NODE_BINARY:
+        if ((node->op == '+' || node->op == '-') &&
+            (is_addr_type(expr_type(node->lhs)) || is_addr_type(expr_type(node->rhs)))) {
+            gen_ptr_arith(node);
+            return;
+        }
         gen_expr(node->rhs);
         emit_push();
         gen_expr(node->lhs);
```

`int` 同士の `+` / `-` は、ch02 からの `addl` / `subl` のまま。

### 3.4 決めたこと（変えてもよい）

| 書き方 | C 標準 | この案での扱い |
|--------|--------|----------------|
| `p + i` / `i + p` / `p - i` | 要素サイズでスケーリング | 対応する |
| `p - q`（ポインタ同士） | 要素数（`ptrdiff_t`）を返す | **コンパイルエラー**（`unsupported pointer arithmetic`）。いまは黙って `subl` している |
| `i - p`、`p + q` | エラー | コンパイルエラー |

`p - q` を入れるなら、`subq` のあとに要素サイズで割る（`int` なら `sarq $2`）だけで、10 行ほど増える。この案では入れず、本文の「読者への課題」に回した（5.5 節）。

### 3.5 確かめたこと

作業用のコピー（`steps/ch06`、`steps/ch07` を複製して 3.1〜3.3 を当てたもの）で確認した。

- **警告なしでビルドできる**（`-Wall`）。
- **既存のテストケースは、出力アセンブリが 1 行も変わらない**。`test/cases/01_*`〜`06_*` は ch06 で、全ケースは ch07 で（最適化あり・`--no-opt` の両方）、修正前後の `.s` を比べた。いまのテストにはポインタ算術を使うケースがないためである。
- **Docker（x86-64 Linux）で実行した結果**:

| プログラム | 修正前 | 修正後 ch06 | 修正後 ch07 |
|------------|--------|-------------|-------------|
| 2 節の `*(p + 1)`（期待値 7） | Segmentation fault | 7 | 7 |
| 6 節のテスト（期待値 73） | — | 73 | 73 |
| `p - q`（ポインタ同士） | 黙ってコンパイル | エラーで止まる | エラーで止まる |

## 4. 第7章への影響

- **`optimize.c` は変えなくてよい**。代数的単純化の `x + 0 → x` / `0 + x → x` / `x - 0 → x` は、`x` がポインタでもそのまま正しい。定数畳み込みは両辺が `INT_LIT` のときだけなので、ポインタは関係しない。`p + (1 + 1)` は先に `p + 2` に畳まれ、そのあと `gen_ptr_arith` を通る。
- **ピープホールも変えなくてよい**。`gen_ptr_arith` は `pushq %rax` / `popq %rcx` の組を出すので、ほかの二項演算と同じく `movq %rax, %rcx` に縮む。
- **第7章の本文も変えなくてよい**。`docs/jp/ch07/05_build.md` の 7.5.1 は ch06 からの差分（バックパッチとスコープ）だけを載せている。7.5 の「`lexer.l / parser.y / ast.h / ast.c` は ch06 と完全に同じ」も、そのまま成り立つ。

## 5. 本文の変更（元の文章と改良案）

ソースを 3 節のとおり直す前提で、本文を書き換える箇所を並べる。`PROBLEMS.md` に書いた行番号と見出し番号（§4.3 など）は、見出しの番号付け（d832787）と図の SVG 化（85ff160）より前のものなので、ここでは今の行番号と見出し番号で書く。本文中のコード片のコメントは日本語にする（本文の言語に合わせる）。

全体の方針は 3 つ。

- **問題の説明は 1 か所にまとめる**。「なぜ `addl` ではだめか」は 5.3（6.2.4.3 に至る経路）だけで説明し、ほかの箇所はそこを指す。
- **すでにある説明につなぐ**。32 ビットレジスタに書くと上位 32 ビットが 0 になる規則は、2.2.9 と 5.4.6 で説明済みである。第6章で新しく説明せず、5.4.6 を指して「`int` のときは便利だった規則が、アドレスでは裏目に出る」という流れにする。
- **要素サイズの出どころをはっきりさせる**。`[]` 構文が要素サイズを知っているのではなく、宣言時に `Type.base_size` に記録した値を `expr_type` で読んでいる。`+` も同じ値を読むようにした、と書く。

| 節 | ファイル | 場所 | 変更の大きさ |
|----|----------|------|--------------|
| 5.1 | `ch06/02_lvalue.md` | 6.2.4.3 冒頭の箇条書き（169 行目） | 1 行 |
| 5.2 | `ch06/02_lvalue.md` | 6.2.4.3 のコードのあと（198 行目） | 1 段落を追加 |
| 5.3 | `ch06/02_lvalue.md` | 6.2.4.3 に至る経路（214 行目） | 1 段落を差し替え（コード片つき） |
| 5.4 | `ch06/02_lvalue.md` | 図 6-2 | 図の下半分 |
| 5.5 | `ch06/02_lvalue.md` | C 標準との対比（216〜220 行目） | 2 段落を差し替え |
| 5.6 | `ch06/02_lvalue.md` | 6.2.6.2 `*p` を値として読む（329 行目） | 1 文を追加 |
| 5.7 | `ch06/02_lvalue.md` | 6.2.8 まとめ（400 行目） | 箇条を 1 つ追加 |
| 5.8 | `ch06/00_overview.md` | strlen の例のあと（131 行目） | 1 文を差し替え |
| 5.9 | `ch06/04_build.md` | 6.4.4 の新登場一覧、6.4.6 の実行例 | 2 行を変更、実行例を追加 |

---

### 5.1 6.2.4.3 冒頭の箇条書き（`ch06/02_lvalue.md` 169 行目）

**元の文章**:

> - `*(p + i)` → `NODE_UNARY * → NODE_BINARY +` → **6.2.4.3 には来ない**（後述）

**改良案**:

> - `*(p + i)` → `NODE_UNARY * → NODE_BINARY +` → **6.2.4.3 には来ない**。別の経路で同じ計算をする（後述）

**ねらい**: 経路が違うことは直したあとも変わらないので、そのまま残す。「来ない＝動かない」と読まれないよう、「同じ計算をする」と先に言っておく。

---

### 5.2 6.2.4.3 のコードのあと（`ch06/02_lvalue.md` 198 行目）

**元の文章**:

> そのうえで `(base) + i × elem_size` を計算してアドレスを `%rax` に置く。

**改良案**（元の文はそのまま残し、そのあとに段落を 1 つ足す）:

> そのうえで `(base) + i × elem_size` を計算してアドレスを `%rax` に置く。
>
> **`movslq` が要る理由**: `gen_expr` は `i` を `%eax`（32 ビット）に置く。5.4.6 で見たとおり、32 ビットレジスタに書くと `%rax` の上位 32 ビットは 0 になる。`i = -1` なら `%eax = 0xffffffff` で、`%rax` 全体では `0x00000000_ffffffff`（約 42 億）。これをそのまま 64 ビットのアドレスに足すと大きく外れる。`movslq` は `%eax` を符号付きの値として 64 ビットに広げ、`%rax = 0xffffffff_ffffffff`（= −1）にする。アドレスの計算は、64 ビットにそろえてから行う。

**ねらい**: 今の本文はコメント `/* 64ビット符号拡張 */` だけで、なぜ要るかを書いていない。ここで 5.4.6 の規則を思い出させておくと、5.3 の「`addl` だとアドレスが壊れる」がすぐ分かる。

---

### 5.3 6.2.4.3 に至る経路（`ch06/02_lvalue.md` 214 行目）

見出しと、図 6-1 / 6-2 までの段落（202〜212 行目）はそのまま残す。差し替えるのは図のあとの段落。

**元の文章**:

> 決定的な違いは **`NODE_BINARY '+'` の codegen** にある（`steps/ch06/src/codegen.c` の `case NODE_BINARY` 内）── tiny-c はここで型を見ずに `addl` を出すだけ。だから `*(p+i)` は「p のバイト + i」になり、要素サイズで進まない。一方 `a[i]` は `NODE_INDEX` 経由でこの 6.2.4.3 に来るので、ちゃんと `imulq $elem_size` でスケーリングされる。

**改良案**:

> 違いは、`*(p+i)` が **`NODE_BINARY '+'` の codegen** を通ることにある（`steps/ch06/src/codegen.c` の `case NODE_BINARY`）。ここは ch02 で書いた、`int` 同士を `addl` で足すコードだった。そのまま `p + i` に使うと、2 つの点で壊れる。
>
> 1. **アドレスの上位 32 ビットが消える**。`addl %ecx, %eax` は下位 32 ビットで足し、結果を `%rax` にゼロ拡張して書く（5.4.6 の規則）。`int` を扱うあいだは便利だったこの規則が、64 ビットのアドレスでは裏目に出る。`p = 0x00007ffd_e8a01230` に 1 を足すと、結果は `0x00000000_e8a01231` になり、まったく別のアドレスを読み書きして落ちる。`p + 0` でも落ちる。
> 2. **要素サイズを掛けていない**。`int *p` なら `p + 1` は 4 バイト先を指すべきだが、`addl` は 1 しか足さない。
>
> そこで、片側がポインタ（または配列）の `+` / `-` は、`gen_ptr_arith` が引き受ける。
>
> ```c
> /* gen_expr の switch 内 ── 片側がポインタの + / - はここで振り分ける */
> case NODE_BINARY:
>     if ((op が '+' か '-') && (lhs か rhs がポインタ・配列)) {
>         gen_ptr_arith(node);       /* p + i, i + p, p - i */
>         return;
>     }
>     ...                            /* int 同士は ch02 からの addl / subl のまま */
>
> /* gen_ptr_arith ── 6.2.4.3 の NODE_INDEX と同じ手順 */
> int es = expr_type(ptr)->base_size;        /* 要素サイズ（宣言時の Type から） */
> gen_expr(ptr);                             /* %rax = p の値（アドレス） */
> emit_push();
> gen_expr(idx);                             /* %eax = i */
> fprintf(out, "  movslq %%eax, %%rax\n");   /* 64 ビットに符号拡張 */
> fprintf(out, "  imulq $%d, %%rax\n", es);  /* %rax = i * elem_size */
> emit_pop("%rcx");                          /* %rcx = p */
> fprintf(out, "  addq %%rcx, %%rax\n");     /* %rax = p + i*elem（- なら subq） */
> ```
>
> 要素サイズは、`[]` 構文が知っているわけではない。宣言のときに `Type.base_size` に記録した値を、`expr_type` で読んでいるだけである。`gen_ptr_arith` も同じ値を読む。だから経路は違っても、出てくる命令列は `a[i]` と同じになる。配列名 `a` も `gen_expr` で先頭アドレスになるので、`*(a + i)` も同じように動く。

**ねらい**: 元の文章の「p のバイト + i」は、1 バイトずれるだけに読める。実際は上位 32 ビットが消えて落ちるので、そこを正確に書く。説明は 5.4.6 を指して短くする。

---

### 5.4 図 6-2（`ch06/02_lvalue.md` 212 行目の画像）

**元の図**（`case NODE_BINARY` から下）:

```
case NODE_BINARY        6.2.4.3 にも 6.2.4.2 にも入らず、ただの addl
├─ gen_expr(rhs = i)
├─ gen_expr(lhs = p)
└─ addl %ecx, %eax      スケーリングなし！
```

**改良案**:

```
case NODE_BINARY        片側がポインタなので gen_ptr_arith へ
└─ gen_ptr_arith(node)
   ├─ gen_expr(lhs = p)
   ├─ gen_expr(rhs = i)
   ├─ imulq $4, %rax    スケーリング！
   └─ addq %rcx, %rax   64 ビットで足す
```

図 6-1（`a[i]`）の `imulq $4, %rax` にも「スケーリング！」と付いているので、2 枚を並べると「経路は違うが、最後は同じ命令列になる」ことが見える。

**作り方**: `tools/diagrams/gen_trees.py` の 310 行目からの定義のうち、`case NODE_BINARY` の部分を次のように変える。そのあと `python3 tools/diagrams/gen_trees.py jp` と `en` を実行する。`PLAN.md` の図一覧にある ASCII 版（図 6-2）も、そろえるなら同じ形に直す。

```python
N('case NODE_BINARY', note=t('片側がポインタなので gen_ptr_arith へ', 'one side is a pointer, so go to gen_ptr_arith'), children=[
    N('gen_ptr_arith(node)', children=[
        N('gen_expr(lhs = p)', 'leaf'),
        N('gen_expr(rhs = i)', 'leaf'),
        N('imulq $4, %rax', 'hot', note=t('スケーリング！', 'scaling!')),
        N('addq %rcx, %rax', 'hot', note=t('64 ビットで足す', 'add in 64 bits')),
    ]),
]),
```

---

### 5.5 C 標準との対比（`ch06/02_lvalue.md` 216〜220 行目）

**元の文章**:

> C 標準では `+` のポインタ算術が型に応じてスケーリングするので、`a[i] ≡ *(a + i) ≡ *(p + i)` が成立する。tiny-c は `NODE_INDEX`（`[]` 構文）でしかスケーリングを実装していないので、`*(a + i)` や `*(p + i)` のように `+` を経由する書き方はバイト単位の加算になり標準と挙動が違う。要素アクセスは `a[i]` / `p[i]` 構文を使うこと。
>
> > **読者への課題**: `+` のスケーリングを実装するなら、`case NODE_BINARY '+'` で lhs / rhs の型を見てポインタ側に応じて `imulq $base_size` を挟むだけ。

**改良案**:

> C 標準では `+` のポインタ算術が型に応じてスケーリングするので、`a[i] ≡ *(a + i) ≡ *(p + i)` が成立する。tiny-c も `gen_ptr_arith` でこれにそろえた。`[]` で書いても `+` で書いても、同じ要素を指す。
>
> 対応しないのは、ポインタ同士の引き算 `p - q` である（C では間の要素数を返す）。`i - p` と `p + q` は C でもエラーなので、tiny-c もコンパイルエラーにする。
>
> > **読者への課題**: `p - q` を実装するなら、`subq` で差を取ったあと要素サイズで割ればよい（`int` なら `sarq $2, %rax`）。

**ねらい**: 元の文章は「`+` は使わず `[]` を使え」という制限の説明だった。直したあとは、標準にそろったことと、残った制限（`p - q`）を書く。課題も、残った制限に差し替える。

---

### 5.6 6.2.6.2 `*p` を値として読む（`ch06/02_lvalue.md` 329 行目）

**元の文章**:

> `emit_load` のサイズは「p が指す先の要素サイズ」。`int *p` なら 4 バイト、`char *s` なら 1 バイト。

**改良案**（元の文のあとに 1 文足す）:

> `emit_load` のサイズは「p が指す先の要素サイズ」。`int *p` なら 4 バイト、`char *s` なら 1 バイト。`*(p + 1)` のように `*` の中身が式でも同じで、`expr_type` が `p + 1` を「`p` と同じポインタ型」と判定する。だから `char *s` の `*(s + 1)` も 1 バイト読みになる。

**ねらい**: 3.1 節の `expr_type` の変更を説明する場所。これがないと、`+` を直しても `*(s + 1)` が 4 バイト読みのままになる。

---

### 5.7 6.2.8 まとめ（`ch06/02_lvalue.md` 400 行目）

**元の文章**:

> - load/store のサイズ（`movb`/`movl`/`movq`、`movsbl` で読み込み符号拡張）は型から決まる。

**改良案**（そのあとに箇条を 1 つ足す）:

> - load/store のサイズ（`movb`/`movl`/`movq`、`movsbl` で読み込み符号拡張）は型から決まる。
> - 要素サイズ（`Type.base_size`）も型から決まる。`a[i]` も `*(p + i)` も、添字に要素サイズを掛けて 64 ビットで足す。

**ねらい**: まとめに 1 行あると、第8章で構造体を入れるときに「要素サイズに構造体のサイズが入る」とつなげやすい。省いてもよい。

---

### 5.8 第6章の表紙、strlen の例のあと（`ch06/00_overview.md` 131 行目）

**元の文章**:

> `s[n]` は char 単位で 1 バイトずつ読み出す（ポインタ算術が型に依存することを内部でちゃんと処理している）。

**改良案**:

> `s[n]` は char 単位で 1 バイトずつ読み出す。`s` の型が `char *` なので、添字 `n` に掛ける要素サイズが 1 になる（`int *` なら 4）。`*(s + n)` と書いても同じように動く。

**ねらい**: 元の文章は、今の実装では `[]` で書いたときにしか成り立たない。直したあとは成り立つが、「内部でちゃんと処理している」では何をしているか分からないので、要素サイズの話に言い換える。

---

### 5.9 `ch06/04_build.md`

#### 6.4.4 codegen.c — 大改修（141〜145 行目、新登場の一覧）

**元の文章**:

> - **`gen_addr`**: lvalue（IDENT、`*p`、`a[i]`）のアドレスを `%rax` に。
> - **`expr_type`**: 式の型を推論（変数→シンボルテーブル、`&`→ポインタ昇格、`*`→ポインタ降格）。
> - **`emit_load(sz)` / `emit_store(sz)`**: 1/4/8 バイトの読み書きをサイズ別に。

**改良案**:

> - **`gen_addr`**: lvalue（IDENT、`*p`、`a[i]`）のアドレスを `%rax` に。
> - **`expr_type`**: 式の型を推論（変数→シンボルテーブル、`&`→ポインタ昇格、`*`→ポインタ降格、`p + i`→ポインタのまま）。
> - **`gen_ptr_arith`**: `p + i` / `p - i` を、要素サイズでスケーリングして 64 ビットで計算する。
> - **`emit_load(sz)` / `emit_store(sz)`**: 1/4/8 バイトの読み書きをサイズ別に。

#### 6.4.6 ビルドして動かす（「### 配列」のあと、任意）

**元の文章**: なし。

**改良案**（新しい小見出しを足す）:

````markdown
### ポインタ算術

```bash
$ cat ptrarith.c
int main() {
    int a[3];
    int *p = a;
    a[1] = 7;
    return *(p + 1);
}
$ ./tinyc ptrarith.c > ptrarith.s && gcc -o ptrarith ptrarith.s && ./ptrarith; echo $?
7
```
````

---

### 5.10 変えなくてよいところ

- `docs/jp/ch06/02_lvalue.md` 194 行目「lhs も **配列名でもポインタでもよい**。`Type` 構造体（`ast.h`）の `base_size` フィールドが…」── 要素サイズの出どころをすでに正しく説明している。5.3 の改良案はここを受けている。
- 2.2.9 と 5.4.6 のゼロ拡張の説明 ── 正しい。第6章からは指すだけにする。
- `addl` が出てくる第2〜5章・第7章の箇所 ── どれも `int` 同士の足し算の説明で、ポインタは関係しない。
- `docs/jp/ch07/` ── 4 節のとおり。

### 5.11 英語版

`docs/en/ch06/02_lvalue.md`、`docs/en/ch06/00_overview.md`、`docs/en/ch06/04_build.md` は、日本語版を直したあと翻訳パスで更新する。図 6-2 の英語版は、5.4 の `gen_trees.py en` でできる。

## 6. 追加するテスト

`test/cases/06_ptr_arith.c`（新規）。`p + i`、`i + p`、`p - i`、`char *` のスケーリングなし（要素サイズ 1）を 1 本で確かめる。

```c
// expect: 73
int main() {
    char *s = "abc";
    int a[3];
    int *q = a + 2;
    *(1 + a) = 5;
    *q = 30;
    return *(s + 1) + *(q - 1) - a[2];
}
```

`*(s + 1)` = `'b'` = 98、`*(q - 1)` = `a[1]` = 5、`a[2]` = 30 なので、98 + 5 − 30 = 73。修正後の ch06 / ch07 で 73 になることを Docker で確かめた（3.5 節）。

## 7. 今回の範囲外だが、同じ根を持つ問題

ポインタを 32 ビットで扱っている箇所は、`+` / `-` のほかにもある。どれも今のテストやサンプルでは表に出ないが、`PROBLEMS.md` に別項目として足しておくとよい。

- **ポインタ同士の比較** `p == q`、`p < q` ── `emit_compare` が `cmpl %ecx, %eax` で、下位 32 ビットしか比べない。
- **ポインタの真偽** `if (p)`、`while (p)`、`!p` ── `cmpl $0, %eax` なので、下位 32 ビットがたまたま 0 のアドレスを「偽」と判定する。

どちらも、オペランドがポインタなら `cmpq` を出すように直せる。
