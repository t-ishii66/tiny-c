![](../../../images/img-6.png)

# Chapter 7 — Optimization and backpatching

## 0. Introduction

The assembly produced through ch01–ch06 has plenty of waste.

On top of that, the ch06 codegen was a **two-phase** structure (collect_locals → gen_stmt). The prologue's `subq $N, %rsp` couldn't be written until N (the frame size) was known, so we had to walk the AST once to determine N before emitting the body.

Chapter 7 tackles both of these "issues" together. The key is **buffering the assembly output in memory**. From that one move:

| Theme | Contents | Relation to the buffer |
|------|------|----------------|
| **AST optimization** (section 01) | Constant folding + algebraic simplification | None directly (lives on the AST) |
| **Backpatching** (section 02, required) | Patch the prologue's `subq $N` afterwards → codegen becomes single-pass | `ftell` / `fseek` on the buffer |
| **Block scope** (section 03) | A by-product of single-pass codegen: `{ int x=1; }{ int x=2; }` works | None directly (rides on codegen's flow) |
| **Peephole optimization** (section 04) | Look at the emitted asm and rewrite adjacent instructions | Read the buffer and re-emit |

Backpatching and optimization are **different things**. Backpatching is structural plumbing for codegen — it doesn't change what the code does. Optimization actually shortens the code. `--no-opt` disables AST optimization and peephole only; **backpatching is always on**. Block scope is a language-feature addition.

Where chapters 1–6 built "the compiler's body (lexer → parser → AST → codegen)," chapter 7 adds:

```
source → lex → parse → AST → [AST opt] → codegen → asm → [peephole] → output asm
                              ↑                ↑                ↑
                              ch07 adds       single-pass        ch07 adds
                                              (backpatch)
```

The asm is written to an in-memory buffer, polished by the peephole, and then flows to stdout. Backpatching happens on the same buffer.

## 1. The effect, sampled

Compiling `return 2 + 3 * 4;`:

**Without optimization** (11 instruction lines)

```asm
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
```

**With optimization** (5 instruction lines)

```asm
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $14, %eax
  leave
  ret
```

`2 + 3 * 4` is **folded to 14 at compile time**, and the dead code after the implicit `return` is eliminated. The instruction count drops too.

`printf("hello\n")` also benefits:

**Without optimization**: `pushq %rax; popq %rdi` remains.
**With optimization**: replaced with a single `movq %rax, %rdi`, and trailing dead code is gone.

## 2. Diff against ch06

| File | Change |
|---------|------|
| `lexer.l` / `parser.y` / `ast.h` / `ast.c` | **No change** |
| `codegen.c` | **Two-phase → single-pass** (`collect_locals` removed); backpatch the prologue with `ftell`/`fseek`; add block scope (`enter_scope` / `exit_scope`) |
| `optimize.h` / `optimize.c` | **New** — two functions, `optimize_ast` and `peephole` |
| `main.c` | Always use `open_memstream`; `--no-opt` support; route through peephole |
| `Makefile` | Add `optimize.c` to the build |

Optimization (AST + peephole) is **a completely separate pass** flanking codegen. Backpatching is an **internal improvement** to codegen — from the outside, it just turned two phases into one; the emitted code is the same as ch06. Block scope is **a language-feature addition** that drops in naturally now that codegen is single-pass.

## 3. Usage

```bash
$ ./tinyc test.c              # optimized (default)
$ ./tinyc --no-opt test.c     # unoptimized (for comparison)
$ ./tinyc --dump-ast test.c   # print the AST (post-optimization)
$ ./tinyc --no-opt --dump-ast test.c  # print the AST (pre-optimization)
```

`--no-opt` lets us directly compare before/after, which is how we demo things in ch07.

## 4. Section layout

| Section | Contents |
|--------|------|
| 00 (this file) | Map of the chapter |
| 01 | AST-level optimization — constant folding + algebraic simplification |
| 02 | Eliminating Phase 1 with backpatching — patch the prologue's `subq $N` and make codegen single-pass |
| 03 | Block scope — drops in naturally on the single-pass codegen; `locals` head save/restore (with slot reuse) |
| 04 | Peephole optimization — look at emitted asm and rewrite |
| 05 | The complete diffs, build, and before/after comparisons |

## 5. Next

In section 01 (`01_const_fold.md`) we look at the inside of AST optimization. Why does `2 + 3 * 4` become `14` before reaching codegen? Why does `x + 0` become just `x`? The mechanism is one small recursive function.
