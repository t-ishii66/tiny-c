# 05 — A minimum of x86 assembly, and codegen.c

The assembly we emit in Chapter 1 is just seven lines.

```asm
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $42, %eax
  leave
  ret
```

We'll build up the meaning of these seven lines from the minimum amount of background.


## 1. What assembly is

What a CPU actually executes is **machine code** — a sequence of binary instructions. Strings of numbers like `0x48 0x89 0xe5`. Writing that by hand is impossible.

So we give each machine instruction a **mnemonic** (a human-readable name); that's **assembly language**.

```
machine code: 48 89 e5
assembly:     movq %rsp, %rbp     ← human-readable
```

Assembly and machine code are in **one-to-one correspondence**. If you write `movq %rsp, %rbp` in assembly, it always becomes the byte sequence `48 89 e5`. The thing that does the conversion is the **assembler** (`as`, or the one built into `gcc`).

In other words, assembly is "machine code you can read and write." If the compiler emits assembly, the assembler and linker turn it into an executable.


## 2. Registers — the CPU's scratchpad

When the CPU does computation, it uses small storage areas called **registers**. Registers live inside the CPU and are vastly faster than memory. Think of them as a "workbench" where you keep values temporarily while computing.

x86-64 has 16 general-purpose registers. They're 64 bits wide and have names that start with `r`: `%rax`, `%rbx`, `%rcx`, and so on. Chapter 1 uses only three of them:

| Register | Width | Role |
|---------|----|------|
| `%rax` | 64-bit | The star of return values and computation. Its low 32 bits go by the alias `%eax` |
| `%rsp` | 64-bit | Stack pointer — points to the top of the stack |
| `%rbp` | 64-bit | Base pointer — anchors a function's stack frame |

The leading `%` on register names is AT&T syntax.

The relationship between `%rax` and `%eax`:

```
+----------------------+
|         %rax         |   ← the full 64 bits
+----------+----------+
            |   %eax   |   ← the low 32 bits
            +---+------+
                | %ax  |   ← the low 16 bits
                +------+
```

We use `%eax` for integers (`int` is 32 bits) and `%rax` for pointers and addresses (those need 64 bits).


## 3. How to write instructions — AT&T syntax

There are two flavors of x86 assembly: Intel syntax and AT&T syntax. We use **AT&T syntax** (it's what gcc uses by default). Linux's `as` assembler also defaults to AT&T.

Hallmarks of AT&T syntax:

- Register names get a `%` prefix (`%eax`)
- Immediate values (constants) get a `$` prefix (`$42`)
- Operand order is **"source → destination"** (`mov src, dst`)
- Each instruction takes a **size suffix** at the end (`b`, `w`, `l`, `q` = 1, 2, 4, 8 bytes)

Examples:

```asm
movl $42, %eax        # put 42 into eax (4-byte move)
movq %rsp, %rbp       # copy rsp into rbp (8-byte move)
pushq %rbp            # push the value of rbp onto the stack (8 bytes)
```

## 4. Functions and the stack — where values live

When you call a function, you need:

1. **A return address** — when the called function ends, control comes back to the caller.
2. **A place for local variables.**
3. **Scratch space for intermediate results** — there are only so many registers, so complex expressions need to spill values somewhere.

The thing that serves all three is the **stack**. The stack is a region of memory that grows on each call and shrinks on return.

On x86-64, the stack "**grows from high to low addresses**." "Down" in this context means **toward smaller numeric addresses**. By convention we draw this upside-down, so the top of the stack is "at the bottom" of the picture.

Here's what `pushq %rbp` (which appeared above) actually does to the stack, in a diagram:

```
high addr  ┌──────────────┐
           │              │
           │              │
           │     ...      │  ← %rsp is here before the push (the previous top)
           ├──────────────┤
           │ caller's rbp │  ← %rsp is here after the push (moved 8 bytes down)
           ├──────────────┤
           │              │
low addr   └──────────────┘
```

`%rsp` is the register that points to **the top of the stack**. Pushing a value (`push`) moves `%rsp` down; popping (`pop`) moves it back up. Concretely, `pushq %rbp` is the two-step action "move `%rsp` 8 bytes down, then write the value of `%rbp` at that location."

`%rbp` is the register that points to **the base of the current function's frame**. When code inside a function asks "where do my local variables live?", the answer is given as an offset from `%rbp`. `%rbp` doesn't move from the start of the function to the end, so it provides a stable anchor.


## 5. Function prologue and epilogue

There's a **standard pattern** when implementing a function on x86-64. The code at a function's entrance is called the **prologue**, and the code at the exit is the **epilogue**.

### Prologue

```asm
pushq %rbp           # save the caller's rbp on the stack
movq %rsp, %rbp      # set rbp to the current rsp (start our own frame)
```

Line 1: `%rbp` currently holds the caller's frame base. To avoid clobbering it, we save it on the stack.
Line 2: Set `%rbp` to the current stack top. That marks the start of our frame. From now until this function returns, `%rbp` doesn't move.

### Epilogue

```asm
leave                # restore rbp; bring rsp back to its original value
ret                  # jump to the return address
```

Line 1: `leave` is really two instructions fused into one. First `movq %rbp, %rsp` ("bring rsp back to the bottom of our frame"), then `popq %rbp` ("restore the caller's rbp that we saved").
Line 2: `ret` pops the return address off the stack and jumps to it. Control flows back to the caller.

Chapter 1 doesn't use local variables, so the prologue is just "save and update rbp," and the epilogue is just `leave` and `ret` — the simplest possible form. Chapter 3 brings in variables, and the prologue grows slightly (an instruction is added to reserve frame space).


## 6. Returning a value — the `%eax` convention

When a C function returns a value, where does it put it?

The answer is **the `%eax` register**. It's specified by the **System V AMD64 ABI** calling convention.

```
return 42;   →   movl $42, %eax    + a ret instruction
```

`%eax` is the low 32 bits of `%rax`. The convention is to put a 32-bit integer (`int`) return value into `%eax`. For values that fit in `int`, that's all you need. When `ret` runs with 42 in `%eax`, the caller (OS loader → libc's `_start` → the caller of `main`) sees "the return value of main is 42."

That becomes the process **exit code**. That's why `echo $?` prints 42.

"**Put the `int` return value in `%eax` and `ret`.**"


## 7. The full picture

The seven lines again:

```asm
  .text                  # what follows is code
  .globl main            # expose main to outside the file
main:                    # label marking the entrance of function main
  pushq %rbp             # ┐
  movq %rsp, %rbp        # ┴ prologue: set up the stack frame
  movl $42, %eax         # put the return value 42 into eax
  leave                  # ┐
  ret                    # ┴ epilogue: return to the caller
```

Lines starting with `.` are **directives** — instructions to the assembler. They're not CPU instructions; they say "what follows is the code region," or "expose this name externally."

| Line | Kind | Meaning |
|----|-----|------|
| `.text` | directive | Start the code section |
| `.globl main` | directive | Make `main` visible to the linker |
| `main:` | label | Attach the name `main` to this location |
| `pushq %rbp` | instruction | Save `%rbp` on the stack |
| `movq %rsp, %rbp` | instruction | Start our own frame |
| `movl $42, %eax` | instruction | Put the return value into `%eax` |
| `leave` | instruction | Tear down the frame |
| `ret` | instruction | Return to the caller |

Without the `main` label, the linker asks "where is `main`?". Without `.globl`, `main` is invisible to the linker (it becomes a private name visible only inside the file).


## 8. Reading codegen.c

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "codegen.h"

static FILE *out;

/* Generate code for an expression — result goes into %eax */
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}

/* Generate code for a statement */
static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        gen_expr(node->expr);
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        break;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}

/* Generate code for the entire program */
void codegen(Node *prog, FILE *output) {
    out = output;

    /* prog is a single function definition (for now) */
    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

Three functions. Let's go through them.

### `gen_expr` — put the expression's value into `%eax`

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}
```

Code generation for expressions. **One contract: leave the expression's value in `%eax`.**

In Chapter 1 the only expression kind is the integer literal. We just emit a `movl $42, %eax`-style instruction.

In `fprintf(out, "  movl $%d, %%eax\n", node->int_val)`, the `%%` is the printf way of emitting a literal `%`. We want to emit the literal register name `%eax`, so we write `%%eax`.

In Chapter 2 expressions get richer, and `gen_expr` grows considerably (`+` and `*` handling are added). But **the contract doesn't change**: "put the expression's value in `%eax`."

### `gen_stmt` — convert a statement to code

```c
static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN:
        gen_expr(node->expr);
        fprintf(out, "  leave\n");
        fprintf(out, "  ret\n");
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            gen_stmt(l->node);
        break;
    default:
        fprintf(stderr, "unknown stmt\n");
        exit(1);
    }
}
```

Code generation for statements.

For `NODE_RETURN`:
1. **First, evaluate the expression** (`gen_expr(node->expr)`) — afterwards `%eax` holds the return value.
2. Then emit `leave` and `ret`.

In just three lines, this "correctly implements `return 42;` as a return statement." We delegate the expression sub-problem to `gen_expr`; our job is just to emit the "return" part.

For `NODE_BLOCK`:
Just recurse through the statements in the list. The block itself emits no code. This is exactly the "AST + recursion" pattern from the previous section.

### `codegen` — the entry point

```c
void codegen(Node *prog, FILE *output) {
    out = output;

    fprintf(out, "  .text\n");
    fprintf(out, "  .globl %s\n", prog->name);
    fprintf(out, "%s:\n", prog->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    gen_stmt(prog->body);
}
```

The entry point that `main.c` calls.

1. Stash the output `FILE *` in a module-static variable (so `gen_expr` and `gen_stmt` can use it).
2. Emit the directives and labels (`.text`, `.globl main`, `main:`).
3. Emit the prologue.
4. Process the function body (the block) via `gen_stmt`.

In Chapter 1 a program is a single function definition, so this simple structure is enough. Chapter 5 introduces multiple functions, and we'll turn this into a loop.

Note that `prog->name` is the string `"main"` — the identifier name we received as `$2` in the bison action `new_func_def($2, ...)`. So this structure handles any function name, not just `main`.

### codegen.h

```c
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"

void codegen(Node *program, FILE *out);

#endif
```

## 9. Tree shape ⇄ instruction order

Lay the AST of `int main() { return 42; }` next to its assembly:

```
AST:                       Assembly:

                           .text
                           .globl main
FUNC_DEF main              main:
                             pushq %rbp
                             movq %rsp, %rbp
  BLOCK
    RETURN
      INT_LIT 42             movl $42, %eax
                             leave
                             ret
```

- The `FUNC_DEF` node produces the prologue.
- The `INT_LIT` under the `RETURN` node produces the `movl`.
- The `RETURN` node itself produces the epilogue.

When you walk the AST recursively, the visit order naturally becomes the output order of the instructions. The fact that "tree shape" and "instruction order" line up is the deep reason a compiler can be written so simply.


## Next

In the last section (`06_build.md`) we look at `main.c` and the `Makefile` that tie everything together — completing the compiler.
