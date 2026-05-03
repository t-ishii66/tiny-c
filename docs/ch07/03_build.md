# 03 — 完全形とビルド

ch07 の差分を ch06 と並べる。`lexer.l` `parser.y` `ast.h` `ast.c` `codegen.c` は **無変更**。新規 `optimize.c` と main.c の変更が中身のすべて。

## 1. optimize.c の骨格

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
    /* ... 他のノード型は子を再帰最適化するだけ ... */
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

static int is_instr_line(const char *line) { /* ... */ }

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
    /* ライン解放 */
    for (int i = 0; i < n_lines; i++)
        if (lines[i]) free(lines[i]);
    n_lines = 0;
}
```

完全版は `steps/ch07/src/optimize.c`。

## 2. main.c の変更

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

    if (no_opt) {
        codegen(program, stdout);
    } else {
        /* codegen をメモリへ → peephole で stdout へ */
        char *buf = NULL;
        size_t len = 0;
        FILE *mem = open_memstream(&buf, &len);
        codegen(program, mem);
        fclose(mem);
        peephole(buf, (int)len, stdout);
        free(buf);
    }
}
```

## 3. Makefile

`optimize.c` を追加するだけ:

```makefile
SRCS    = src/ast.c src/codegen.c src/optimize.c src/main.c           # optimize.c 追加
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/optimize.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o
```

## 4. ビルド

```bash
$ cd steps/ch07
$ make
```

## 5. デモ 1: 定数畳み込み

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
  movl $0, %eax
  leave
  ret

$ ./tinyc const.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $14, %eax
  leave
  ret
```

**18 行 → 7 行**。AST レベルで `2 + 3 * 4 = 14` が畳み込まれ、ピープホールで末尾 dead code が削られた。

## 6. デモ 2: 代数的単純化

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

## 7. デモ 3: ピープホール（pushq/popq → movq）

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
  leaq .LS0(%rip), %rax
  movq %rax, %rdi          ← 1命令に
  movl $0, %eax
  call printf
  leave
  ret                      ← ここまで（dead code は消えた）
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 10, 0
```

`.section .rodata` は dead code 除去で誤って消されない（`.` で始まるディレクティブ行として認識される）。

## 8. デモ 4: 再帰 fib の行数削減

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
66
$ ./tinyc fib.c | wc -l
57
```

`fib` 内部で関数呼び出しが2回あり、それぞれの引数渡しで `pushq %rax; popq %rdi` が出る。さらに `if` 分岐の `then` 部の `return n;` 末尾の dead code（次の `.Lendif_0:` までの数行）が削られる。**全体で約14%の削減**。

## 9. ここまでで作ったもの

第7章で:

- AST 最適化パスの導入（**lexer/parser/codegen に手を入れずに** 機能追加できる構造の実例）。
- ピープホール最適化の導入（**生成 asm 上での後処理** という別レイヤーの最適化）。
- `--no-opt` でビフォー・アフターを直接比較できる。

tiny-c の世界では「最適化」は最小限だが、**コンパイラに最適化を後から付け足せる構造の重要性** を体感できた。実プロダクションのコンパイラ（GCC/LLVM/clang など）はこの考え方を発展させ、**何十、何百もの最適化パス** を順に通している ── でも各パスは tiny-c の `optimize_ast` や `peephole` と同じ「**入力を受け取って改善版を返す**」という形をしている。

## まとめ

- 最適化は 2 レベル: **AST 上**（定数畳み込み + 代数的単純化）と **生成 asm 上**（ピープホール）。
- AST 最適化は `Node *` を再帰的に書き換えるだけ。`optimize_ast(node)` は新しい（または同じ）ノードを返す。
- ピープホール最適化は asm を行に分け、隣接 2 行のパターンを書き換える。`pushq %rax; popq REG → movq %rax, REG`、`pushq %rax; popq %rax → 消去`、`ret` 後のデッドコード除去。
- codegen は変更しない。最適化は **codegen の前後** に追加されるパスとして外側から噛ませる ── これがコンパイラの拡張性の本質。
- `lexer.l / parser.y / ast.* / codegen.c` は ch06 と完全に同じ。**新規ファイル `optimize.c` と main.c の差分だけで完結**。
