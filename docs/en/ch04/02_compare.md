# 02 — Codegen for comparison and logical negation

How do we express "expressions that produce a boolean," like `a < b` or `!x`, in assembly? Three new instructions appear: **`cmpl`**, **`setcc`**, and **`movzbl`**.

## 1. C has no bool type (and neither does tiny-c)

The result of C's comparison operators `<` `<=` `>` `>=` `==` `!=` is **`int`**: 1 for true, 0 for false.

```c
int x = (3 < 5);       // x = 1
int y = (10 == 11);    // y = 0
```

Same in tiny-c. A comparison expression's value is `int`, either 0 or 1. By contract, the value lands in `%eax`.

Logical negation `!` is the same: `!0` is 1, `!nonzero` is 0.

So "computing a boolean" boils down to "writing 0 or 1 into `%eax`."

## 2. The compare instruction `cmpl`

x86 has an instruction `cmpl src, dst` that compares two values:

- Action: internally computes `dst - src` (the result is thrown away).
- Side effect: updates the **flags register** with the result.
  - ZF (zero flag): 1 if the result is 0 (= `dst == src`)
  - SF (sign flag): 1 if the result is negative (signed)
  - OF (overflow), CF (carry), and so on are also set

So it's "doesn't actually subtract, but updates the flags *as if* it had."

Example:

```
movl $5, %eax      # eax = 5
movl $3, %ecx      # ecx = 3
cmpl %ecx, %eax    # internally: eax - ecx = 5 - 3 = 2
                   #   ZF=0, SF=0  (result is positive)
```

Subsequent instructions read these flags, which is how the comparison result gets used.

## 3. Conditional set `setcc`

`setXX` instructions **set an 8-bit register to 0 or 1** based on flags. The `XX` part is the condition name.

| Instruction | Condition | Set when true |
|------|------|------------|
| `sete`  | ZF == 1 (equal) | result of `==` |
| `setne` | ZF == 0 (not equal) | result of `!=` |
| `setl`  | signed less than | result of signed `<` |
| `setle` | signed less or equal | result of `<=` |
| `setg`  | signed greater than | result of `>` |
| `setge` | signed greater or equal | result of `>=` |

The "l" in `setl` is "less." `setle` is "less or equal." `setg` is "greater."

The **destination operand of these instructions is restricted to an 8-bit register**. We can't put `%eax` (32-bit) directly; we use `%al`, which names the low 8 bits:

```
cmpl %ecx, %eax    # compare
setl %al           # %al = 1 if %eax < %ecx else 0
```

## 4. Zero extension `movzbl`

Updating only `%al` leaves **old garbage** in the upper 24 bits of `%eax`. To make all of `%eax` exactly 0 or 1, we zero-extend `%al` into `%eax`.

```
movzbl %al, %eax   # %eax = (32-bit unsigned extension of %al)
```

`movzbl` = "**mov** **z**ero-extend **b**yte to **l**ong." Copies the low 8 bits to a 32-bit register, filling the upper 24 with zeros.

Now `%eax` is exactly 0 or 1. The "value in `%eax`" contract is honored.

## 5. The three-instruction comparison set

The full pattern for `a < b`:

```
gen_expr(rhs)         # eax = b
pushq %rax
gen_expr(lhs)         # eax = a
popq %rcx             # ecx = b
                      # now: eax = a, ecx = b
cmpl %ecx, %eax       # eax - ecx = a - b, updates flags
setl %al              # %al = 1 if a < b
movzbl %al, %eax      # %eax = 1 or 0
```

The "rhs evaluation → push → lhs evaluation → pop" pattern established in ch02 stays the same. Only the last operation changes — from `addl` to three instructions `cmpl + setl + movzbl`.

`<= > >= == !=` follow the exact same shape; only the `setl` is swapped for a different instruction.

```c
case '<':   emit_compare("setl");  return;
case OP_LE: emit_compare("setle"); return;
case '>':   emit_compare("setg");  return;
case OP_GE: emit_compare("setge"); return;
case OP_EQ: emit_compare("sete");  return;
case OP_NE: emit_compare("setne"); return;
```

`emit_compare` is a small helper that emits the three instructions:

```c
static void emit_compare(const char *setcc) {
    fprintf(out, "  cmpl %%ecx, %%eax\n");
    fprintf(out, "  %s %%al\n", setcc);
    fprintf(out, "  movzbl %%al, %%eax\n");
}
```

One helper, six comparison operators, all neatly together.

## 6. Mind the operand order of `cmpl`

AT&T syntax `cmpl src, dst` computes **`dst - src`**. The same order as `addl`, `subl`, etc. (the operation goes to the right register).

`cmpl %ecx, %eax` computes `eax - ecx`. In our codegen, `eax = lhs` and `ecx = rhs`, so the flags reflect the sign of `lhs - rhs`.

- When `lhs < rhs`, `lhs - rhs < 0`, so the flags make `setl %al` produce 1.
- When `lhs > rhs`, `lhs - rhs > 0`, so `setg %al` produces 1.
- When `lhs == rhs`, `lhs - rhs == 0`, so `sete %al` produces 1.

Just what we want. Combined with ch02's convention ("evaluate rhs first → rhs in ecx"), the semantics fall out naturally.

If we reversed the order (`cmpl %eax, %ecx`), the sign of the flags would flip and every comparison would invert. **We can write comparisons naturally because we kept ch02's ordering (rhs → ecx, lhs → eax).**

## 7. Logical negation `!`

`!x` is equivalent to `x == 0`. The implementation is the same:

```
gen_expr(operand)     # eax = x
cmpl $0, %eax         # update flags from eax - 0
sete %al              # %al = 1 if eax == 0
movzbl %al, %eax      # %eax = 1 or 0
```

`cmpl $0, %eax` is "compare `%eax` against 0." `sete` follows with "1 if equal."

Almost the same as a binary comparison. The differences: the right operand is the immediate `$0`, and there's no stack push/pop.

```c
case '!':
    fprintf(out, "  cmpl $0, %%eax\n");
    fprintf(out, "  sete %%al\n");
    fprintf(out, "  movzbl %%al, %%eax\n");
    return;
```

One added case in the unary `switch`.

## 8. Comparison codegen as a whole (additions to gen_expr)

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    /* ... cases from ch03 ... */
    case NODE_UNARY:
        gen_expr(node->operand);
        switch (node->op) {
        case '-':
            fprintf(out, "  negl %%eax\n");
            return;
        case '!':                                    /* added */
            fprintf(out, "  cmpl $0, %%eax\n");       /* added */
            fprintf(out, "  sete %%al\n");            /* added */
            fprintf(out, "  movzbl %%al, %%eax\n");   /* added */
            return;                                   /* added */
        }
        ...
    case NODE_BINARY:
        gen_expr(node->rhs);
        fprintf(out, "  pushq %%rax\n");
        gen_expr(node->lhs);
        fprintf(out, "  popq %%rcx\n");
        switch (node->op) {
        /* ... ch02's +-*/% ... */
        case '<':   emit_compare("setl");  return;   /* added */
        case OP_LE: emit_compare("setle"); return;   /* added */
        case '>':   emit_compare("setg");  return;   /* added */
        case OP_GE: emit_compare("setge"); return;   /* added */
        case OP_EQ: emit_compare("sete");  return;   /* added */
        case OP_NE: emit_compare("setne"); return;   /* added */
        }
        ...
    }
}
```

## Next

In the next section (`03_control.md`) we put these booleans to use for **branching** — `je`, `jmp`, and **labels**.
