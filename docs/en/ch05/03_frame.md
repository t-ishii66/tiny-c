# 03 — Redesigning the stack frame

To meet the ABI's alignment rule, we replace ch04's lazy frame allocation with a **pre-pass**.

## 1. The ch04 scheme and its problem

In ch03 and ch04, we emitted `subq $8, %rsp` inline for each variable declaration.

```c
int x = 1;     /* subq $8, %rsp; movl $1, %eax; movl %eax, -8(%rbp) */
int y = 2;     /* subq $8, %rsp; movl $2, %eax; movl %eax, -16(%rbp) */
```

That moves `%rsp` 8 bytes per variable. One variable: 8-misaligned. Two: 16-aligned. Three: 8-misaligned again — alignment shifts back and forth.

That was fine through ch04. Why? **We never made a `call`.** Without `call`, the 16-aligned constraint on `%rsp` doesn't bite.

ch05 introduces `call`. Before every `call`, `%rsp` must be 16-aligned. **We can't tell in advance when a call will appear** (mid-declaration, inside nested ifs, during recursion, etc.). The lazy scheme is out.

## 2. The fix — pre-pass allocation

Survey the whole function: count **how many locals and parameters it has**, and allocate the total once with `subq`.

```
Prologue:
  pushq %rbp
  movq %rsp, %rbp
  subq $aligned_frame_size, %rsp     # ← multiple of 16!
```

`aligned_frame_size` is the total variable size rounded up to a multiple of 16. With this:

- After `pushq %rbp`: 16-aligned
- After `subq $aligned_frame_size, %rsp`: 16-aligned (we subtracted a multiple of 16)
- Right after entering the body: 16-aligned ✓

Each `var_decl` **no longer emits a `subq`** — the location is already allocated. It just evaluates the initializer and writes the value into the variable's slot via `movl` (e.g., `int x = 5;` becomes `movl $5, %eax; movl %eax, -8(%rbp)`).

## 3. Two-phase codegen — use a pre-pass to learn the structure

To pre-allocate, we need to know "how many variables" before entering the body. We can find out by **walking the AST once**.

In implementation, we split it into two phases.

### Phase 1: collect names (`collect_locals`)

Register the function's parameters and every `var_decl` in the body in the symbol table. **Emit no code.** Just assign each variable an offset.

```c
static void collect_locals(Node *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_VAR_DECL:
        add_local(node->name);    /* register in the symbol table, assign offset */
        break;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            collect_locals(l->node);
        break;
    case NODE_IF:
        collect_locals(node->then_body);
        collect_locals(node->else_body);
        break;
    case NODE_WHILE:
        collect_locals(node->body);
        break;
    default:
        break;     /* expressions etc. don't contain local declarations */
    }
}
```

We recurse into the children of `NODE_BLOCK`, `NODE_IF`, and `NODE_WHILE`. Every `var_decl` in the function — **wherever it sits** — gets collected.

`add_local` itself is the ch03 function: append an entry to the symbol-table linked list. As it does so, it **bumps the global `frame_size` by 8**, so by the end of the walk `frame_size` is the total local-region size for this function.

### Phase 2: emit code (`gen_func`)

With the symbol table and frame size ready, emit the prologue and the body.

```c
static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    stack_offset = 0;

    /* Phase 1: register parameters and variables */
    for (NodeList *l = fn->params; l; l = l->next)
        add_local(l->node->name);
    collect_locals(fn->body);

    /* round up to a multiple of 16 */
    int aligned = (frame_size + 15) & ~15;

    /* Phase 2: prologue */
    fprintf(out, "%s:\n", fn->name);
    fprintf(out, "  pushq %%rbp\n");
    fprintf(out, "  movq %%rsp, %%rbp\n");
    if (aligned > 0)
        fprintf(out, "  subq $%d, %%rsp\n", aligned);

    /* spill parameters into stack slots */
    int i = 0;
    for (NodeList *l = fn->params; l; l = l->next) {
        int off = find_local(l->node->name);
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);
        i++;
    }

    /* Phase 3: body */
    gen_stmt(fn->body);

    /* implicit return 0 (in case of fall-through) */
    fprintf(out, "  movl $0, %%eax\n");
    fprintf(out, "  leave\n");
    fprintf(out, "  ret\n");
}
```

The rounding expression `(frame_size + 15) & ~15` is the standard trick for rounding to a multiple of 16. `& ~15` zeros the low 4 bits.

Notes:

- `arg_regs32` is an array of the 32-bit argument-passing registers: `{"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"}` (definition in the full `codegen.c` shown in `05_build.md`). For the i-th argument, look it up as `arg_regs32[i]`.
- The symbol-table linked list `locals` is never explicitly freed. The `locals = NULL` at the top of `gen_func` simply drops the reference to the previous function's table; the underlying nodes are reclaimed by the OS at process exit. The stack region the generated code uses is automatically freed by `leave` in the epilogue.

### Codegen for NODE_VAR_DECL

With pre-pass allocation, the `var_decl` handler **only writes the value**. No `subq`.

```c
case NODE_VAR_DECL: {
    int off = find_local(node->name);   /* changed: ch03 used add_local + subq $8 */
    gen_expr(node->expr);
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
```

We use `find_local`, not `add_local`. Phase 1 already registered every variable, so **redeclaration can't happen by accident**, and the local region was **already allocated** by the prologue's `subq`.

## 4. Parameter spill

Argument-passing registers carry the values **`%rdi, %rsi, ...`** the instant the function body begins. But these are caller-saved — calling another function inside the body **destroys** them.

So, at the start of the function, we **copy parameter values into stack slots**. This is called **spilling**.

```
movl %edi, -8(%rbp)      # save the 1st argument into the slot at -8(%rbp)
movl %esi, -16(%rbp)     # save the 2nd into -16(%rbp)
...
```

From there on, the function body accesses parameters **just like local variables** — from `-8(%rbp)`, etc. We never worry about `%edi` and friends. Even if a recursive call clobbers them, our slot copies are safe.

In the symbol table, parameters are registered as ordinary `LVar`s. At codegen time, seeing `IDENT a` triggers `find_local("a")` → offset (say `8`) → `movl -8(%rbp), %eax` — exactly the same as any local variable.

## 5. Frame layout

The frame layout for a function like `int fact(int n) { ... uses other variables and calls ... }`:

```
high address
 +---------------+
 | return addr   |  ← pushed by the caller's call
 +---------------+
 | old %rbp      |  ← pushed by our pushq %rbp; movq %rsp, %rbp makes %rbp point here
 +---------------+
 | n (= %edi)    |  -8(%rbp)   ← parameter spill
 +---------------+
 | (unused)      |  -16(%rbp)  ← padding for 16-alignment; subq $16 lands %rsp here
 +---------------+
 |               |
 | (grows and   |
 |  shrinks via  |
 |  push/pop)    |
 |               |
low address
```

`fact`'s frame has just `n`, 8 bytes; we round up to a multiple of 16 (one slot stays unused for alignment) and allocate 16 bytes.

A function with zero locals and zero parameters (e.g. `int main() { return fact(5); }`) has `frame_size = 0` and `aligned = 0`, i.e., **no `subq` emitted at all**. The prologue is just `pushq %rbp; movq %rsp, %rbp`.

## 6. The limits of "don't reuse variables"

This pre-allocation scheme still uses ch03's flat symbol table, so at ch05, redeclaring `int y = ...;` in a different block is an error. **ch07 introduces block scope**, where the same name can be declared in different blocks (we save `locals`' head on scope entry and roll it back on exit; sibling blocks reuse stack slots because `frame_size` is also saved/restored).

## Next

In the next section (`04_call.md`) we look at the **caller's** codegen. Set up arguments, keep alignment, `call` — introducing the alignment-tracking variable `stack_offset`.
