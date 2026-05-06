# 04 — 完全形とビルド

ch07 の差分を ch06 と並べる。

| ファイル | ch06 → ch07 |
|---------|------------|
| `lexer.l` / `parser.y` / `ast.c` | **無変更** |
| `ast.h` | `Node->lvar` フィールドと `LVar` 前方宣言を **削除**（バックパッチで Phase 1 が不要に）|
| `codegen.c` | `collect_locals` を **削除**、単一パス化。プロローグの `subq $N, %rsp` を `ftell`/`fseek` で **バックパッチ** |
| `main.c` | 常に `open_memstream` を使う。`--no-opt` でもバッファ経由で stdout に流す |
| `optimize.h` / `optimize.c` | **新規** ── `optimize_ast` と `peephole` の 2 関数 |
| `Makefile` | `optimize.c` をビルド対象に追加 |

「最適化」（節 01・03）と「バックパッチ」（節 02）は別物だが、両者とも **メモリバッファ** という同じインフラの上に乗る。`--no-opt` で外せるのは最適化だけで、バックパッチは常に有効。

## 1. codegen.c — バックパッチ部分の差分

ch06 の `gen_func` は **二相**（Phase 1 = `collect_locals` で `max_frame_size` 確定 → Phase 2 = プロローグ + 本体生成）だった。ch07 は **単一パス + バックパッチ** に置き換わる:

```c
#define SUBQ_FRAME_WIDTH 10

static void gen_func(Node *fn) {
    locals = NULL; frame_size = 0; max_frame_size = 0;
    stack_offset = 0; scope_depth = 0;

    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");

    /* ★ プロローグの subq をプレースホルダで書き、位置を覚えておく */
    long subq_pos = ftell(out);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, 0);

    enter_scope();   /* 関数スコープ */

    /* params: add_local と spill を 1 ループで。Phase 1 はもうない */
    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        LVar *v = add_local(l->node->name, l->node->type);
        Type *t = v->type;
        int sz = t->is_pointer ? 8 : 4;
        if (sz == 8)
            fprintf(out, "  movq %s, -%d(%%rbp)\n", arg_regs64[i], v->offset);
        else
            fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], v->offset);
        i++;
    }

    gen_stmt(fn->body);   /* 本体: VAR_DECL で frame_size と max_frame_size が育つ */

    exit_scope();
    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");

    /* ★ ここで max_frame_size が確定。プロローグに戻って subq を書き直す */
    int aligned_frame = (max_frame_size + 15) & ~15;
    long here = ftell(out);
    if (fseek(out, subq_pos, SEEK_SET) != 0) {
        fprintf(stderr, "backpatch: output stream is not seekable\n");
        exit(1);
    }
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, aligned_frame);
    fseek(out, here, SEEK_SET);
}
```

`gen_stmt` の `NODE_VAR_DECL` も単一パス向けにシンプル化:

```c
case NODE_VAR_DECL: {
    LVar *v = add_local(node->name, node->type);   /* その場で確保 + 可視化 */
    if (!node->expr) return;
    fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
    emit_push();
    gen_expr(node->expr);
    emit_pop("%rcx");
    Type *t = v->type;
    int sz = t->is_pointer ? 8 : t->base_size;
    emit_store(sz);
    return;
}
```

ch06 の「Phase 2 で `node->lvar` から LVar を取り出して再リンク」する手順が **完全に消える** ── AST に back-pointer を残す必要もなくなった（だから `ast.h` から `Node->lvar` を削除）。

完全版は `steps/ch07/src/codegen.c` を参照。

## 2. optimize.c の骨格

```c
#include "ast.h"
#include "optimize.h"

/* AST 最適化 */
Node *optimize_ast(Node *node) {
    if (!node) return NULL;
    switch (node->kind) {
    case NODE_BINARY: {
        node->lhs = optimize_ast(node->lhs);
        node->rhs = optimize_ast(node->rhs);
        /* 代数的単純化 */
        switch (node->op) {
        case '+':
            if (is_int_lit_val(node->lhs, 0)) return node->rhs;
            if (is_int_lit_val(node->rhs, 0)) return node->lhs;
            break;
        case '-':
            if (is_int_lit_val(node->rhs, 0)) return node->lhs;
            break;
        case '*':
            if (is_int_lit_val(node->lhs, 1)) return node->rhs;
            if (is_int_lit_val(node->rhs, 1)) return node->lhs;
            if (is_int_lit_val(node->lhs, 0) || is_int_lit_val(node->rhs, 0))
                return new_int_lit(0);
            break;
        case '/':
            if (is_int_lit_val(node->rhs, 1)) return node->lhs;
            break;
        }
        /* 定数畳み込み */
        if (is_int_lit(node->lhs) && is_int_lit(node->rhs)) {
            int a = node->lhs->int_val, b = node->rhs->int_val, r = 0;
            switch (node->op) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            /* ... 同様に他の演算子 ... */
            default: return node;
            }
            return new_int_lit(r);
        }
        return node;
    }
    case NODE_UNARY: {
        node->operand = optimize_ast(node->operand);
        if (is_int_lit(node->operand)) {
            switch (node->op) {
            case '-': return new_int_lit(-node->operand->int_val);
            case '!': return new_int_lit(!node->operand->int_val);
            }
        }
        return node;
    }
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    /* ... PROGRAM, FUNC_DEF, IF, WHILE, ASSIGN, INDEX, RETURN, EXPR_STMT, VAR_DECL, CALL ... */
    default:
        return node;
    }
}

/* ピープホール */
static char *lines[MAX_LINES];
static int n_lines;

static void peephole_pushpop(void) {
    for (int i = 0; i + 1 < n_lines; i++) {
        if (!lines[i] || !lines[i+1]) continue;
        if (strcmp(lines[i], "  pushq %rax\n") != 0) continue;
        if (strcmp(lines[i+1], "  popq %rax\n") == 0) {
            free(lines[i]); free(lines[i+1]);
            lines[i] = lines[i+1] = NULL;
            i++;
        } else if (strncmp(lines[i+1], "  popq ", 7) == 0) {
            char reg[16];
            sscanf(lines[i+1] + 7, "%15s", reg);
            free(lines[i]); free(lines[i+1]);
            char *combined = malloc(64);
            snprintf(combined, 64, "  movq %%rax, %s\n", reg);
            lines[i] = combined;
            lines[i+1] = NULL;
            i++;
        }
    }
}

static void peephole_dead_after_ret(void) {
    int dead = 0;
    for (int i = 0; i < n_lines; i++) {
        if (!lines[i]) continue;
        if (!dead) {
            if (strcmp(lines[i], "  ret\n") == 0) dead = 1;
            continue;
        }
        if (!is_instr_line(lines[i])) { dead = 0; continue; }
        free(lines[i]); lines[i] = NULL;
    }
}

void peephole(const char *in_buf, int in_len, FILE *out) {
    split_lines(in_buf, in_len);
    peephole_pushpop();
    peephole_dead_after_ret();
    for (int i = 0; i < n_lines; i++)
        if (lines[i]) fputs(lines[i], out);
    for (int i = 0; i < n_lines; i++)
        if (lines[i]) free(lines[i]);
    n_lines = 0;
}
```

完全版は `steps/ch07/src/optimize.c`。

## 3. main.c の変更

ch06 では `codegen(program, stdout)` で直接出力していたが、ch07 では **常に memstream** を経由する（バックパッチで `ftell`/`fseek` が必要なため）。`--no-opt` のときはそのまま出し、最適化ありなら peephole を通す:

```c
#include "optimize.h"

int main(int argc, char **argv) {
    int dump_ast = 0;
    int no_opt = 0;
    /* ... 引数パース ... */

    yyin = fopen(filename, "r");
    yyparse();
    fclose(yyin);

    if (!no_opt) program = optimize_ast(program);   /* AST 最適化 */

    if (dump_ast) { print_ast(program, 0); return 0; }

    /* codegen は常にメモリバッファに書く（バックパッチに ftell/fseek が必要）。
       --no-opt のときは ピープホール段階だけスキップ。 */
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    codegen(program, mem);
    fclose(mem);

    if (no_opt) {
        fputs(buf, stdout);
    } else {
        peephole(buf, (int)len, stdout);
    }
    free(buf);
}
```

## 4. Makefile

`optimize.c` を追加するだけ:

```makefile
SRCS    = src/ast.c src/codegen.c src/optimize.c src/main.c
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/optimize.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o
```

## 5. ビルド

```bash
$ cd steps/ch07
$ make
```

## 6. デモ 1: 定数畳み込み + プロローグ subq の見え方

```bash
$ cat const.c
int main() {
    return 2 + 3 * 4;
}

$ ./tinyc --no-opt const.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $0         , %rsp     ← バックパッチで埋まった subq（ローカル無しなので 0）
  movl $4, %eax
  pushq %rax
  movl $3, %eax
  popq %rcx
  imull %ecx, %eax
  pushq %rax
  movl $2, %eax
  popq %rcx
  addl %ecx, %eax
  leave
  ret
  movl $0, %eax           ← 暗黙の return 0（dead code）
  leave
  ret

$ ./tinyc const.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $0         , %rsp
  movl $14, %eax
  leave
  ret
```

注目点:

- `subq $0         , %rsp` の **数字の右に並ぶスペース** はバックパッチの placeholder の幅（10 桁）が固定なため。アセンブラは余分な空白を許容するので問題なく動く。
- `--no-opt` 版は **19 行**、最適化版は **7 行** に縮む。AST 最適化で `2 + 3 * 4 = 14` が畳み込まれ、ピープホールで末尾の dead code（`movl $0, %eax; leave; ret`）が削れた。

## 7. デモ 2: 代数的単純化

```bash
$ cat alg.c
int main() {
    int x = 7;
    return x + 0 + x * 1;
}

$ ./tinyc --no-opt --dump-ast alg.c
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          BINARY +
            IDENT x
            INT_LIT 0
          BINARY *
            IDENT x
            INT_LIT 1

$ ./tinyc --dump-ast alg.c
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          IDENT x
          IDENT x
```

`x + 0 + x * 1` が `x + x` に縮んだ。

## 8. デモ 3: ピープホール（pushq/popq → movq）

```bash
$ cat hello.c
int main() {
    return printf("hello\n");
}

$ ./tinyc --no-opt hello.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $0         , %rsp
  leaq .LS0(%rip), %rax
  pushq %rax              ← 隣接ペア
  popq %rdi               ← 隣接ペア
  movl $0, %eax
  call printf
  leave
  ret
  movl $0, %eax           ← dead code
  leave
  ret
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 10, 0

$ ./tinyc hello.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $0         , %rsp
  leaq .LS0(%rip), %rax
  movq %rax, %rdi          ← 1 命令に
  movl $0, %eax
  call printf
  leave
  ret                      ← ここまで（dead code は消えた）
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 10, 0
```

`.section .rodata` は dead code 除去で誤って消されない（`.` で始まるディレクティブ行として認識される）。

## 9. デモ 4: 再帰 fib の行数削減

```bash
$ cat fib.c
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    return fib(10);
}

$ ./tinyc --no-opt fib.c | wc -l
67
$ ./tinyc fib.c | wc -l
58
```

`fib` 内部で関数呼び出しが 2 回あり、それぞれの引数渡しで `pushq %rax; popq %rdi` が出る。さらに `if` 分岐の `then` 部の `return n;` 末尾の dead code（次の `.Lendif_0:` までの数行）が削られる。**全体で約 13% の削減**。

## 10. デモ 5: バックパッチによるフレームサイズ確定

ローカル変数を持つ関数で `subq $N` が後埋めされる様子:

```bash
$ cat frame.c
int main() {
    { int a = 1; int b = 2; }
    { int c = 3; int d = 4; }
    return 0;
}

$ ./tinyc --no-opt frame.c | head -7
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16        , %rsp    ← バックパッチで 16 が埋まった
  leaq -8(%rbp), %rax
```

**フレームは 16 バイト**（兄弟ブロックでスロット再利用：`a, b` と `c, d` が同じ `-8`/`-16` を共有）。ch06 だと Phase 1 で `max_frame_size` を先に確定してから書いていたが、ch07 では本体を生成しながら N が育ち、最後にプロローグの placeholder を埋めるだけ。

## 11. ここまでで作ったもの

第 7 章で:

- **AST 最適化** パスの導入。lexer/parser/codegen に手を入れずに「最適化を後付けできる構造」を実例化。
- **バックパッチ** で codegen を **二相 → 単一パス** に簡素化。`Node->lvar` などの中間状態が不要になり、AST と codegen の関心が分離した。
- **ピープホール最適化** で生成 asm 上の後処理を実装。
- `--no-opt` で最適化のビフォー・アフターを直接比較できる。バックパッチは常に有効。

tiny-c の世界では「最適化」は最小限だが、**コンパイラに後から機能を付け足せる構造の重要性** を体感できた。実プロダクションのコンパイラ（GCC/LLVM/clang など）はこの考え方を発展させ、何十・何百もの最適化パスを順に通している ── でも各パスは tiny-c の `optimize_ast` や `peephole` と同じ「**入力を受け取って改善版を返す**」という形をしている。バックパッチも本物のコンパイラで広く使われる定番テクニック（特に前方ジャンプの相対オフセットを後で埋める用途）。

## まとめ

- 最適化は 2 レベル: **AST 上**（定数畳み込み + 代数的単純化）と **生成 asm 上**（ピープホール）。
- AST 最適化は `Node *` を再帰的に書き換えるだけ。`optimize_ast(node)` は新しい（または同じ）ノードを返す。
- ピープホール最適化は asm を行に分け、隣接 2 行のパターンを書き換える。
- **バックパッチ**: 出力をメモリバッファに溜めることで `ftell`/`fseek` が使え、プロローグの `subq $N` を後から埋められる ── これで Phase 1 が消え codegen が単一パスに。
- AST 最適化とピープホールは `--no-opt` で外せるが、バックパッチは codegen の構造そのものなので常に有効。
- `lexer.l / parser.y / ast.c` は ch06 と完全に同じ。差分は `ast.h`（lvar 削除）、`codegen.c`（バックパッチ）、`main.c`（常に memstream）、`optimize.c` 新規 の 4 箇所。
