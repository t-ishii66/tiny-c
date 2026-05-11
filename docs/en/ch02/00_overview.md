![](../../../images/img-2.png)

# Chapter 2 — Build a calculator

## 0. Introduction

In Chapter 1 we got `int main() { return 42; }` working — the skeleton is complete. In Chapter 2 we add **arithmetic** to that skeleton.

The goal of this chapter:

```c
int main() {
    return 2 + 3 * 4;
}
```

We want a compiler that correctly returns `14` for this (it's `2 + (3*4) = 14`, not `(2+3)*4 = 20`).

We get the four arithmetic operators (`+ - * / %`), parentheses, and the unary minus (`-x`). In short, we build **a calculator for integer expressions inside C**.

```c
return (10 - 3) * 2 + 100 / 4 % 7;   // works
return -5 + 8;                        // works
return -(2 + 3);                      // works
```

## 1. Diff against ch01

There isn't much to add.

| File | Change |
|---------|------|
| `lexer.l` | Add five tokens for `+ - * / %` |
| `parser.y` | Rewrite the expression grammar as a **hierarchy that encodes precedence** |
| `ast.h` / `ast.c` | Add `NODE_BINARY` and `NODE_UNARY` |
| `codegen.c` | Generate code for binary and unary operators. Managing intermediate values via the stack shows up here |
| `main.c` / `Makefile` | **No change** |

Two central themes for the chapter:

- **How grammar expresses precedence** — how should `parser.y` be written so `2 + 3 * 4` is interpreted correctly as `2 + (3 * 4)`?
- **Managing intermediate values with the stack** — we still only use `%eax`, yet complex expressions evaluate correctly. How?

## 2. Run it first

Feed a `.c` file to **the `tinyc` we build in this chapter** and assembly comes out.

```bash
$ cat test.c
int main() {
    return 2 + 3 * 4;
}

$ ./tinyc test.c
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
```

Eleven lines, with `pushq`, `popq`, `imull`, and `addl`. This is the canonical shape of "assembly that evaluates an expression."

Turn it into an executable and the exit code is 14.

```bash
$ ./tinyc test.c > test.s
$ gcc -o test test.s
$ ./test ; echo $?
14
```

Peek at the AST with `--dump-ast` and you can see the precedence baked into the tree shape.

```bash
$ ./tinyc --dump-ast test.c
FUNC_DEF main
  BLOCK
    RETURN
      BINARY +
        INT_LIT 2
        BINARY *
          INT_LIT 3
          INT_LIT 4
```

The right child of the `+` node is the `*` node. The structure "first compute `3*4`, then add the result to `2`" is expressed in the shape of the tree itself.

## 3. Section layout

| Section | Contents |
|--------|------|
| 00 (this file) | Map of the chapter |
| 01 | Encoding precedence in the grammar — the layered structure of parser.y |
| 02 | Managing intermediate values with the stack — binary operators in codegen.c |
| 03 | The complete files with their diffs; build and run |

Unlike Chapter 1, we don't re-explain flex, bison, ASTs, or x86 basics. We use the same tools from Chapter 1.

## 4. Next

In the next section (`01_precedence.md`) we look at why `2 + 3 * 4` becomes `2 + (3*4)` — the mechanism at the grammar level.
