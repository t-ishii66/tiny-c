# 04 — Codegen for function calls

Translate `f(a, b, c)` into assembly. Three things to do:

1. Evaluate the arguments `a, b, c` and put them in `%rdi, %rsi, %rdx`.
2. Align `%rsp` to 16 bytes.
3. `call f`.

Looks simple, but there's a wrinkle in making sure the argument evaluations **don't clobber each other**, and **dynamic alignment** to manage.

## 1. The difficulty with argument evaluation

The ABI's argument-to-register mapping again:

| Arg # | 64-bit | 32-bit |
|---------|---------|---------|
| 1 | `%rdi` | `%edi` |
| 2 | `%rsi` | `%esi` |
| 3 | `%rdx` | `%edx` |
| 4 | `%rcx` | `%ecx` |
| 5 | `%r8`  | `%r8d` |
| 6 | `%r9`  | `%r9d` |

The first thought is "evaluate them in order and put each directly in its register":

```c
gen_expr(args[0]); movl %eax, %edi
gen_expr(args[1]); movl %eax, %esi
gen_expr(args[2]); movl %eax, %edx
```

But this breaks. If `gen_expr(args[1])` contains **another function call**, that call will clobber `%rdi`. `%rdi` is caller-saved, so it could be in use after any call.

```c
f(g(1), 2)
```

Evaluation:
1. Compute `g(1)` → `%eax = g(1)`, `movl %eax, %edi`
2. Compute `2` → `%eax = 2`, `movl %eax, %esi`
3. `call f`

That one's fine. But:

```c
f(2, g(1))
```

Evaluation:
1. Compute `2` → `%eax = 2`, `movl %eax, %edi` (`%rdi = 2`)
2. Compute `g(1)` → during the call to `g`, **`%rdi` gets clobbered**
3. `call f` — but `%rdi` is already destroyed

The arguments don't reach `f` correctly.

## 2. The fix — push everything, then popq

Order the work like this:

```
Evaluate every argument, pushing onto the stack as you go.
Once all are evaluated, pop them into registers.
```

This dodges the "don't touch this register" problem. Once a value is pushed, any subsequent call can clobber `%rdi` to its heart's content — the popq at the end pulls the correct value back.

There's a small wrinkle in the order. The register order is `%rdi (1st), %rsi (2nd), %rdx (3rd), ...`. If we push "left to right," the last pushed (= the topmost on the stack) is the Nth argument. Popping would then take the Nth out into `%rdi` (1st) — wrong.

The fix: **push the arguments in reverse order**. The last push becomes the 1st argument, and popq pulls them off neatly into `%rdi`, `%rsi`, ... in order.

```
push args[N-1]    # push the last one first (it ends up on the bottom)
push args[N-2]
...
push args[1]
push args[0]      # push the first one last (it ends up on top)

popq %rdi         # 1st → %rdi
popq %rsi         # 2nd → %rsi
...
popq arg_regs[N-1]
```

A small recursive function makes the implementation natural:

```c
static int push_args(NodeList *l) {
    if (!l) return 0;
    int n = push_args(l->next);   /* push the tail first */
    gen_expr(l->node);
    emit_push();
    return n + 1;
}
```

We recurse to the end of the NodeList and push on the way back up, so **the last argument is pushed first**. The first argument ends up on top of the stack.

## 3. Tracking alignment — `stack_offset`

Before `call`, `%rsp` must be 16-aligned. But we must track **statically** how far `%rsp` has drifted at any point.

Every argument push moves `%rsp` by 8. Every `pushq %rax` for a binary op moves it too. We need to remember "is the alignment currently even or odd?"

Introduce an integer variable `stack_offset`: **the current `%rsp` difference from the post-prologue state, measured in 8-byte units**.

```c
static int stack_offset;

static void emit_push(void) {
    fprintf(out, "  pushq %%rax\n");
    stack_offset++;
}
static void emit_pop(const char *reg) {
    fprintf(out, "  popq %s\n", reg);
    stack_offset--;
}
```

At function entry, `stack_offset = 0`. `%rsp` is 16-aligned.

- `stack_offset` even → an even number of 8-byte offsets, so `%rsp` is on a multiple of 16 (16-aligned).
- `stack_offset` odd → odd number of 8-byte offsets, so `%rsp` is 8 bytes off a multiple of 16 (8-misaligned).

## 4. Padding at `call`

Before emitting `call`, if `stack_offset` is odd (i.e., `%rsp` is 8-misaligned), `subq $8, %rsp` to drop by 8 and become 16-aligned. After `call`, `addq $8, %rsp` to restore.

```c
case NODE_CALL: {
    int pad = (stack_offset % 2) != 0;
    if (pad) {
        fprintf(out, "  subq $8, %%rsp\n");
        stack_offset++;       /* update the tracker too */
    }

    int n_args = push_args(node->args);
    if (n_args > 6) { /* ... error ... */ }
    for (int i = 0; i < n_args; i++)
        emit_pop(arg_regs64[i]);

    fprintf(out, "  movl $0, %%eax\n");    /* variadic ABI */
    fprintf(out, "  call %s\n", node->name);

    if (pad) {
        fprintf(out, "  addq $8, %%rsp\n");
        stack_offset--;
    }
    return;
}
```

The `arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"}` used here is the **64-bit version** of `arg_regs32[]` (`%edi`, `%esi`, ...) seen on the callee side in section 03. The caller uses `popq` (a 64-bit instruction) to pull values off the stack, hence 64-bit names; the callee uses `movl` (32-bit) to spill `int` parameters into slots, hence 32-bit names — an asymmetry. Why this still adds up is explained in **section 6 (the 32-bit-argument issue and the caller/callee mismatch)**.

Key points here:

- **The `pad` flag**, indicating whether to emit padding, is set based on `stack_offset` at the start of the call.
- Padding is emitted before push_args. Since push_args pairs equal numbers of pushes and pops, it leaves `stack_offset` unchanged (it grows temporarily and comes back).
- At the moment of `call`, `stack_offset` is even after padding → `%rsp` is 16-aligned.

## 5. Does this work for nested calls?

Trace the codegen of `f(1) + f(2)` (a textbook nested-call case). In the comments, `so` is short for `stack_offset` and `PAD` is the alignment fix from section 4 (`subq $8, %rsp` to 16-align). **The PAD check happens only at the moment we enter `CALL`** (specifically, just after `gen_expr` branches into `NODE_CALL`, before any argument is pushed) — in other situations (memory loads, arithmetic, etc.) `%rsp` being temporarily misaligned doesn't matter (we're not crossing a call).

```c
gen_expr(BINARY +, lhs=f(1), rhs=f(2)):
  gen_expr(rhs = CALL f(2)):       # NODE_CALL handling begins
    stack_offset = 0 (even), no pad
    push_args([2]):
      gen_expr(2)                # eax=2
      emit_push                  # so=1
      return 1                   # number of args pushed
    emit_pop %rdi                # so=0
    movl $0, %eax                # variadic ABI
    call f                       # so=0 even → 16-aligned ✓
  emit_push                      # so=1, save f(2)'s result
  gen_expr(lhs = CALL f(1)):     # NODE_CALL handling begins
    (so is still 1 because the previous emit_push saved f(2))
    stack_offset = 1 (odd) at entry to CALL → emit PAD before the upcoming call f
    subq $8, %rsp                # so=2 (PAD: held until the call)
    push_args([1]):
      gen_expr(1)                # eax=1
      emit_push                  # so=3
      return 1                   # number of args pushed
    emit_pop %rdi                # so=2
    movl $0, %eax                # variadic ABI
    call f                       # so=2 even → 16-aligned ✓
    addq $8, %rsp                # so=1 (undo PAD)
  emit_pop %rcx                  # so=0
  addl %ecx, %eax                # eax = f(1) + f(2)
```

The first `call f(2)` enters with `so=0`, naturally 16-aligned. The moment we `pushq` to save `f(2)`'s result, `so` becomes 1; entering the next CALL detects the **odd value and inserts padding**. The actual `call` is 16-aligned, every time.

Just one number, `stack_offset`, lets us align correctly at any depth of nesting.

## 6. The 32-bit-argument issue and the caller/callee mismatch

Looking back, there's an interesting asymmetry:

- The **caller** uses `pushq %rax` / `popq %rdi` (64-bit ops) for arguments.
- The **callee** (the spill in section 03) receives with `movl %edi, -off(%rbp)` (32-bit ops).

Is it OK to pass 64-bit and receive 32-bit? Yes. There's a **crucial x86-64 rule** that makes it line up:

> **Writing to a 32-bit register (`%eax`, `%edi`, etc.) automatically clears the upper 32 bits of the corresponding 64-bit register (`%rax`, `%rdi`, etc.) to zero.**

Tracing what happens when an `int` is passed:

1. caller: `movl $42, %eax` → `%rax = 0x00000000_0000002A` (upper 32 are zero-extended)
2. caller: `pushq %rax` → push 8 bytes (upper 4 = zero, lower 4 = 42)
3. caller: `popq %rdi` → `%rdi = 0x00000000_0000002A`
4. callee: `movl %edi, -8(%rbp)` → reads just `%edi` (low 4 bytes = 42) and stores it in the slot

The callee **only looks at `%edi`**. The upper 32 bits being zero is incidental; per the ABI, "the upper 32 bits of an integer argument are undefined" — whatever the caller put there, the callee ignores. That's why this scheme is correct.

Summary:

| Value | caller | callee spill |
|---|---|---|
| int (4 bytes) | `pushq %rax` (upper 32 zero-extended) → `popq %rdi` | `movl %edi, -off(%rbp)` |
| pointer (8 bytes) | `pushq %rax` → `popq %rdi` | `movq %rdi, -off(%rbp)` |

When pointers arrive in ch06, the callee gets a `movq` branch; in ch05, with only `int`s, `movl` is enough.

## 7. Handling the variadic ABI

`movl $0, %eax` right before `call`. This sets `%al = 0` (XMM registers used = 0), making variadic calls like `printf` safe. It's harmless for non-variadic calls too, so we **always emit it** in tiny-c.

## 8. Why recursive calls "just work"

Consider factorial:

```c
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
```

Calling `fact(5)` recurses into `fact(4)`, then `fact(3)`, and so on, calling itself. Each call has its own local `n` — independent variables. How does that work?

Each call's prologue pushes a new frame and updates `%rbp` to that frame's base. `-8(%rbp)` is relative to the *current* `%rbp`, so it **points to a different memory location per call**.

```
high address
 +-------------+
 | main's rbp  |
 +-------------+
 | rip = main  |
 | saved rbp   |
 | n=5         |   ← fact(5)'s frame
 +-------------+
 | rip = fact  |
 | saved rbp   |
 | n=4         |   ← fact(4)'s frame
 +-------------+
 | rip = fact  |
 | saved rbp   |
 | n=3         |   ← fact(3)'s frame
 +-------------+
 | ...         |
low address
```

The `n` of `fact(3)` and the `n` of `fact(4)` both look like `-8(%rbp)` in assembly, but `%rbp` differs, so they refer to different memory. As long as the prologue/epilogue and `%rbp` follow the ABI, recursion works with no special mechanism.

## Next

In the last section (`05_build.md`) we lay out the complete files and read the generated assembly for a nested call (`f(1) + f(2)`), watching the moment the padding kicks in.
