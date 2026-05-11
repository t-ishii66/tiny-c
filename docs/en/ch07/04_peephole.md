# 04 — Peephole optimization

Peephole optimization is a simple post-processing pass: walk the emitted asm top-to-bottom through a **narrow window**, and rewrite wasteful patterns as they appear. In tiny-c the window is either **two adjacent lines** or **the block after a `ret`**.

## 1. Where it sits in the pipeline (memstream recap)

The setup of accumulating assembly output into a memory buffer via `open_memstream` was already introduced in ch07/02 for backpatching. The peephole pass **reuses the same buffer** — after codegen has finished writing the asm, we scan and rewrite it once before flushing to stdout.

```
codegen → memory buffer (open_memstream) → peephole → stdout
                ↑                              ↑
        introduced in ch07/02            added here
```

In `main.c`:

```c
char *buf = NULL;
size_t len = 0;
FILE *mem = open_memstream(&buf, &len);
codegen(program, mem);     /* backpatching is complete by this point */
fclose(mem);
peephole(buf, (int)len, stdout);   /* added this section: line-level rewrites */
```

codegen itself is **unchanged**. We just slot a `peephole` function — which reads the buffer and rewrites it — between codegen and stdout.

## 2. Split into lines

Peephole work is easiest to think about line-by-line. Split the buffer on `\n` into an array:

```c
static char *lines[MAX_LINES];
static int n_lines;

static void split_lines(const char *buf, int len) {
    /* split buf on \n into lines[] */
}
```

Each `lines[i]` is one line (including the trailing `\n`). The peephole works on `lines[]`, replacing positions with `NULL` or with a different string. At the end, only the non-NULL lines get flushed to `stdout`.

## 3. Pattern 1: `pushq %rax; popq %reg` → `movq %rax, %reg`

The most visible waste in tiny-c's emitted asm is at the tail of function-call argument passing:

```asm
movl $4, %eax
pushq %rax
movl $3, %eax
pushq %rax    ← pushing arg 1
popq %rdi     ← popping it right back
popq %rsi
movl $0, %eax
call f
```

The last `pushq %rax` is **immediately** followed by `popq %rdi`. That's "use the stack as a courier from `%rax` to `%rdi`" — which is just `movq %rax, %rdi` directly.

```asm
movl $4, %eax
pushq %rax
movl $3, %eax
movq %rax, %rdi   ← two lines collapse to one
popq %rsi
movl $0, %eax
call f
```

Implementation:

```c
for (int i = 0; i + 1 < n_lines; i++) {
    if (strcmp(lines[i], "  pushq %rax\n") != 0) continue;

    /* pushq %rax; popq REG → movq %rax, REG */
    if (strncmp(lines[i+1], "  popq ", 7) == 0) {
        char reg[16];
        sscanf(lines[i+1] + 7, "%15s", reg);
        free(lines[i]); free(lines[i+1]);
        char *combined = malloc(64);
        snprintf(combined, 64, "  movq %%rax, %s\n", reg);
        lines[i] = combined;
        lines[i+1] = NULL;
        i++;
    }
}
```

"lines[i] is `pushq %rax\n` and lines[i+1] is of the form `popq REG\n`" — we recognize the pattern with strcmp/strncmp, and on a match we replace lines[i] with a `movq` instruction and set lines[i+1] to NULL.

## 4. Pattern 2: `pushq %rax; popq %rax` → erase

Same framing applies — if a push is immediately followed by a pop into the same register, the push/pop pair itself is meaningless, so erase both.

The implementation lines up with pattern 1:

```c
if (strcmp(lines[i+1], "  popq %rax\n") == 0) {
    free(lines[i]);   lines[i]   = NULL;
    free(lines[i+1]); lines[i+1] = NULL;
    i++;
    continue;
}
```

tiny-c's normal codegen doesn't usually produce this pattern, but AST optimization can shrink an expression in a way that exposes it. We keep this case as a safety net.

## 5. Pattern 3: dead code after `ret`

ch05 introduced "**always emit `movl $0, %eax; leave; ret` at the end of `gen_func`**" (a fall-through guard). When a function has an explicit `return`, those tail instructions are unreachable (dead code).

```asm
main:
  ...
  movl $42, %eax    ← from return 42
  leave
  ret               ← function exits here
  movl $0, %eax     ← unreachable
  leave             ← unreachable
  ret               ← unreachable
.globl set          ← next function (label) starts here
set:
  ...
```

Everything from after a `ret` up to the next label is "code that won't execute" — safe to delete.

Implementation:

```c
int dead = 0;
for (int i = 0; i < n_lines; i++) {
    if (!lines[i]) continue;
    if (!dead) {
        if (strcmp(lines[i], "  ret\n") == 0) dead = 1;
        continue;
    }
    /* in dead state */
    if (!is_instr_line(lines[i])) {
        /* hit a label or directive → leave dead state */
        dead = 0;
        continue;
    }
    /* instruction line → erase */
    free(lines[i]);
    lines[i] = NULL;
}
```

"Enter dead state on `ret`, erase instruction lines, exit dead state on a label or directive" — that's all.

`ret` also appears **mid-function**. For instance, if `if (cond) return 1; else return 2;` early-returns in each branch, `ret` shows up before the function's tail:

```asm
  cmpl $0, %eax
  je .Lelse_0
  movl $1, %eax        ← then branch
  leave
  ret                  ← mid-function ret
  jmp .Lendif_0        ← unreachable; erased
.Lelse_0:              ← dead state ends here
  movl $2, %eax
  leave
  ret
.Lendif_0:
  ...
```

The `jmp .Lendif_0` after `ret` is unreachable (the function exits just before), and gets erased as an instruction line in dead state. The next `.Lelse_0:` label ends the dead state — even though linear control flow from the `ret` can't reach it, **labels can be the target of a `jmp` / `je` from elsewhere**, so code after a label has to be treated as "potentially reachable" and kept. That preserves the `else` branch's code. **Both mid-function and end-of-function `ret` are handled by the same rule.**

`is_instr_line` is a helper that decides "**is this line an executable instruction?**":

- Starts with whitespace (indented) → looks like an instruction
- But if the first non-whitespace char is `.`, it's a **directive** (`.text`, `.globl`, `.section` …) — not an instruction
- Doesn't start with whitespace → it's a **label** (`main:`, `.LS0:` …)

If we misclassify a directive or label as an instruction, we'd erase even `.section .rodata`. We classify carefully:

```c
static int is_instr_line(const char *line) {
    if (!line) return 0;
    if (line[0] != ' ' && line[0] != '\t') return 0;  /* label */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '.') return 0;                          /* directive */
    if (*p == '\n' || *p == '\0') return 0;           /* blank line */
    return 1;
}
```

## 6. The limits of peephole

There's plenty of waste this approach can't catch easily. For example:

```asm
leaq -8(%rbp), %rax    ← &x
pushq %rax              ← save addr
movl $5, %eax           ← compute rhs
popq %rcx               ← restore addr to %rcx
movl %eax, (%rcx)       ← *rcx = eax
```

That's the codegen for `int x = 5;`. The `pushq` and `popq` are **not adjacent** (there's a `movl $5, %eax` between them), so a 2-line peephole can't see the pattern. It really should collapse to a single `movl $5, -8(%rbp)`.

Catching this would require:

- Special-casing "**assignment with an immediate rhs**" at the AST level (have codegen emit `movl $C, addr` directly), or
- A more complex peephole with a **3-4 line window** that traces push/pop pairs.

tiny-c's peephole sticks to the two kinds: adjacent 2-line and the block after `ret`.

## 7. Peephole passes are order-independent

The three patterns we implemented **don't interfere with each other**:

- The pushq/popq fusion only rewrites two adjacent lines; it doesn't affect post-`ret` deletion.
- Post-`ret` deletion only erases lines from a given point on; it doesn't affect push/pop fusion.

So whether `peephole_pushpop()` runs before or after `peephole_dead_after_ret()` doesn't change the result. In more complex optimizers, pass order does change the result (→ pass scheduling).

## 8. Next

In the final section (`05_build.md`) we lay out the full diff of `optimize.c` and the `main.c` changes side-by-side, with before/after demos.
