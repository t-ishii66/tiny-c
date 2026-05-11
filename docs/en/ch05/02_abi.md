# 02 — The System V AMD64 ABI

The agreement around calling a function — where to put arguments, where to read the return value, which registers may be clobbered — is called the **calling convention** or **ABI**. On x86-64 Linux, the one in use is the **System V AMD64 ABI**.

> **"Aren't arguments passed on the stack?"**
>
> The 32-bit x86 convention (**cdecl**) had the caller `pushl` all arguments onto the stack — many readers will remember that. But x86-64 (AMD64) changed things: the number of general-purpose registers went from 8 to 16, and the design choice was made to **pass the first few arguments in registers — it's faster**. The System V AMD64 ABI is built on that principle.
>
> tiny-c targets x86-64 Linux, so we follow this ABI. All libc functions, including `printf`, are exposed via the System V ABI, so **using our own scheme would mean we couldn't call libc**. Register-passing instead of stack-passing isn't a choice; it's the default.

## 1. The core of the ABI — register assignment

### Integer arguments

The first **six** integer (or integer-compatible, including pointer) arguments go into these registers, in order:

| Arg # | 64-bit | 32-bit |
|---------|---------|---------|
| 1 | `%rdi` | `%edi` |
| 2 | `%rsi` | `%esi` |
| 3 | `%rdx` | `%edx` |
| 4 | `%rcx` | `%ecx` |
| 5 | `%r8`  | `%r8d` |
| 6 | `%r9`  | `%r9d` |

The 7th argument onward goes on the stack. In tiny-c we cap at **6 arguments**, so stack-passing never comes up.

Calling `add(3, 4)`:

```
movl $3, %edi           # arg 1 = 3
movl $4, %esi           # arg 2 = 4
call add
```

The callee reads `%edi` and `%esi` to get the arguments.

### Return value

Integer return values go in **`%rax`** (or `%eax` for 32-bit values).

Our codegen contract "the expression's value is in `%eax`" was designed **to match this ABI**. For `return expr;`, after `gen_expr(expr)` we just emit `leave; ret`, and the value naturally ends up in the right place (`%eax`).

## 2. caller-saved vs. callee-saved

Registers fall into two categories based on who is responsible for preserving them.

### caller-saved (volatile, saved by the caller)
The callee may **freely clobber** these registers. The caller, if it needs a value preserved, must stash it before `call`.

- `%rax`, `%rcx`, `%rdx`, `%rsi`, `%rdi`, `%r8`, `%r9`, `%r10`, `%r11`

It makes sense that the argument-passing registers (`%rdi` through `%r9`) are all caller-saved — arguments are consumed once, so the callee can trash them.

### callee-saved (non-volatile, saved by the callee)
If the callee uses these, it's responsible for **saving them and restoring them on return**.

- `%rbx`, `%rbp`, `%rsp`, `%r12`, `%r13`, `%r14`, `%r15`

`%rbp` being callee-saved is important. That's why the **first line** of a function's prologue is `pushq %rbp` — we always save the caller's `%rbp` and restore it with `leave`. Without that, the caller's local-variable access would break.

`%rsp` is callee-saved too (though more "naturally restored" than "explicitly saved/restored"). What the prologue subtracted with `subq` gets undone by the epilogue's `leave` (= `movq %rbp, %rsp; popq %rbp`).

### What tiny-c uses

- Arguments: `%rdi` through `%r9` (caller-saved, per spec).
- Return value: `%rax` (caller-saved, per spec).
- Scratch: only `%rax` and `%rcx` (both caller-saved).
- Frame pointer: `%rbp` (callee-saved; saved in prologue).

We **don't use** callee-saved registers (`%rbx`, `%r12`–`%r15`). Using them could optimize codegen (fewer push/pop on the stack), but callee-saved registers require save-and-restore, adding complexity. tiny-c is teaching code; simplicity first.

## 3. The 16-byte stack alignment

This is the ABI rule that demands the most care.

> **Immediately before a `call` instruction, `%rsp` must be 16-byte aligned.**

Why the alignment? After a function's prologue (after `pushq %rbp` but before any `subq`), the callee assumes **`%rsp` is a multiple of 16**. x86-64 has **instructions that assume memory addresses are 16-byte aligned**, and running them on a misaligned stack causes a segmentation fault. The standard library, including `printf`, uses such instructions internally, so violating alignment makes even simple calls crash.

The division of responsibility:

```
Caller's job: before call, align %rsp to a 16-byte boundary
              (the call instruction pushes an 8-byte return address, so the callee enters offset by 8)
Callee opens: pushq %rbp adds another 8 bytes → back to 16-byte alignment
Callee subq:  subtract a multiple of 16 → local area stays 16-aligned
```

Break this, and function calls fail somewhere down the line (even if things seem to work superficially).

## 4. Prologue and epilogue, revisited

The prologue/epilogue we've been writing since ch01 is a literal implementation of the ABI's requirements.

```
Prologue:
  pushq %rbp           # save the callee-saved %rbp
  movq %rsp, %rbp      # new frame base
  subq $N, %rsp        # allocate locals (N must be a multiple of 16)

Epilogue:
  leave                # = movq %rbp, %rsp; popq %rbp
  ret                  # jump to the return address on top of the stack
```

`pushq %rbp` honors the ABI (saving callee-saved). The `subq` amount honors the ABI constraint (multiple of 16). `leave; ret` is the matching inverse.

Through ch04, tiny-c grew the stack one variable at a time with `subq $8, %rsp`. Eight bytes per variable means alignment depended on the variable count — sometimes 16-aligned, sometimes not — i.e., **alignment was not guaranteed**. That still worked through ch04, **because we never made a `call`**. Without a `call`, broken alignment doesn't cause a segfault.

The moment ch05 introduces function calls, **16-alignment suddenly becomes mandatory**. The lazy scheme through ch04 won't do. In the next section (`03_frame.md`) we redesign how we build the frame.

## 5. Variadic functions and `%al`

When calling a variadic function like `printf("%d %d", a, b)`, the ABI imposes an extra rule:

> When calling a function that uses variadic arguments, set `%al` (= the low 8 bits of `%rax`) to **the number of floating-point arguments passed in XMM registers**.

The ABI can't tell at the call site how many of `printf`'s `...` arguments are float/double, so the caller hints at it via `%al`. If we only pass integers, `%al = 0`.

tiny-c doesn't deal with floats, so we **always emit `movl $0, %eax`** right before `call`. That sets `%al = 0`. For non-variadic functions, `%al = 0` is harmless.

```
movl $0, %eax        # %al = 0  (XMM regs used = 0)
call printf
```

That makes `printf` safe to call (we actually run it when string literals appear in ch06). We emit this single line before every tiny-c call — no need to distinguish variadic from non-variadic, which keeps the code simple.

## 6. The big picture of a function call

Translating "follow the ABI" into a recipe:

```
[Caller]
  1. Put arguments in %rdi, %rsi, %rdx, %rcx, %r8, %r9
  2. Check that %rsp is 16-aligned (pad if misaligned)
  3. movl $0, %eax  (variadic-ABI insurance)
  4. call name
  5. The return value is in %rax

[Callee]
  1. pushq %rbp; movq %rsp, %rbp  (establish frame)
  2. subq $N, %rsp  (N a multiple of 16)
  3. Copy %edi, %esi, ... into the corresponding stack slots
  4. Execute the body
  5. Place the return value in %eax
  6. leave; ret
```

That's everything. The next two sections turn each side into implementation.

## Next

In the next section (`03_frame.md`) we replace ch04's lazy frame allocation with **pre-pass allocation**, and guarantee 16-alignment.
