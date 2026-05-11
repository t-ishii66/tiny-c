# 02 — Managing intermediate values with the stack

How do we evaluate a multi-operator expression like `2 + 3 * 4` using just the register `%eax`? The answer: **use the stack**.

## 1. The ch01 codegen, recalled

In ch01, `gen_expr` had a single case:

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        break;
    }
}
```

The contract was: "the value of an expression ends up in `%eax`." For an integer literal, `movl $42, %eax` and we're done.

In ch02 we add `NODE_BINARY` and `NODE_UNARY` cases here. The contract is unchanged: **after `gen_expr`, the result is in `%eax`**.

## 2. The problem with binary operators

`2 + 3` as assembly:

```
movl $2, %eax       # eax = 2
movl $3, %ecx       # ecx = 3
addl %ecx, %eax     # eax = eax + ecx = 5
```

But this only works because the operands are integer literals.

What about `(1 + 2) + (3 + 4)`, where each side is itself an expression?

- Compute the left side `(1 + 2)` in `%eax` → `%eax = 3`
- Compute the right side `(3 + 4)` in `%eax` → `%eax = 7`
- ...but where did the `3` from the left go? It got overwritten.

If we insist that "after `gen_expr`, `%eax` holds the value," then evaluating a different expression destroys the previous one. We don't have enough registers.

The tool that solves this is the **stack**.

## 3. Saving intermediate values to the stack

The stack is a special memory region whose address decreases on each function call. `%rsp` (the stack pointer) points to its current top. The CPU manipulates it with `pushq` and `popq`.

```
pushq %rax     # rsp -= 8;  *(rsp) = rax;
popq  %rcx     # rcx = *(rsp);  rsp += 8;
```

`pushq` saves the register value onto the stack; `popq` pulls one off. Last in, first out (LIFO).

With this, we can temporarily stash `%eax`, do another computation, and recover the value later. **Registers are limited, but the stack is orders of magnitude larger** — there's no shortage of room for intermediate values.

## 4. The binary-operator pattern

A binary operator — the shape **left(lhs) operator right(rhs)** (e.g. `a + b`, `x * y`) — is generated like this:

```
gen_expr(rhs)         # %eax = the right operand
pushq %rax            # save the right value on the stack
gen_expr(lhs)         # %eax = the left operand
popq %rcx             # pop the saved right value into %ecx
                      # at this point: %eax = left, %ecx = right
addl %ecx, %eax       # %eax = left + right
```

The order is the key: **evaluate the right side first, push it, evaluate the left side later**.

### Why this order

What we really want is "**generate every binary operator with the same pattern**." `+ - * / %` have different meanings, but the code-generation skeleton should be uniform — otherwise we'd be writing separate code for each operator.

To make that work, we standardize "**which register holds the left operand and which holds the right operand at the moment of the operation**." In tiny-c:

- Left operand (lhs) → `%eax`
- Right operand (rhs) → `%ecx`

Why this assignment? By contract, the expression's value sits in `%eax`. AT&T's `subl %ecx, %eax` computes `%eax = %eax - %ecx` — the result overwrites the left register (`%eax`). So **if `%eax` holds the lhs, the result of `lhs - rhs` stays in `%eax`**, matching the contract.

For commutative `+` and `*` we could swap left and right, but for non-commutative `-`, `/`, `%` (and comparison operators coming later) the order matters. So we fix the register assignment regardless of operator.

### At the implementation level

To produce the state "`%eax = lhs` and `%ecx = rhs` right before the operation," the order is:

1. Evaluate `rhs` first → `%eax = rhs` → push it on the stack
2. Evaluate `lhs` next → `%eax = lhs` (rhs is safe, it's already saved)
3. Pop the saved `rhs` into `%ecx`
4. Now `%eax = lhs`, `%ecx = rhs`. A single `addl` / `subl` / ... finishes the job

Summary:

| Instruction | Meaning (AT&T) |
|------|----------------|
| `addl %ecx, %eax` | `eax = eax + ecx` |
| `subl %ecx, %eax` | `eax = eax - ecx` |
| `imull %ecx, %eax` | `eax = eax * ecx` |

Add, subtract, and multiply share the same pattern. Just change the instruction.

## 5. Nesting doesn't break the pattern

Does this pattern actually work for `(1 + 2) + (3 + 4)`?

```c
gen_expr(outer BINARY)
  // right operand (3+4)
  gen_expr(right BINARY)
    gen_expr(4)         → movl $4, %eax
    pushq %rax            // [4] on stack
    gen_expr(3)         → movl $3, %eax
    popq %rcx             // ecx = 4
    addl %ecx, %eax       // eax = 3+4 = 7
  pushq %rax              // [7] on stack
  // left operand (1+2)
  gen_expr(left BINARY)
    gen_expr(2)         → movl $2, %eax
    pushq %rax            // [2, 7] on stack
    gen_expr(1)         → movl $1, %eax
    popq %rcx             // ecx = 2;  stack: [7]
    addl %ecx, %eax       // eax = 1+2 = 3
  popq %rcx               // ecx = 7
  addl %ecx, %eax         // eax = 3+7 = 10
```

The stack progresses `[7]` → `[2, 7]` → `[7]` → `[]`.

Because each level's `pushq`/`popq` come in pairs, the stack balances. **Each level of recursion allocates and frees the stack space it uses.**

## 6. Division and modulo are special

Up to `+ - *` we get away with two-operand instructions, but `/ %` are different. x86's `idivl` works like this:

- Divides a doubleword (a 64-bit dividend) by a 32-bit divisor.
- The divisor is the register passed as the argument, e.g. `idivl %ecx`.
- The dividend lives **in `%edx:%eax`, fixed** (`%edx` is the upper 32 bits, `%eax` the lower).
- The **quotient goes to `%eax`, the remainder to `%edx`**.

We work in 32-bit integers, but `idivl` only has a 64-bit ÷ 32-bit form. So before dividing, we need to widen the 32-bit dividend in `%eax` into the 64-bit `%edx:%eax`.

The instruction that does that is **`cdq`** (Convert Doubleword to Quadword). It sign-extends `%eax` into `%edx` (when `%eax` is non-negative, `%edx = 0`; when negative, `%edx = -1` (= 0xFFFFFFFF)). Now `%edx:%eax` is a correct signed 64-bit value.

```
gen_expr(rhs)         # eax = divisor
pushq %rax
gen_expr(lhs)         # eax = dividend
popq %rcx             # ecx = divisor
cdq                   # sign-extend eax into edx:eax
idivl %ecx            # eax = dividend/divisor, edx = remainder
```

For `%`, we tack on `movl %edx, %eax` at the end to move the remainder into `%eax` — to honor the contract that "the expression's value is in `%eax`."

```
cdq
idivl %ecx
movl %edx, %eax       # remainder → eax
```

## 7. Unary minus

`-x` takes one instruction: `negl %eax`.

```c
case NODE_UNARY:
    gen_expr(node->operand);   // eax = operand
    fprintf(out, "  negl %%eax\n");   // eax = -eax
    return;
```

`negl` flips the sign in two's-complement. The stack is untouched.

## 8. The full ch02 gen_expr

Three new cases is all it takes.

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT:
        fprintf(out, "  movl $%d, %%eax\n", node->int_val);
        return;
    case NODE_UNARY:                                                    /* added */
        gen_expr(node->operand);                                        /* added */
        fprintf(out, "  negl %%eax\n");                                 /* added */
        return;                                                         /* added */
    case NODE_BINARY:                                                   /* added */
        gen_expr(node->rhs);                                            /* added */
        fprintf(out, "  pushq %%rax\n");                                /* added */
        gen_expr(node->lhs);                                            /* added */
        fprintf(out, "  popq %%rcx\n");                                 /* added */
        switch (node->op) {                                             /* added */
        case '+': fprintf(out, "  addl %%ecx, %%eax\n"); return;        /* added */
        case '-': fprintf(out, "  subl %%ecx, %%eax\n"); return;        /* added */
        case '*': fprintf(out, "  imull %%ecx, %%eax\n"); return;       /* added */
        case '/':                                                       /* added */
            fprintf(out, "  cdq\n");                                    /* added */
            fprintf(out, "  idivl %%ecx\n");                            /* added */
            return;                                                     /* added */
        case '%':                                                       /* added */
            fprintf(out, "  cdq\n");                                    /* added */
            fprintf(out, "  idivl %%ecx\n");                            /* added */
            fprintf(out, "  movl %%edx, %%eax\n");                      /* added */
            return;                                                     /* added */
        }
    default:
        fprintf(stderr, "unknown expr\n");
        exit(1);
    }
}
```

`gen_stmt` and `codegen` don't change from ch01 at all.

## 9. Why push `%rax` when working with 32-bit values

`pushq %rax` pushes the entire 64-bit register. `movl $4, %eax` writes the low 32 bits (`%eax`), but on x86-64, writing to `%eax` **automatically zeroes the upper 32 bits**. So the whole `%rax` ends up holding the right value (zero on top, the integer in the bottom).

The stack's unit is 8 bytes, so we use the 64-bit `pushq`. A 32-bit-only `pushl` doesn't exist on x86-64 (operand-size rules forbid it).

## Next

In the last section (`03_build.md`) we lay out the full `lexer.l`, `parser.y`, `ast.h/c`, and `codegen.c`, then build and run.
