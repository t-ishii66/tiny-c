# 03 — Codegen for if and while

`if` and `while` in C ultimately translate into just two kinds of jumps:

- **Unconditional jump** (`jmp`): always jump somewhere else.
- **Conditional jump** (`je`, `jne`, `jl`, ...): jump or not, depending on the flags.

The compiler's job is to translate the source's control structure into **jumps and labels**.

## 1. Labels and jumps

A **label** is "a name for a jump target" in assembly.

```
.Lhello:           # this is the label ".Lhello"
   ...instructions...
   jmp .Lhello     # unconditionally jump to .Lhello
```

The `.Lhello:` line isn't an instruction — it just names this location. `jmp .Lhello` overwrites the CPU's `%rip` (instruction pointer) with that location.

Names starting with `.L` are by convention **local labels** — markers that aren't exposed to the linker, used within a single file. We add numbers to keep them unique within a function.

```c
/* codegen.c */
static int label_count;
static int new_label(void) { return label_count++; }
```

Each call to `new_label()` returns a unique integer. Build label names like `.Lelse_3` and `.Lendif_3` and there's no collision.

## 2. The conditional jump we use most — `je`

`je` (jump if equal) jumps **when ZF (the zero flag) is set**. Placed right after a `cmpl` or `testl`, it jumps "when equal" or "when the result was zero."

```
cmpl $0, %eax       # compare %eax with 0. Equal ⇒ ZF=1
je   .Lzero         # jump to .Lzero if ZF is set
```

These two instructions express "jump if `%eax` is 0."

In our codegen, comparison and condition expressions leave **0 or 1 in `%eax`**. `if (cond)` means "if `cond` is true (nonzero), do the then-part; if false (0), do the else-part." So:

- When it's 0, "jump elsewhere" = `cmpl $0, %eax; je target`

The core of an `if (cond)` translation (evaluate the condition → jump away on false) is expressed in just those two instructions. Then we put a label at the target and lay out the then-body and else-body.

## 3. Translating if (without else)

```c
if (cond)
    body;
```

becomes:

```
   gen_expr(cond)         # eax = value of cond
   cmpl $0, %eax           # compare against 0
   je   .Lendif_N           # skip body if cond is 0
   gen_stmt(body)
.Lendif_N:
```

If `cond` is 0, `je` jumps to `.Lendif_N`, skipping the body. Otherwise `je` falls through, the body runs, and execution naturally reaches `.Lendif_N:`.

The label number `N` is issued just for this `if`. Multiple `if`s in the same function get different numbers, so they don't collide.

## 4. Translating if-else

```c
if (cond)
    then_body;
else
    else_body;
```

Translation:

```
   gen_expr(cond)
   cmpl $0, %eax
   je   .Lelse_N             # jump to else if false
   gen_stmt(then_body)
   jmp  .Lendif_N             # after then, jump to end
.Lelse_N:
   gen_stmt(else_body)
.Lendif_N:
```

After the then-body, we must `jmp .Lendif_N` to **skip the else**. Without it, execution would fall through into the else body.

Two labels: `.Lelse_N` and `.Lendif_N`. Sharing the same number `N` visually pairs the two labels of one `if`.

Implementation:

```c
case NODE_IF: {
    int n = new_label();
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    if (node->else_body) {
        fprintf(out, "  je .Lelse_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, "  jmp .Lendif_%d\n", n);
        fprintf(out, ".Lelse_%d:\n", n);
        gen_stmt(node->else_body);
        fprintf(out, ".Lendif_%d:\n", n);
    } else {
        fprintf(out, "  je .Lendif_%d\n", n);
        gen_stmt(node->then_body);
        fprintf(out, ".Lendif_%d:\n", n);
    }
    return;
}
```

The shape differs depending on whether `else` is present. Without `else`, we skip `.Lelse_N` entirely and jump straight to `.Lendif_N`.

## 5. Translating while

```c
while (cond)
    body;
```

Translation:

```
.Lbegin_N:
   gen_expr(cond)
   cmpl $0, %eax
   je   .Lendwhile_N          # leave if false
   gen_stmt(body)
   jmp  .Lbegin_N             # after the body, go back and re-check
.Lendwhile_N:
```

A loop has three pieces:

1. **`.Lbegin_N`**: top of the loop, before the condition check.
2. **`.Lendwhile_N`**: exit; jumped to when the condition becomes false.
3. **`jmp .Lbegin_N`**: at the end of the body, return to the top.

"Going back up" via an unconditional `jmp` is the essence of a loop. The conditional jump is just the means for "exiting."

Implementation:

```c
case NODE_WHILE: {
    int n = new_label();
    fprintf(out, ".Lbegin_%d:\n", n);
    gen_expr(node->cond);
    fprintf(out, "  cmpl $0, %%eax\n");
    fprintf(out, "  je .Lendwhile_%d\n", n);
    gen_stmt(node->body);
    fprintf(out, "  jmp .Lbegin_%d\n", n);
    fprintf(out, ".Lendwhile_%d:\n", n);
    return;
}
```

## 6. Nesting doesn't break it

Even with an `if` inside a `while`, or a `while` inside an `if`, label numbers are **unique per `new_label()` call**, so there's no collision.

```c
while (i < 10) {
    if (i == 5) {
        x = 100;
    }
    i = i + 1;
}
```

The generated assembly roughly looks like:

```
.Lbegin_0:               # top of while
    ; ... evaluate cond ...
    je .Lendwhile_0
    ; ... evaluate if's cond ...
    cmpl $0, %eax
    je .Lendif_1         # ← new_label() issued 1 for the if
    ; ... x = 100 ...
.Lendif_1:               # ← same 1
    ; ... i = i + 1 ...
    jmp .Lbegin_0
.Lendwhile_0:            # ← while's 0
```

The outer `while` uses `0`, the inner `if` uses `1`. **Each nested structure gets its own label-number space**, so things just work.

## Next

In the last section (`04_build.md`) we lay out the full files and build & run.
