# 05 — Complete files and build

The ch07 diff against ch06:

| File | ch06 → ch07 |
|---------|------------|
| `lexer.l` / `parser.y` / `ast.h` / `ast.c` | **No change** |
| `codegen.c` | **Remove** `collect_locals`, go single-pass. **Backpatch** the prologue's `subq $N, %rsp` with `ftell`/`fseek`. Introduce **block scope** (`enter_scope` / `exit_scope`, `scope_top[]`, `max_frame_size`) |
| `main.c` | Always use `open_memstream`. Even with `--no-opt`, the asm flows through the buffer before going to stdout |
| `optimize.h` / `optimize.c` | **New** — two functions, `optimize_ast` and `peephole` |
| `Makefile` | Add `optimize.c` to the build |

"Optimization" (sections 01 and 04), "backpatching" (section 02), and "block scope" (section 03) are different things, but they all sit on the same shared infrastructure: **memory buffer + single-pass codegen**. `--no-opt` only disables optimization; backpatching and scope are always on.

## 1. codegen.c — the backpatching + scope diff

ch06's `gen_func` was **two-phase** (Phase 1 = `collect_locals` to finalize `frame_size` → Phase 2 = prologue + body emission). ch07 replaces it with **single-pass + backpatching + scope**:

```c
#define SUBQ_FRAME_WIDTH 10

static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    max_frame_size = 0;
    stack_offset = 0;
    scope_depth = 0;

    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");

    /* ★ Write the prologue's subq as a placeholder; remember the position */
    long subq_pos = ftell(out);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, 0);

    enter_scope();   /* function scope: parameters live here */

    /* params: add_local and spill in a single loop. Phase 1 is gone */
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

    gen_stmt(fn->body);   /* body: VAR_DECL grows frame_size, tracking max_frame_size */

    exit_scope();

    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");

    /* ★ max_frame_size is now final. Seek back to the prologue and rewrite subq */
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

`gen_stmt`'s `NODE_VAR_DECL` also simplifies for the single-pass world:

```c
case NODE_VAR_DECL: {
    LVar *v = add_local(node->name, node->type);   /* allocate + make visible on the spot */
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

case NODE_BLOCK:
    enter_scope();
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    exit_scope();
    return;
```

ch06's "call `add_local` in Phase 1 to pre-build locals, then emit the body in Phase 2" two-phase shape is **completely gone** — during body emission, when we hit a VAR_DECL we just call `add_local` on the spot. `enter_scope` / `exit_scope` at `NODE_BLOCK` handle the per-scope rewind of `locals`, and `max_frame_size` holds the value for the `subq` backpatch.

Full version in `steps/ch07/src/codegen.c`.

## 2. optimize.c skeleton

```c
#include "ast.h"
#include "optimize.h"

/* AST optimization */
Node *optimize_ast(Node *node) {
    if (!node) return NULL;
    switch (node->kind) {
    case NODE_BINARY: {
        node->lhs = optimize_ast(node->lhs);
        node->rhs = optimize_ast(node->rhs);
        /* algebraic simplification */
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
        /* constant folding */
        if (is_int_lit(node->lhs) && is_int_lit(node->rhs)) {
            int a = node->lhs->int_val, b = node->rhs->int_val, r = 0;
            switch (node->op) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            /* ... the other operators similarly ... */
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

/* peephole */
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

Full version in `steps/ch07/src/optimize.c`.

## 3. main.c changes

ch06 wrote directly via `codegen(program, stdout)`, but ch07 **always** routes through a memstream (`ftell`/`fseek` for backpatching requires it). For `--no-opt`, just dump the buffer; otherwise pipe it through peephole:

```c
#include "optimize.h"

int main(int argc, char **argv) {
    int dump_ast = 0;
    int no_opt = 0;
    /* ... parse args ... */

    yyin = fopen(filename, "r");
    yyparse();
    fclose(yyin);

    if (!no_opt) program = optimize_ast(program);   /* AST optimization */

    if (dump_ast) { print_ast(program, 0); return 0; }

    /* codegen always writes to a memory buffer (backpatching needs ftell/fseek).
       With --no-opt, only the peephole step is skipped. */
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

Just add `optimize.c`:

```makefile
SRCS    = src/ast.c src/codegen.c src/optimize.c src/main.c
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/optimize.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o
```

## 5. Build

```bash
$ cd steps/ch07
$ make
```

## 6. Demo 1: constant folding + how the prologue subq looks

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
  subq $0         , %rsp     ← subq filled in by backpatching (0 — no locals)
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
  movl $0, %eax           ← implicit return 0 (dead code)
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

Notes:

- The **spaces after the number** in `subq $0         , %rsp` come from the placeholder's fixed width (10 digits). Assemblers accept the extra whitespace, so it works fine.
- The `--no-opt` version is **19 lines**, the optimized version shrinks to **7 lines**. AST optimization folded `2 + 3 * 4 = 14`, and peephole stripped the trailing dead code (`movl $0, %eax; leave; ret`).

## 7. Demo 2: algebraic simplification

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

`x + 0 + x * 1` collapses to `x + x`.

## 8. Demo 3: peephole (pushq/popq → movq)

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
  pushq %rax              ← adjacent pair
  popq %rdi               ← adjacent pair
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
  movq %rax, %rdi          ← collapsed to one instruction
  movl $0, %eax
  call printf
  leave
  ret                      ← ends here (dead code is gone)
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 10, 0
```

`.section .rodata` is correctly preserved by dead-code removal (recognized as a directive line starting with `.`).

## 9. Demo 4: line-count reduction for recursive fib

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

`fib` has two function calls internally, each of which emits `pushq %rax; popq %rdi` for argument passing. On top of that, the dead code at the tail of `if`'s `then` branch (`return n;` is followed by several lines up to the next `.Lendif_0:`) gets stripped. **About 13% reduction overall.**

## 10. Demo 5: frame size finalized by backpatching

A demo combining **slot reuse** from block scope with backpatching:

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
  subq $16        , %rsp    ← max_frame_size = 16, filled in by backpatching
  leaq -8(%rbp), %rax
```

Four variables, yet **the frame is 16 bytes** (sibling blocks reuse slots: `a, b` and `c, d` share `-8`/`-16`). In ch06, Phase 1 finalized `frame_size` first and then wrote `subq $16, %rsp`. In ch07, we first emit the placeholder `subq $0         , %rsp`, generate the body while tracking `max_frame_size` through scope traversal, and fill it in at the end with `ftell`/`fseek`. The placeholder's leftover spaces stay in the output, so a careful read shows "this is where the backpatch happened."

## 11. What we built

In Chapter 7 we:

- Added an **AST optimization** pass. Without touching lexer/parser/codegen, we demonstrated a structure where "optimization can be bolted on after the fact."
- Used **backpatching** to simplify codegen from **two-phase to single-pass**. We can call `add_local` directly during body emission; the `collect_locals` pre-pass is gone.
- Layered **block scope** on top of the single-pass codegen. `{ int x=1; }{ int x=2; }` works, and stack slots are reused across sibling blocks.
- Implemented **peephole optimization** as a post-processing pass on the emitted asm.
- `--no-opt` lets you directly compare before/after for optimization. Backpatching and scope are always on.

In tiny-c's world the "optimizations" are minimal, but **we now have a structure where optimization can be inserted as a separate pass**. Real production compilers (GCC/LLVM/clang etc.) extend this idea, running dozens or hundreds of optimization passes in sequence — but each pass has the same shape as tiny-c's `optimize_ast` or `peephole`: "**take an input and return an improved version.**" Backpatching has its analogues too (filling in relative offsets for forward jumps later is a similar trick that shows up in many places).

## Summary

- Two levels of optimization: **on the AST** (constant folding + algebraic simplification) and **on the emitted asm** (peephole).
- AST optimization is just recursive rewriting of `Node *`. `optimize_ast(node)` returns a new (or the same) node.
- Peephole optimization splits asm into lines and rewrites patterns spanning two adjacent lines.
- **Backpatching**: accumulating output in a memory buffer makes `ftell`/`fseek` usable, so we can fill in the prologue's `subq $N` after the fact — that removes Phase 1 and makes codegen single-pass.
- **Block scope**: layered on top of single-pass codegen via `enter_scope` / `exit_scope`, which rewind the `locals` linked list's head. Sibling blocks reuse slots; `max_frame_size` holds the backpatch value.
- AST optimization and peephole can be disabled via `--no-opt`, but backpatching and scope are part of codegen's structure and are always on.
- `lexer.l / parser.y / ast.h / ast.c` are identical to ch06. The diff is concentrated in three places: `codegen.c` (backpatching + scope), `main.c` (always memstream), and the new `optimize.c`.
