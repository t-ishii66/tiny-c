# 03 — グローバル変数と文字列リテラル

ch05 までのデータはすべて **スタック上** にあった: 関数のフレーム内のローカル変数、push/pop の一時値。ch06 で初めて **スタックの外** にデータを置くことになる ── グローバル変数は `.bss`、文字列リテラルは `.rodata`。

## 1. ELF のセクションいくつか

x86-64 Linux の実行ファイル（ELF 形式）は、いくつかの **セクション** に分かれている。tiny-c が出力するアセンブリで使うのは以下:

| セクション | 用途 | 特性 |
|----------|------|------|
| `.text`     | 関数本体（実行コード） | 読み取り + 実行可能 |
| `.bss`      | ゼロ初期化データ | 読み書き可能、ファイルにバイト列を書き込まない（ロード時にゼロで埋める）|
| `.rodata`   | 読み取り専用データ（定数） | 読み取りのみ、実行不可 |
| `.data`     | 初期化済みデータ | 読み書き可能（tiny-c では使わない）|

これまでは `.text` だけだった。ch06 で残り2つも登場する。

## 2. グローバル変数: `.bss`

`int g;` のようなグローバル変数は、tiny-c では **初期化子なし** ── ゼロ初期化される。これは `.bss` の用途そのもの。

アセンブリ出力例:

```asm
  .bss
  .globl g
g:
  .zero 4

  .globl arr
arr:
  .zero 40
```

- `.bss` がセクション切り替え。
- `.globl g` で外部公開（リンカから見える）。
- `g:` で **ラベル**（その位置に名前を付ける）。
- `.zero 4` で 4 バイトのゼロ領域を確保（ELF 上は実際のバイト列ではなく「サイズだけ」が記録される）。

`int g;` は 4 バイト、`int arr[10];` は 40 バイト、`char *gp;` は 8 バイト ── 型のサイズに応じて `.zero` 引数を変える。

## 3. グローバルへのアクセス: `name(%rip)`

ローカル変数は `-off(%rbp)` で参照していた。グローバルは **PC 相対アドレッシング** で参照する:

```asm
  leaq g(%rip), %rax       # %rax = g のアドレス
  movl (%rax), %eax        # g の値を読む

  movl %eax, g(%rip)       # g に書く（直接形式も可）
```

`g(%rip)` は「`%rip`（命令ポインタ）から `g` までの相対オフセット」をリンカが解決する仕組み。実行ファイルが任意のアドレスにロードされても正しく動く（**位置独立コード**, PIC）。

tiny-c の `gen_addr(IDENT g)` は変数のグローバル / ローカルを判定して、適切な命令を出す:

```c
if (v->is_global)
    fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);
else
    fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
```

`gen_addr` がグローバルとローカルの違いを吸収するので、`gen_expr` や `NODE_ASSIGN` は何も変えなくていい。これも lvalue の抽象化の効果。

## 4. シンボルテーブルの拡張

ローカル用とグローバル用、2つのシンボルテーブルを持つ:

```c
static LVar *locals;     /* per-function */
static LVar *globals;    /* program-wide */
```

`LVar` 構造体に `is_global` フラグと `Type *type` を追加:

```c
struct LVar {
    char *name;
    int offset;        /* ローカル時: -offset(%rbp) */
    Type *type;
    int is_global;     /* 1 なら %rip 相対、offset は使わない */
    LVar *next;
};
```

`find_var(name)` はローカル → グローバル の順で検索する。同名のローカルがあればそちらが優先 ── **ローカル/グローバル間のシャドーイング** は C と一致する。ローカル変数のブロックスコープ管理は次節 04 で扱う。

codegen の入口で:

1. プログラム全体を 1 周し、`NODE_GLOBAL_VAR_DECL` をすべて `globals` テーブルに登録。
2. その後、関数群を順に `gen_func`。各関数は `locals` をリセットしつつグローバルは参照可能。

これで関数本体内から `g = 42` のようにグローバルに書き込めるようになる。

## 5. 文字列リテラル: `.rodata`

文字列リテラル `"hello"` は **読み取り専用データ**。`.rodata` に置く。各リテラルにユニークなラベル `.LSn` を付け、コード中ではそのアドレスを参照する。

```asm
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 0
```

`.byte` ディレクティブは指定した値のバイト列をその位置に置く。`104, 101, ...` は `'h'`, `'e'`, ... の ASCII。最後の `0` は C 文字列の終端 `'\0'`。

実は `.string "hello"` という便利なディレクティブもあるが、エスケープシーケンスの解釈など細かい違いがあるので、tiny-c ではバイト直書きのほうが理解しやすい。

文字列の **値** は「そのデータの先頭アドレス」だ:

```asm
  leaq .LS0(%rip), %rax      # %rax = "hello" のアドレス
```

これを `char *s` に代入したり、`printf` の引数として渡したりする。

## 6. 文字列リテラルの管理

複数の文字列リテラルを区別するため、コード生成中は **string literal table** に追加していき、最後にまとめて `.rodata` に出す:

```c
typedef struct StrLit {
    char *str;
    int len;
    int label;        /* .LS0, .LS1, ... の番号 */
    StrLit *next;
} StrLit;

static StrLit *strs;
static int str_count;

static int add_string(char *str, int len) {
    StrLit *s = calloc(1, sizeof(StrLit));
    s->str = str;
    s->len = len;
    s->label = str_count++;
    s->next = strs;
    strs = s;
    return s->label;
}
```

`gen_expr(NODE_STRING_LIT)` で `add_string` を呼んでラベル番号を取り、`leaq .LSn(%rip), %rax` を出す。

```c
case NODE_STRING_LIT: {
    int n = add_string(node->str_val, node->str_len);
    fprintf(out, "  leaq .LS%d(%%rip), %%rax\n", n);
    return;
}
```

最後に `codegen()` の終わりで `.rodata` を切って全リテラルを `.LS0`, `.LS1`, ... の形で並べる。

## 7. char 値の扱い

文字リテラル `'a'` は `int` 値（97）として扱う。AST 上は `NODE_CHAR_LIT(int_val=97)`。codegen は `movl $97, %eax` を出すだけ ── `INT_LIT` と同じ扱いでよい。

```c
case NODE_CHAR_LIT:
    fprintf(out, "  movl $%d, %%eax\n", node->int_val);
    return;
```

`char c = 'A';` の場合:
- 右辺 `'A'` は `movl $65, %eax`（4 バイト分の値）
- 左辺 `c` は `char` 型 → `movb %al, (%rcx)` で 1 バイト store
- `%al` は `%eax` の最下位 1 バイトなので、`'A'` の値 65 がそのまま書き込まれる

`return c;` のとき:
- `c` を読む: `movsbl -off(%rbp), %eax`（1 バイトを符号拡張して 4 バイトに）
- 戻り値として `%eax` の 65 を返す

## 8. 関数引数のサイズ別 spill

ch05 では引数 spill は `movl %edi, -off(%rbp)`（4 バイト）一択だった。ch06 では `char *s` のような pointer 引数は 8 バイト渡しなので分岐が要る:

```c
int sz = t->is_pointer ? 8 : 4;
if (sz == 8)
    fprintf(out, "  movq %s, -%d(%%rbp)\n", arg_regs64[i], v->offset);
else
    fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], v->offset);
```

`int strlen(char *s)` の `s` は `char *`（pointer）なので `movq %rdi, -8(%rbp)`。`int set(int v)` の `v` は `int` なので `movl %edi, -8(%rbp)`。

## 9. 全部つながる: Hello, world

```c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}
```

1. パーサ: `STRING_LIT "hello"` を受け取り、ノードに保存。
2. codegen 始まり: `.text` セクション、`main:` ラベル、プロローグ。
3. `char *s = "hello";`（代入の順序は ① lhs のアドレス → push、② rhs を計算（%rax）、③ pop %rcx で lhs アドレスを取り出し、④ store）:
   - ① `gen_addr(s)` → `leaq -8(%rbp), %rax`、`pushq %rax` で退避。
   - ② `gen_expr(STRING_LIT "hello")` → `add_string` で label 0 を取り、`leaq .LS0(%rip), %rax`（`%rax` = `"hello"` のアドレス）。
   - ③ `popq %rcx`（`%rcx` = `s` のスロットのアドレス）。
   - ④ `movq %rax, (%rcx)` で store ── `s` のスロットに `"hello"` のアドレスが書き込まれる。
4. `printf("%s\n", s);`:
   - push_args は **引数を逆順に push**（最初の引数が最後に push されてスタックの一番上に来るように）。
   - まず arg 2 = `s` を push: `gen_expr(s)` → `leaq -8(%rbp), %rax; movq (%rax), %rax`、push。
   - 次に arg 1 = `"%s\n"` を push: `gen_expr(STRING_LIT "%s\n")` → `add_string` で label 1、`leaq .LS1(%rip), %rax`、push。
   - pop `%rdi` ← スタック先頭 = `"%s\n"` のアドレス（= arg 1）、pop `%rsi` ← その下 = s の値（= `"hello"` のアドレス、arg 2）。
   - `movl $0, %eax; call printf`。
5. `return 0;` → `movl $0, %eax; leave; ret`。
6. codegen 終わり:
   - `.bss` セクション（このプログラムでは空）。
   - `.section .rodata` を切り、`.LS0: .byte 104, 101, 108, 108, 111, 0` と `.LS1: .byte 37, 115, 10, 0` を出す。

リンク後実行すると `hello` と表示される。tiny-c が「**printf を呼べる**」言語になった瞬間。

## 10. まとめ

- グローバル変数は `.bss` セクションに `name: .zero N` で確保、`leaq name(%rip), %rax` で参照。
- グローバル / ローカルの違いは `gen_addr` が吸収。
- 文字列リテラルは `.rodata` に `.LSn:` でユニークラベル付き、`leaq .LSn(%rip), %rax` で値（アドレス）を取る。
- 文字リテラル `'a'` は単なる整数値。`char` 型変数への store は `movb`、load は `movsbl`。
- pointer 引数の spill は `movq`（8 バイト）。

## 次へ

次の節（`04_scope.md`）で **ブロックスコープ** を導入する ── append-only のシンボルテーブルに active フラグと scope_stack を足し、`{ int x=1; }{ int x=2; }` のような同名変数の別宣言が通るようにする。
