![](../../images/img-1.png)

# Chapter 1 — A compiler that just returns 42

## 0. Introduction — this chapter is special

We're going to build **a compiler for a tiny subset of C**. The goal is **to understand how compilers work by assembling one with your own hands**. We start with a very small compiler and add features as the chapters progress.

The compiler we build in Chapter 1 can compile exactly one kind of program:

```c
int main() {
    return 42;
}
```

No addition. No variables. No `if`, no `while`. The only thing allowed after `return` is a single integer literal.

Even so, every part that a real compiler has is already in there:

- Read the source and break the characters into tokens (**lexical analysis**)
- Read the structure of the token stream and build it into a tree (**parsing**)
- Walk the tree and emit x86 assembly (**code generation**)

If you link the emitted assembly with `gcc`, you get an executable. Its exit code is 42.

From Chapter 2 onward we put flesh on this skeleton, but **the skeleton itself carries through to the end**. That's why Chapter 1 alone is split into seven sub-sections.

| Section | Contents |
|--------|------|
| 00 (this file) | A map of the whole chapter, and a confirmation that "this thing actually runs" |
| 01 | Regular expressions — the minimum needed for flex |
| 02 | Intro to flex + detailed walkthrough of the lexer `lexer.l` |
| 03 | Intro to bison + detailed walkthrough of the parser `parser.y` |
| 04 | What an abstract syntax tree is + detailed walkthrough of `ast.h` / `ast.c` |
| 05 | The minimum of x86 assembly + detailed walkthrough of `codegen.c` |
| 06 | `main.c` and `Makefile` — bringing it all together |


## 1. Run it first

Source file `test.c`:

```c
int main() {
    return 42;
}
```

Feed it to the compiler `tinyc` and out comes x86 assembly:

```bash
$ ./tinyc test.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $42, %eax
  leave
  ret
```

Hand that assembly to `gcc` and you get an executable. `gcc` invokes the linker (which stitches a program together) and the assembler (which turns assembly into machine code) internally for you.

```bash
$ ./tinyc test.c > test.s
$ gcc -o test test.s
$ ./test
$ echo $?
42
```

`echo $?` is the shell idiom for "show the exit code of the last program." The C source `return 42;` has genuinely become "a program that exits with code 42."


## 2. Peeking inside with `--dump-ast`

tinyc has a `--dump-ast` option that prints the tree structure (**AST**, Abstract Syntax Tree) the compiler builds internally.

```bash
$ ./tinyc --dump-ast test.c
FUNC_DEF main
  BLOCK
    RETURN
      INT_LIT 42
```

The source code `int main() { return 42; }` has been converted into this tree.

- At the top, "function definition `main`"
- Inside it, a "block `{...}`"
- Inside the block, a "return statement"
- The return statement's target: "integer literal 42"

The compiler first converts the source into this tree, then walks the tree to emit assembly. **Source → tree → assembly**, two stages.

```
Source                 Tree (AST)            Assembly
"int main() {          FUNC_DEF main         main:
  return 42;             BLOCK                 pushq %rbp
}"                         RETURN              movq %rsp, %rbp
                             INT_LIT 42        movl $42, %eax
                                               leave
                                               ret
```

Why route through a tree? In raw string form, extracting "the expression after `return`" is fiddly; on a tree you just say "the return node's child" and there it is. We convert it into a form that's easier to work with inside the compiler.


## 3. The files we'll write

There are seven files in Chapter 1.

| File | What it does |
|---------|---------|
| `src/lexer.l` | Turns the **string** of source code into a stream of **tokens** (written in flex) |
| `src/parser.y` | Assembles the token stream into a **tree (AST)** (written in bison) |
| `src/ast.h` / `src/ast.c` | Node types of the tree, plus functions to build nodes |
| `src/codegen.h` / `src/codegen.c` | Walks the tree and emits **assembly** |
| `src/main.c` | Handles command-line arguments and ties the above together |
| `Makefile` | Build instructions for the compiler itself |


## 4. The overall flow

```
+-----------+
| test.c    |  Human-written tiny-c source / int main() { return 42 ; }
+-----+-----+
      |
      v
+-----------+
| lexer.l   |  flex generates C code
| (lex.yy.c)|  → splits the string into tokens
+-----+-----+
      | int, main, (, ), {, return, 42, ;, }
      v
+-----------+
| parser.y  |  bison generates C code
|(parser.tab|  → builds a tree from the token stream
| .c)       |  along the way, it calls constructors from ast.c
+-----+-----+
      |
      v
+-----------+
| Node tree |  the AST, in memory
+-----+-----+
      |
      v
+-----------+
| codegen.c |  walks the tree and emits assembly
+-----+-----+
      |
      v
+-----------+
| test.s    |  x86 assembly
+-----------+
```

`lexer.l` and `parser.y` are not C code as-is. **flex** and **bison** generate C source files (`lex.yy.c`, `parser.tab.c`) from these definition files. We then compile the generated C alongside our own C (`ast.c`, `codegen.c`, `main.c`) to produce an executable called `tinyc`.

To build a compiler, we use another compiler (gcc) and two code-generation tools (flex, bison).


## 5. Assumed background

- **Basic C**: structs, pointers, functions, `malloc`/`calloc`.
- **Basic shell usage**: `cd`, `make`, `echo`, `./prog`, etc.

Things we don't assume — we cover them inline as needed:

- Regular expressions (→ section 01)
- How to use flex (→ section 02)
- How to use bison (→ section 03)
- x86 assembly (→ section 05)


## 6. Target environment

The target is **x86-64 Linux**. The emitted assembly is x86-64, and the calling convention is the System V AMD64 ABI.

On Mac (Apple Silicon) or Windows the compiler itself (`tinyc`) still builds and runs — it just emits assembly as a string, so it can run anywhere. But to turn that assembly into an executable you need an x86-64 Linux environment (Docker or WSL is the easiest way).

If you only want to check the AST with `--dump-ast`, anything works.


## 7. Next

In the next section (`01_regex.md`) we cover the basics of **regular expressions** — just the amount that flex needs.
