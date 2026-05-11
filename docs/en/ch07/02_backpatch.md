# 02 — Eliminating Phase 1 with backpatching

The ch06 codegen was structured as **Phase 1 (collect) → Phase 2 (emit)**. The sole reason for splitting Phase 1 was that **`subq $N, %rsp` in the prologue couldn't be written without knowing `frame_size`** — since you can't know N until you've walked the function body, you needed a pre-walk to determine N before emitting the body.

ch07 removes that constraint with **backpatching**: "write a placeholder where N isn't known yet, then write back later." Codegen becomes **single-pass**.

Optimization (the AST folding in section 01; peephole in section 04) is a separate topic, but the **memory buffer** introduced in ch07 enables backpatching — well worth covering here.

## 1. What was the problem

ch06's `gen_func` ran in this order:

```c
gen_func(fn) {
    /* Phase 1: walk every VAR_DECL and finalize frame_size */
    for params: add_local(...);
    collect_locals(fn->body);    /* walk the AST only; emit no code */

    /* Phase 2: now that frame_size is known, write the prologue */
    fprintf("subq $%d, %%rsp", aligned(frame_size));
    /* body codegen (locals are already built by Phase 1) */
    gen_stmt(fn->body);
    ...
}
```

Two AST walks. The first calculates frame size only; the second actually emits assembly.

Why two? **Because we wrote with `fprintf` immediately.** Once `subq $N` was emitted with a value, there was no way to go back and rewrite it later. That made Phase 1 necessary.

## 2. With a memory buffer, we can rewrite

In ch07 we accumulate assembly output in a **memory buffer** (`open_memstream`). This was introduced for peephole optimization (section 04), but a side effect is that **`ftell` and `fseek` work on the buffer** — we can return to a written position and overwrite it.

```c
/* main.c */
char *buf = NULL;
size_t len = 0;
FILE *mem = open_memstream(&buf, &len);
codegen(program, mem);     /* write into mem */
fclose(mem);                /* at this point buf holds all the assembly */

if (no_opt)
    fputs(buf, stdout);
else
    peephole(buf, len, stdout);
```

`mem` has the `FILE *` interface but is backed by memory. `ftell(mem)` returns the current write position (byte offset); `fseek(mem, pos, SEEK_SET)` jumps back. **`stdout` is not seekable**, but memstream is.

In ch07 we **use the buffer even with `--no-opt`** (we just `fputs` it). Backpatching is needed regardless of optimization, so the buffer itself is always required.

## 3. Backpatching the prologue

`gen_func` changes to:

```c
#define SUBQ_FRAME_WIDTH 10   /* fixed digit count for the placeholder */

static void gen_func(Node *fn) {
    locals = NULL; frame_size = 0;

    fprintf(out, "  .globl %s\n", fn->name);
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");

    /* ★ Write the prologue's subq as a placeholder; remember the position */
    long subq_pos = ftell(out);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, 0);   /* 0 is a placeholder */

    /* params: add_local + spill */
    for params: { LVar *v = add_local(...); /* emit the spill instruction */ }

    /* emit the body (frame_size grows here) */
    gen_stmt(fn->body);

    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");

    /* ★ frame_size is now final. Seek back to the prologue and overwrite */
    int aligned_frame = (frame_size + 15) & ~15;
    long here = ftell(out);
    fseek(out, subq_pos, SEEK_SET);
    fprintf(out, "  subq $%-*d, %%rsp\n", SUBQ_FRAME_WIDTH, aligned_frame);
    fseek(out, here, SEEK_SET);
}
```

Key points:

- **The placeholder is fixed width.** The `*` in `%-*d` becomes `SUBQ_FRAME_WIDTH = 10`, giving "left-aligned, 10 digits, padded with spaces" — something like `subq $0         , %rsp` (number + space padding).
- **The write-back uses the same fixed width.** If the byte length changed, we'd clobber the following code; **keeping the length constant is mandatory**. 10 digits can represent frame sizes up to ~10 GB, plenty for tiny-c.
- Assemblers (`as` / GAS / LLVM as) accept **extra whitespace inside operands** like `subq $16        , %rsp` — the spaces before `,` are not part of the number; they're just whitespace and ignored.

## 4. Codegen becomes single-pass

With Phase 1 gone, `gen_func` allocates LVars during body generation. `NODE_VAR_DECL` handling:

```c
case NODE_VAR_DECL: {
    /* single pass: allocate the LVar here, making the name visible, then emit init code */
    LVar *v = add_local(node->name, node->type);
    if (!node->expr) return;
    fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
    emit_push();
    gen_expr(node->expr);
    emit_pop("%rcx");
    int sz = v->type->is_pointer ? 8 : v->type->base_size;
    emit_store(sz);
    return;
}
```

In ch06, Phase 1 (`collect_locals`) walked every VAR_DECL and called `add_local` to build the locals table, then Phase 2 emitted the body. **In ch07, Phase 1 disappears** — we just call `add_local` the moment we encounter a VAR_DECL during body generation.

`NODE_BLOCK` needs no special handling — just emit the statements in order:

```c
case NODE_BLOCK:
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    return;
```

## 5. ch06 vs ch07

| Item | ch06 (two-phase) | ch07 (single-pass + backpatch) |
|------|-----------|---------------------------|
| AST walk count | 2 (collect_locals + gen_stmt) | **1** (gen_stmt only) |
| Output destination | `stdout` directly | via `open_memstream` buffer |
| Prologue's `subq $N` | finalize N first, then write | write a placeholder, fill in N later |
| Where `add_local` is called | Phase 1 (inside `collect_locals`) | during body generation (inside `NODE_VAR_DECL`) |

## 6. Next

In the next section (`03_scope.md`) we put **block scope** on top of the single-pass codegen. `{ int x=1; }{ int x=2; }` (same name in different blocks) becomes legal. The two-phase design would have made keeping scope state in sync between phases complex; with single-pass, scope just rides on the normal codegen flow.
