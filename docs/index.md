---
title: "tiny-c — Build Your Own C Compiler Step by Step"
description: "A C compiler tutorial in seven chapters. Build a working C compiler in about 1500 lines of C, with lexer (flex), parser (bison), AST, and x86-64 code generation. Free, open source, and educational."
keywords: "C compiler tutorial, build your own compiler, write a compiler in C, educational compiler, small C compiler, x86-64 assembly tutorial, lexer, parser, abstract syntax tree, AST, code generation, flex, bison, System V AMD64 ABI, calling convention, stack frame, recursive descent, LALR parser, compiler construction, programming language implementation, learn compilers, compiler from scratch, teaching compiler, compiler design, x86-64 Linux, lvalue rvalue, pointer array, block scope, constant folding, peephole optimization, backpatching"
lang: en
---

[English](./) &nbsp;|&nbsp; [日本語](jp/)

# tiny-c — Build Your Own C Compiler Step by Step

<img src="images/top.png" width="800" alt="tiny-c — an educational C compiler that compiles a subset of C to x86-64 assembly">

**tiny-c** is a teaching C compiler that compiles a tiny subset of C to x86-64 assembly. The compiler source is about **1500 lines of C**. The documentation is written so that knowledge of C alone is enough to follow along — reading the seven chapters in order naturally walks you through every line of the codebase.

If you have ever wanted to **build your own C compiler from scratch**, understand how **lexers, parsers, and code generators** fit together, or see how a real compiler emits **x86-64 assembly** for variables, pointers, function calls, and recursion — tiny-c is for you.

## What tiny-c compiles

```c
int fib(int n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);   // → 55
}
```

Recursion, loops, pointers, arrays, function calls, `printf`, and block scope all work. Constant folding and peephole optimization shrink the output assembly.

## What you'll learn

Each chapter presents **a complete, working small compiler**. As chapters progress, the language grows and so does the compiler. Topics covered:

- **Lexical analysis** with **flex** (regular expressions, tokens)
- **Parsing** with **bison** (LALR(1) grammars, precedence, associativity)
- **Abstract syntax tree (AST)** construction and traversal
- **Code generation** to x86-64 assembly (AT&T syntax)
- **Stack frames**, `%rbp` / `%rsp`, 16-byte alignment
- **System V AMD64 ABI**: register argument passing (`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`), variadic calls, `printf`
- **lvalue / rvalue** duality and the symmetry between `&` and `*`
- **Pointer arithmetic**, 1-D arrays, array decay
- **Global variables** in `.bss`, **string literals** in `.rodata`, RIP-relative addressing
- **Control flow** (`if`/`else`, `while`) via conditional jumps and labels
- **AST-level optimization**: constant folding, algebraic simplification
- **Peephole optimization** on emitted assembly
- **Backpatching** with `ftell` / `fseek` on a memory buffer
- **Block scope** and stack slot reuse across sibling blocks

## Chapters

| Chapter | What it can compile | Focus |
|---|------|------|
| [Ch1: A compiler that just returns 42](en/ch01/00_overview.html) | `int main() { return 42; }` | The full pipeline (lexer → parser → AST → code generation → build) |
| [Ch2: Build a calculator](en/ch02/00_overview.html) | `return 2 + 3 * 4;` | How grammar expresses operator precedence; the evaluation stack |
| [Ch3: Variables](en/ch03/00_overview.html) | `int x = 1; int y = 2; return x + y;` | Variables as stack-frame addresses; the symbol table |
| [Ch4: Branches and loops](en/ch04/00_overview.html) | `if/else`, `while`, comparison operators | The CPU only knows conditional jumps; label generation |
| [Ch5: Calling and defining functions](en/ch05/00_overview.html) | Function definitions, parameters, recursion, `printf` | System V AMD64 ABI, register passing, 16-byte stack alignment |
| [Ch6: Pointers and arrays](en/ch06/00_overview.html) | `char *s = "hello"; printf("%s\n", s);` | The lvalue/rvalue duality; the `gen_addr` function; `&` and `*` symmetry |
| [Ch7: Optimization, backpatching, scope](en/ch07/00_overview.html) | `{ int x=1; }{ int x=2; }` (block scope) | AST optimization, single-pass codegen via backpatching, block scope, peephole optimization |

## Build & run

Prerequisites: `gcc`, `flex`, `bison`, `make`.

```bash
git clone https://github.com/t-ishii66/tiny-c.git
cd tiny-c
make
./tinyc hello.c               # write assembly to stdout
./tinyc --dump-ast hello.c    # print the AST instead

# Produce an executable (x86-64 Linux)
./tinyc hello.c > hello.s
gcc -o hello hello.s
./hello
```

## Supported subset of C

| Feature | Examples |
|------|-----|
| Types | `int`, `char`, `void`, `int*`, `char*` |
| Literals | `42`, `'a'`, `"hello"` |
| Operators | `+` `-` `*` `/` `%` `==` `!=` `<` `<=` `>` `>=` `!` `-` (unary) `&` `*` (dereference) |
| Control flow | `if`/`else`, `while` |
| Functions | Definition and call (up to 6 arguments) |
| Variables | Local (initializer required), global |
| Arrays | 1-D only (`int a[10];`) |
| Pointers | 1 level only (`int *p`) |

Deliberately excluded: `for`, `switch`, `&&`, `||`, structs, float, preprocessor — minimalism is part of the teaching.

## Why tiny-c?

Most compiler textbooks either drown you in theory or jump to LLVM. tiny-c sits in the middle: **enough working C code to be real**, **small enough to read in an afternoon**. By the end, you will have seen — line by line — how a Turing-complete C subset turns into x86-64 machine code.

The compiler itself is the teaching artifact. Every line is reachable from the chapters; every chapter ends with a runnable compiler.

## License & credits

- Concept, structure, code review, documentation review: **t-ishii66**
- Coding, documentation: **Claude Opus 4.7** (Anthropic)
- Illustrations: ChatGPT 5.4 (OpenAI)
- License: see the repository
- Version: 1.0.0 (2026-05-11)

[GitHub repository](https://github.com/t-ishii66/tiny-c) &nbsp;|&nbsp; [日本語版](jp/)
