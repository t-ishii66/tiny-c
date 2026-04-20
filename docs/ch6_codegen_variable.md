# 章6: コード生成 — 変数

## この章で学ぶこと

```c
int main() {
    int x = 5;
    int y = 3;
    return x + y;
}
```

この章では、値に名前をつける仕組み——**変数**——のコード生成を見る。

## 核心: 変数はメモリ上の場所に付けた名前

ソースコード上の変数名 `x` は、コンパイル後のアセンブリには存在しない。コンパイラは変数名を**スタック上のアドレス**に変換する。コンパイル後に残るのはオフセット（`-8(%rbp)`, `-16(%rbp)`）だけだ。

## スタック上の変数の配置

変数は `rbp`（ベースポインタ）からの相対位置で管理する:

```
rbp          ┌──────────┐
             │  古い rbp │
  -8(%rbp)   ├──────────┤
             │  x (= 5) │  ← 最初の変数: 8バイト
 -16(%rbp)   ├──────────┤
             │  y (= 3) │  ← 次の変数: 8バイト
             └──────────┘
                         ← rsp
```

変数が宣言されるたびにオフセットを 8 ずつ減らす。`x` は `-8(%rbp)`、`y` は `-16(%rbp)`。

## 生成されるアセンブリ

```asm
main:
    pushq %rbp
    movq  %rsp, %rbp
    subq  $16, %rsp           # 変数2つ分の領域を一括確保

    # int x = 5;
    movl  $5, %eax            # 初期値 5 を eax に
    movl  %eax, -8(%rbp)      # eax → x の位置に書き込み

    # int y = 3;
    movl  $3, %eax
    movl  %eax, -16(%rbp)     # eax → y の位置に書き込み

    # return x + y;
    leaq  -8(%rbp), %rax      # x のアドレスを rax に
    movl  (%rax), %eax        # そのアドレスから値を読む → eax = 5
    pushq %rax                # 退避
    leaq  -16(%rbp), %rax     # y のアドレス
    movl  (%rax), %eax        # eax = 3
    movl  %eax, %ecx          # 右 → ecx
    popq  %rax                # 左(5) → eax
    addl  %ecx, %eax          # eax = 5 + 3 = 8

    leave
    ret
```

## 変数の2つの操作

変数に対する操作は本質的に2つしかない:

| 操作 | 意味 | アセンブリ |
|------|------|-----------|
| **書き込み** | 値をメモリに格納 | `movl %eax, -8(%rbp)` |
| **読み出し** | メモリから値をロード | `leaq -8(%rbp), %rax` + `movl (%rax), %eax` |

読み出しが2命令に分かれているのは、`gen_addr`（アドレス計算）と `load`（値の読み出し）を分離するためだ。この分離が章9（ポインタ）で重要になる。

## シンボルテーブル

変数名からオフセットへの対応を管理するのが**シンボルテーブル**だ。tiny-c では単純な連結リストで実装する:

```c
typedef struct Var {
    char *name;       // 変数名
    int offset;       // rbp からのオフセット (-8, -16, ...)
    Type type;        // 型 (TY_INT, TY_CHAR, TY_PTR_INT, ...)
    int is_array;     // 配列か
    int is_global;    // グローバル変数か
    struct Var *next;
} Var;

static Var *locals;       // 現在の関数のローカル変数リスト
static int stack_offset;  // 次に使えるオフセット (0, -8, -16, ...)
```

変数の追加:

```c
static Var *add_local(const char *name, Type type, ...) {
    Var *v = calloc(1, sizeof(Var));
    v->name = strdup(name);
    v->type = type;
    stack_offset -= 8;
    v->offset = stack_offset;    // -8, -16, -24, ...
    v->next = locals;
    locals = v;
    return v;
}
```

変数の検索:

```c
static Var *find_var(const char *name) {
    for (Var *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}
```

名前で線形探索するだけだ。変数の数が少ない tiny-c ではこれで十分だ。

## 変数宣言のコード生成

AST の `ND_VAR_DECL` ノードを処理する:

```c
case ND_VAR_DECL: {
    Var *v = add_local(node->name, node->type, 0, 0);  // シンボルテーブルに登録
    if (node->lhs) {
        gen_expr(node->lhs);                            // 初期値を計算 → eax
        emit("movl %%eax, %d(%%rbp)", v->offset);      // eax → メモリ
    }
    break;
}
```

`int x = 5;` では:
1. `add_local("x", TY_INT)` で `x` をオフセット `-8` に登録
2. `gen_expr(IntLit 5)` で `eax = 5`
3. `movl %eax, -8(%rbp)` で値をメモリに書き込む

## 変数参照のコード生成

式の中で変数名 `x` が現れたとき:

```c
case ND_IDENT: {
    Var *v = find_var(node->name);      // シンボルテーブルから検索
    gen_addr(node);                     // アドレスを rax にセット
    emit("movl (%%rax), %%eax");        // そのアドレスから値を読む
    break;
}
```

`gen_addr` はアドレスを計算する関数だ:

```c
static void gen_addr(Node *node) {
    case ND_IDENT: {
        Var *v = find_var(node->name);
        emit("leaq %d(%%rbp), %%rax", v->offset);   // rax = rbp + offset
        break;
    }
}
```

`leaq -8(%rbp), %rax` は「`rbp - 8` のアドレスを `rax` にセットする」命令だ。アドレスを**計算するだけ**で、メモリの読み書きはしない。

## 代入のコード生成

`x = x + y` の AST は:

```
Assign
├─ lhs: Ident(x)      ← 書き込み先
└─ rhs: ADD            ← 書き込む値
        ├─ Ident(x)
        └─ Ident(y)
```

コード生成:

```c
case ND_ASSIGN:
    gen_expr(node->rhs);          // 1. 右辺を計算 → eax に値が入る
    emit("pushq %%rax");          //    退避
    gen_addr(node->lhs);          // 2. 左辺のアドレスを計算 → rax
    emit("movq %%rax, %%rcx");    //    アドレスを rcx に移す
    emit("popq %%rax");           //    値を eax に復元
    gen_store(type);              // 3. 値をアドレスにストア
```

ここで `gen_addr` が登場する。代入の左辺は**値ではなくアドレスが必要**だ。`x` の値 `5` ではなく、`x` が格納されている場所 `-8(%rbp)` が必要だ。

生成されるアセンブリ（`x = x + y` の部分）:

```asm
    # x + y を計算（結果は eax に）
    leaq  -8(%rbp), %rax        # x のアドレス
    movl  (%rax), %eax          # eax = x(5)
    pushq %rax
    leaq  -16(%rbp), %rax       # y のアドレス
    movl  (%rax), %eax          # eax = y(3)
    movl  %eax, %ecx
    popq  %rax
    addl  %ecx, %eax            # eax = 8

    # x に代入
    pushq %rax                  # 値(8)を退避
    leaq  -8(%rbp), %rax        # x のアドレスを計算
    movq  %rax, %rcx            # アドレス → rcx
    popq  %rax                  # 値(8) → eax
    movl  %eax, (%rcx)          # メモリに書き込み
```

## スタック領域の一括確保

関数の先頭で `subq $16, %rsp` を実行し、ローカル変数の領域を一括で確保する。コンパイラは関数本体を先にスキャンして必要なバイト数を数える:

```c
int space = nparams * 8 + count_locals(node->body);
space = (space + 15) & ~15;   // 16バイト境界にアライン
if (space > 0)
    emit("subq $%d, %%rsp", space);
```

16バイトアラインは x86-64 ABI の要件だ。関数呼び出し時にスタックポインタが16の倍数でなければならない。

## グローバル変数

ローカル変数はスタック上にあるが、グローバル変数は**別のメモリ領域**（`.bss` セクション）に配置される:

```c
int g;       // グローバル変数

int main() {
    g = 77;
    return g;
}
```

生成されるアセンブリ:

```asm
    .text
    .globl main
main:
    ...
    leaq  g(%rip), %rax       # g のアドレス（RIP相対）
    ...

    .bss
    .globl g
    .align 4
g:
    .zero 4                   # 4バイト、ゼロ初期化
```

ローカル変数は `rbp` 相対、グローバル変数は `rip` 相対。`gen_addr` が両方を透過的に扱う:

```c
static void gen_addr(Node *node) {
    Var *v = find_var(node->name);
    if (v->is_global)
        emit("leaq %s(%%rip), %%rax", v->name);    // グローバル
    else
        emit("leaq %d(%%rbp), %%rax", v->offset);  // ローカル
}
```

## この章の要点

1. 変数は**スタック上のオフセット**に変換される。コンパイル後、変数名は消える
2. **シンボルテーブル**が「名前 → オフセット」の対応を管理する
3. 変数への操作は**書き込み**（レジスタ → メモリ）と**読み出し**（メモリ → レジスタ）の2つだけ
4. `gen_addr` がアドレスを計算し、`gen_expr` が値を計算する。この分離が代入やポインタの基盤になる

## 次の章へ

変数を使えるようになったが、まだ一本道のコードしか書けない。次の章で `if` と `while` を導入し、実行の流れを分岐させる。
