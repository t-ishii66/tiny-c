# 02 — Stack frame and symbol table

Turn the variables `x` and `y` into **memory addresses** the CPU can read. That's the substance of this chapter's codegen.

Two new tools appear:

- **Stack frame** — the per-function local memory region, addressed relative to `%rbp`.
- **Symbol table** — the name → offset translation table. Used only inside codegen.

## 1. Stack frame, recap

The function prologue we wrote in ch01:

```
pushq %rbp           # save the caller's %rbp on the stack
movq %rsp, %rbp      # copy the current %rsp into %rbp
```

These two lines lay the **foundation of the function's stack frame**. `%rbp` (the base pointer) now anchors **the reference point** of this function's local region.

And the epilogue:

```
leave                # bring rsp back to rbp and pop the saved rbp
ret                  # jump to the return address
```

`leave` is equivalent to `movq %rbp, %rsp; popq %rbp`. After it, `%rsp` is back to its state right after the prologue and `%rbp` is back to the caller's value. **The stack region this function used is cleanly freed.**

In ch01 and ch02 we added nothing in between — there were no local variables. In ch03 we put **room for local variables** between the prologue and the first `leave`.

## 2. Where local variables live

The stack grows downward (toward lower addresses). Right after the prologue, `%rbp` points to the location where the caller's `%rbp` was saved.

```
            high address
           +---------+
           |  ...    |
           +---------+
           | ret addr|   pushed by call (%rsp points here right after call)
           +---------+
   %rbp →  | old %rbp|   pushed by pushq %rbp (then movq %rsp, %rbp makes %rbp point here)
           +---------+
   %rsp →  |         |   (empty stack)
           |         |
           | ...     |
           low address
```

Local variables go **below** this `%rbp`. We grow downward in 8-byte steps: `-8(%rbp)`, `-16(%rbp)`, `-24(%rbp)`, ...

```
   %rbp →    | old %rbp|
             +---------+
   -8(%rbp)  |    x    |     int x  (first)
             +---------+
   -16(%rbp) |    y    |     int y  (second)
             +---------+
   -24(%rbp) |    z    |     int z  (third)
             +---------+
   %rsp →    |         |
```

Each variable is always accessible via its **negative offset** from `%rbp`. Even if `%rsp` moves (because we `pushq` an intermediate value), `%rbp` doesn't, so the variable's location is unchanged. That's the reason we keep `%rbp` separate.

### Why 8 bytes each?

An `int` is only 4 bytes. Why allocate 8 bytes per variable?

The simple reason: **the stack's natural unit is 8 bytes**. On x86-64, `pushq` and `popq` operate on 8 bytes, and the stack pointer naturally moves in 8-byte multiples. Making each local 8 bytes means a single `subq $8` expands the region by one slot, and we get clean offsets `-8`, `-16`, `-24`, ...

Production compilers manage alignment and size carefully per type, but tiny-c takes the shortcut: "everything is 8 bytes."

## 3. The symbol table

"When I see `x` written, that means `-8(%rbp)`." We need a structure that remembers that mapping — the symbol table.

A minimal implementation:

```c
typedef struct LVar LVar;
struct LVar {
    char *name;
    int offset;     /* positive; used as -offset(%rbp) */
    LVar *next;
};

static LVar *locals;       /* head of the linked list */
static int frame_size;     /* total bytes allocated so far */
```

A singly-linked list of `(name, offset)` pairs.

Function to add a new variable:

```c
static int add_local(char *name) {
    for (LVar *v = locals; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name;
    frame_size += 8;
    v->offset = frame_size;
    v->next = locals;
    locals = v;
    return v->offset;
}
```

The offset is "running total size so far." The first variable gets 8, the second 16, and so on. Those become `-8(%rbp)`, `-16(%rbp)` in the emitted code.

Lookup:

```c
static int find_local(char *name) {
    for (LVar *v = locals; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v->offset;
    fprintf(stderr, "undeclared variable: %s\n", name);
    exit(1);
}
```

Walk the list, return the offset if found, error otherwise.

The symbol table is a static device used **only inside `codegen.c`**. The AST and `parser.y` stay at the name level; address resolution doesn't touch them. `tinyc` gets away with "one flat table per function" — when `if`/`while` arrive in ch04 and global variables arrive in ch06, the local mechanism stays the same. In ch07, when we introduce block scope, we extend this by saving and restoring the head of the `locals` list.

## 4. Codegen for NODE_VAR_DECL

How to compile `int x = 1;`:

```c
case NODE_VAR_DECL: {
    int off = add_local(node->name);          /* register the name, get an offset */
    fprintf(out, "  subq $8, %%rsp\n");       /* widen the stack by 8 bytes */
    gen_expr(node->expr);                     /* evaluate the initializer → eax */
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);  /* store eax */
    return;
}
```

Three (well, four) steps:

1. `add_local` to register the name and obtain the offset.
2. `subq $8, %rsp` to lower `%rsp` by 8 (see below for why).
3. Evaluate the initializer (`node->expr`) with `gen_expr`. The result lands in `%eax`.
4. `movl %eax, -off(%rbp)` to write that value to the assigned location.

The `subq $8, %rsp` matters. Why? Right after the prologue, `%rsp` is at the same location as `%rbp`. If we wrote `x` into `-8(%rbp)` and then issued a `pushq`, `%rsp` would drop 8 bytes — landing exactly on `-8(%rbp)` — and the pushed data would overwrite `x`'s slot.

`subq $8, %rsp` **moves `%rsp` below `x`'s slot**. Subsequent `pushq`s now go to `-16(%rbp)` or lower, so `x`'s region is safe. The `subq` is `%rsp`'s way of saying "above this is variable territory; below is scratch."

## 5. Codegen for NODE_IDENT

For example, reading `x` while computing the right-hand side of `y = x + 1;`. One instruction does it.

```c
case NODE_IDENT: {
    int off = find_local(node->name);
    fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);
    return;
}
```

Resolve the name with `find_local`, load from there into `%eax`. The contract — **"the expression's value is in `%eax`"** — is preserved.

## 6. Codegen for NODE_ASSIGN

Continuing with `y = x + 1;`: the part that writes the result of `x + 1` into `y`. Assignment is "compute the rhs, then write to the lhs's location."

```c
case NODE_ASSIGN: {
    if (node->lhs->kind != NODE_IDENT) {
        fprintf(stderr, "lhs of '=' must be a variable\n");
        exit(1);
    }
    int off = find_local(node->lhs->name);
    gen_expr(node->rhs);                      /* eax = the rhs's value */
    fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);
    return;
}
```

This is where we lvalue-check: if `node->lhs` isn't `NODE_IDENT`, it's an error.

The order is **evaluate rhs first**. Assignment evaluates the right of `=`, then writes the left — straight C semantics. After `gen_expr(node->rhs)`, `%eax` holds the value to be written. `movl %eax, -off(%rbp)` writes it to the destination.

The **value of an assignment expression** is **the value written**. In a chain like `a = (b = 7)`, the inner assignment's result (7) is the outer rhs. In our code, the `movl` doesn't change `%eax`, so **the value we wrote remains in `%eax`** — the assignment expression's own value also lives in `%eax`.

## 7. Codegen for NODE_EXPR_STMT

The `ASSIGN` above is an **expression** node: it computes a value, which ends up in `%eax`. `EXPR_STMT`, on the other hand, is a **statement** node — its role is to run the expression but throw away the result. An assignment terminated by `;`, like `y = x + 1;`, becomes a two-layer AST with `EXPR_STMT` wrapping `ASSIGN` — "the computing" is `ASSIGN`, "throwing away the value and moving on" is `EXPR_STMT`.

`NODE_EXPR_STMT` is the node for **"an expression placed as a statement"**. For example:

```c
x = x + 5;
```

This is an "expression" in C (the assignment `x = x + 5`); adding `;` makes it a "statement." The AST looks like:

```
EXPR_STMT
└─ ASSIGN
   ├─ IDENT x
   └─ BINARY +
      ├─ IDENT x
      └─ INT_LIT 5
```

Codegen just evaluates the inner expression:

```c
case NODE_EXPR_STMT:
    gen_expr(node->expr);
    return;
```

Inside `gen_expr`, `%eax` is used freely — compute `x + 5`, hold it in `%eax`, store it into `x`'s slot at `-off(%rbp)`. **The side effect (the memory write) does happen.**

What we "throw away" is the expression's **return value**. The assignment expression `x = x + 5` has a value (the value written), and that value sits in `%eax` after `gen_expr` finishes. But `EXPR_STMT`'s codegen hands `%eax` to no one. When the next statement starts, its first operation overwrites `%eax`.

In other words:

- **`return x = x + 5;`** — the assignment expression is the operand of `return`, so the value in `%eax` is used as the return value. Not discarded.
- **`x = x + 5;`** — the assignment stands as a statement; computation and assignment happen, but no one reads the resulting `%eax`, and the next statement overwrites it. Discarded.

`NODE_EXPR_STMT` is the node for the latter pattern.

## 8. A look at chained assignment

Trace the codegen for `a = b = 7`.

AST:

```
ASSIGN
├─ IDENT a
└─ ASSIGN
   ├─ IDENT b
   └─ INT_LIT 7
```

Codegen for the outer `ASSIGN`:

Let `off_a` be `a`'s offset (returned by `find_local("a")`) and `off_b` be `b`'s.

1. Lvalue check on `lhs` (IDENT a): OK. Grab `off_a`.
2. `gen_expr(node->rhs)` — evaluate the inner `ASSIGN`.
   - Lvalue check on `lhs` (IDENT b): OK. Grab `off_b`.
   - `gen_expr(INT_LIT 7)` → `movl $7, %eax`
   - `movl %eax, -off_b(%rbp)` → write 7 into b. `%eax` is still 7.
3. `movl %eax, -off_a(%rbp)` → write 7 into a. `%eax` is still 7.

That's exactly the meaning of `a = (b = 7)`. Both get 7.

When you feed it to the compiler, the assembly looks like:

```
movl $7, %eax              # compute 7
movl %eax, -16(%rbp)       # b = 7  (%eax still 7)
movl %eax, -8(%rbp)        # a = 7
```

Three instructions for the chain. This is what right-associativity buys us — left-associative would have given `(a = b) = 7`, which breaks both grammar and code.

## 9. The whole codegen.c (gen_expr / gen_stmt excerpts)

```c
static void gen_expr(Node *node) {
    switch (node->kind) {
    case NODE_INT_LIT: { /* same as ch02 */ }
    case NODE_UNARY:   { /* same as ch02 */ }
    case NODE_BINARY:  { /* same as ch02 */ }
    case NODE_IDENT: {                                         /* added */
        int off = find_local(node->name);                      /* added */
        fprintf(out, "  movl -%d(%%rbp), %%eax\n", off);       /* added */
        return;                                                /* added */
    }                                                          /* added */
    case NODE_ASSIGN: {                                        /* added */
        if (node->lhs->kind != NODE_IDENT) { ... error ... }   /* added */
        int off = find_local(node->lhs->name);                 /* added */
        gen_expr(node->rhs);                                   /* added */
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);       /* added */
        return;                                                /* added */
    }                                                          /* added */
    ...
    }
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case NODE_RETURN: { /* same as ch01 */ }
    case NODE_BLOCK:  { /* same as ch01 */ }
    case NODE_VAR_DECL: {                                      /* added */
        int off = add_local(node->name);                       /* added */
        fprintf(out, "  subq $8, %%rsp\n");                    /* added */
        gen_expr(node->expr);                                  /* added */
        fprintf(out, "  movl %%eax, -%d(%%rbp)\n", off);       /* added */
        return;                                                /* added */
    }                                                          /* added */
    case NODE_EXPR_STMT:                                       /* added */
        gen_expr(node->expr);                                  /* added */
        return;                                                /* added */
    ...
    }
}
```

## 10. lvalue / rvalue

- **rvalue** = the **value** of an expression. What ends up in `%eax`. The result of `gen_expr`.
- **lvalue** = a **memory location**. The `x` in `x = 5`. An address like `-8(%rbp)`.

In ch03, the only lvalue is a variable, so `find_local(name)` is enough to get the offset. In ch06, with `*p`, `a[i]`, and `&x`, a dedicated `gen_addr` function appears for computing lvalues. `gen_expr(IDENT)` is a load, `NODE_ASSIGN` is a store — symmetric operations.

## Next

In the last section (`03_build.md`) we lay out the full files, build, and run.
