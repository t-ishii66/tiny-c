# 02 — lvalue and rvalue — the world of `gen_addr`

The left and right sides of `x = 5`, the left and right of `*p = 10`, and `a[i]` — what lets us treat all of these uniformly is the **lvalue / rvalue** distinction. ch06's codegen computes these two with separate functions.

## 1. Recap: rvalue was the world through ch05

As introduced in ch06/00, the essence of the distinction is **"does it have an address?"**:

- **rvalue** — just a value with no address (`42`, `x + 1`, `f(2)`, ...)
- **lvalue** — a location that has an address (`x`, `*p`, `a[i]`, ...)

Through ch05, `gen_expr`'s contract was "place **the expression's value** in `%eax` (or `%rax`)." Whether it was `x + 1`, `f(2)`, or `42`, the value was sitting in `%eax` afterward — that's the rvalue (just a value, no address involvement).

ch03 had one lvalue-flavored move: in `NODE_ASSIGN` for `IDENT`, look up the offset with `find_local(name)` and write with `movl %eax, -off(%rbp)`. That was a special case **specific to "lvalues that are variables."**

In ch06, **places that have addresses** (= lvalues) multiply:

- `x` (the address of a variable's slot, since ch03)
- `*p` (the memory p points to, new)
- `a[i]` (the address of the i-th array element, new)

We want to handle all of these with **"a function that computes an address."** That's **`gen_addr`**.

## 2. When is an lvalue needed — only 2 places in tiny-c

ch06's codegen is made of two functions:

```c
/* gen_expr: place the expression's VALUE in %eax/%rax (rvalue) */
static void gen_expr(Node *node);

/* gen_addr: place the lvalue's ADDRESS in %rax */
static void gen_addr(Node *node);
```

**"Address required" happens in tiny-c only in these 2 places** (i.e., contexts that demand an lvalue):

1. **The left side of `NODE_ASSIGN`** — we need the address of the destination.
2. **The operand of `&`** — we want the address itself as the value.

Everywhere else (`return x;`, `y = x + 1;`, `x` inside `f(x)`, ...) is an rvalue context — only the value matters; we don't care where the address is.

`gen_stmt` always calls `gen_expr`; it never calls `gen_addr` directly.

## 3. lvalue / rvalue table — note the difference between `int x` and `char *p`

When pointers and arrays enter, **the number of "places with addresses" goes up**. Compare a type like `int x` (one location) with a type like `char *p` (two locations: the pointer variable itself, and the place it points to). The difference shows up as more lvalue/rvalue combinations.

### For `int x;` — one place with an address

| Expression | lvalue (address) | rvalue (value) |
|------|------------------|-------------|
| `x` | `&x` (address of x's slot) | the int loaded from x's slot |

### For `char *p;` — two places with addresses

A pointer variable `p` involves **two locations with addresses**:
- **`p`'s own slot** — the 8-byte region allocated for the pointer variable. Its address is `&p`.
- **The place `p` points to** — wherever the address stored in `p` points. Its address *is* `p`'s value.

Each has its own lvalue / rvalue:

| Expression | lvalue (address) | rvalue (value) | Examples |
|------|------------------|-------------|----|
| `p` | `&p` (address of p's slot) | the address value loaded from p's slot | lvalue: `p = some_addr;` / rvalue: `q = p;` |
| `*p` | `p`'s rvalue itself (= the pointee address) | the char loaded from that address | lvalue: `*p = 'x';` / rvalue: `c = *p;` |

Key insight: **`*p`'s lvalue equals `p`'s rvalue**. That's the meat of "`&` and `*` are symmetric" — `*` adds one load; `&` removes one.

### For `int a[5];` — the special rule for arrays (decay)

> **decay (the implicit array → pointer conversion)**: when you use an array name in an expression **without a subscript**, C automatically treats it as "the address of the first element" (= a pointer value). For `int a[5]; int *p = a;`, the `a` becomes `&a[0]` and is assigned to `p`. An array itself has no "value as an rvalue"; in rvalue context, it morphs into an address — that's what decay is.

| Expression | lvalue (address) | rvalue (value) |
|------|------------------|-------------|
| `a` | `&a` = address of the array's start | the rvalue **decays into the same address** (= equal to the lvalue) |
| `a[i]` | base + i × elem_size | the value loaded from that address |

### Rule summary

- **`gen_addr` handles the address computation**, and **`gen_expr` is "gen_addr plus a load."**
- A pointer variable involves **two layers** — `p` and `*p`.
- An array name's rvalue is identical to its lvalue (decay) — that's C's quirk.

In the remaining sections we first look at the **lvalue side** (how to get the address of each form, section 4), then the **users of lvalues** (section 5), and finally the **rvalue side** (section 6).

## 4. The lvalue side — getting an address (`gen_addr`)

`gen_addr`'s contract: **place the lvalue's address in `%rax`**. Three cases — `IDENT`, `*`, `a[i]` — all honor that contract and return.

```c
static void gen_addr(Node *node) {
    switch (node->kind) {
    case NODE_IDENT: { ... }                  /* 4.1 */
    case NODE_UNARY: if (node->op == '*') ... /* 4.2 */
    case NODE_INDEX: { ... }                  /* 4.3 */
    default:
        fprintf(stderr, "not an lvalue\n");
        exit(1);
    }
}
```

Falling into `default` means "tried to take the address of a non-lvalue node" — error.

### 4.1 Address of `IDENT` (variables like `x`, `p`, `a`)

```c
/* inside gen_addr's switch — lvalue side, address into %rax */
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->is_global)
        fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);    /* %rax = global address */
    else
        fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset); /* %rax = local address */
    return;
}
```

`int x`, `int *p`, and `int a[5]` are all handled as IDENT with **the same single instruction**.

Note that tiny-c **allocates every variable in 8-byte units on the stack** (to keep offset math simple; `add_local` rounds up via `round_up_8`). If the type size isn't a multiple of 8, padding extends it:

| Declaration | Type size | Extended to 8-byte units | Result of `gen_addr(IDENT)` (`%rax`) |
|------|----------|--------------------|----------------------------------|
| `int x` | 4 | 8 | start address of x's slot |
| `int *p` | 8 | 8 | start address of p's slot (where the pointer itself lives) |
| `int a[5]` | 20 | 24 | the first byte of the array (= `&a[0]`) |

Locals use `-off(%rbp)`; globals use `name(%rip)` (PC-relative addressing).

**The trick for arrays**: `int a[5]` has type size 20, extended to 24 bytes. At registration time, `add_local` bumps `frame_size` by 24 and sets `v->offset` to "the distance between the array's first byte and `%rbp`." So `leaq -off(%rbp), %rax` alone gives `&a[0]`. No `a + i * elem_size` math here — that's `NODE_INDEX`'s job for `a[i]` (section 4.3).

### 4.2 Address of `*p`

```c
/* inside gen_addr's switch — lvalue side, *p's address into %rax */
case NODE_UNARY:
    if (node->op == '*') {
        /* *p's address is "p's value (the address it points to)" itself.
           Call gen_expr(p) and %rax holds p's value, which is exactly *p's address.
           This makes `*p = 10;`'s left-side evaluation and `&(*p)` go through the same path. */
        gen_expr(node->operand);
        return;
    }
    break;
```

If you want the **address** of `*p`, just take the **value** of `p` — because the value of `p` *is* that address (the section-3 table noted "`*p`'s lvalue equals `p`'s rvalue").

### 4.3 Address of `a[i]` (the `[]` form — works for arrays and pointers)

What we handle here is the AST's `NODE_INDEX`, i.e., when the **`[]` form** is used. Whether `lhs` is the **array name `a`** or the **pointer `p`**, the AST node is the same `NODE_INDEX`, and it lands in this 4.3:

- `a[i]` → `NODE_INDEX(a, i)` → 4.3
- `p[i]` → `NODE_INDEX(p, i)` → 4.3 (lhs-is-pointer branch)
- `*(p + i)` → `NODE_UNARY * → NODE_BINARY +` → **doesn't come here** (covered below)

The `if/else` in the code below splits on "is the lhs an array or a pointer." For an array name, `gen_addr` gives the array's address; for a pointer, `gen_expr` gives the pointer's value (= the pointee address). Either way, we end up with a "base address."

```c
/* inside gen_addr's switch — lvalue side, a[i]'s address into %rax */
case NODE_INDEX: {
    Type *t = expr_type(node->lhs);
    int es = t->base_size;                     /* element size */
    /* get the base address */
    if (lhs is array IDENT)
        gen_addr(node->lhs);                   /* array: its address */
    else
        gen_expr(node->lhs);                   /* pointer: its value (the address) */
    emit_push();                               /* save the base */
    gen_expr(node->rhs);                       /* %eax = i */
    fprintf(out, "  movslq %%eax, %%rax\n");   /* sign-extend to 64-bit */
    fprintf(out, "  imulq $%d, %%rax\n", es);  /* %rax = i * elem_size */
    emit_pop("%rcx");                          /* %rcx = base */
    fprintf(out, "  addq %%rcx, %%rax\n");     /* %rax = base + i*elem */
    return;
}
```

Here `i` is **any expression** — integer literal (`a[5]`), variable (`a[n]`), an expression (`a[j+1]`), a function call (`a[f()]`), anything. `gen_expr(node->rhs)` evaluates `i` at runtime into `%eax`, so the compiler doesn't need to know `i` at compile time.

`lhs` may be **an array name or a pointer**. The `Type` struct (in `ast.h`) has a `base_size` field that holds either "the size of the type a pointer points to" or "the array element size," so we use the same `t->base_size` for both:

- Array name `a` (type `int[5]`) — `base_size` = 4 (the `int` element size). The base is `&a[0]`, obtained via `gen_addr`.
- Pointer `p` (type `int *`) — `base_size` = 4 (the pointee `int` size). The base is `p`'s value, obtained via `gen_expr`.

Then we compute `(base) + i × elem_size` and place the result in `%rax`.

### How we reach 4.3 — `a[i]` and `*(p+i)` go different ways

`gen_expr` is the entry point throughout ch06/02 (see section 2). The path into `case NODE_INDEX` (this 4.3) is taken **only via the `a[i]` form (i.e., `[]`)**. `*(p+i)` has a different surface syntax, takes a different case, and doesn't end up at 4.3.

**`a[i] = X;`** (lvalue path, on the left of ASSIGN):

```
gen_stmt(EXPR_STMT)
└─ gen_expr(NODE_ASSIGN)        ← entry
    ├─ gen_addr(node->lhs)       ← lhs = NODE_INDEX(a, i)
    │  └─ case NODE_INDEX        ← ★ arrives at 4.3
    │     ├─ gen_addr(IDENT a)   ← 4.1 (the array's base address)
    │     ├─ gen_expr(IDENT i)
    │     ├─ imulq $4, %rax     ← scaling!
    │     └─ addq → %rax = &a[i]
    └─ ... store
```

**`*(p+i) = X;`** (lvalue path on the same kind of ASSIGN):

```
gen_stmt(EXPR_STMT)
└─ gen_expr(NODE_ASSIGN)        ← entry
    ├─ gen_addr(node->lhs)       ← lhs = NODE_UNARY '*'
    │  └─ case NODE_UNARY '*'    ← arrives at 4.2 (not 4.3!)
    │     └─ gen_expr(operand)   ← operand = NODE_BINARY '+'
    │        └─ case NODE_BINARY ← not 4.3 or 4.2, just an addl
    │           ├─ gen_expr(rhs = i)
    │           ├─ gen_expr(lhs = p)
    │           └─ addl %ecx, %eax  ← no scaling!
    └─ ... store
```

The decisive difference is in **`NODE_BINARY '+'`'s codegen** (`case NODE_BINARY` in `steps/ch06/src/codegen.c`) — tiny-c emits a plain `addl` here without looking at types. So `*(p+i)` becomes "p bytes + i" and doesn't advance by element size. `a[i]`, by contrast, reaches 4.3 via `NODE_INDEX`, where `imulq $elem_size` does the scaling.

### Contrast with the C standard

In standard C, `+` on a pointer scales by the type, so `a[i] ≡ *(a + i) ≡ *(p + i)`. tiny-c only implements scaling in `NODE_INDEX` (the `[]` form), so `*(a + i)` and `*(p + i)` — anything that goes through `+` — are byte-level additions and differ from the standard. For element access, use the `a[i]` / `p[i]` form.

> **Reader's exercise**: to implement scaling for `+`, just look at `lhs`/`rhs` types in `case NODE_BINARY '+'` and insert `imulq $base_size` on the pointer side.

Concrete examples:
- `int a[5]`, address of `a[2]`: `&a[0] + 2 × 4 = &a[0] + 8` bytes
- `char *s`, address of `s[3]`: `s + 3 × 1 = s + 3` bytes

These aren't computed at compile time — the generated assembly evaluates the same expressions at runtime. Even with a constant index like `a[2]`, tiny-c doesn't fold it; it emits `imulq $4, %rax` (optimization comes in ch07).

## 5. Users of lvalues — `NODE_ASSIGN` and `&`

ASSIGN and `&` are the consumers of the "addresses" produced in section 4.

### 5.1 `NODE_ASSIGN`

**ASSIGN always needs an lvalue on its left** — writing a value into that location is what assignment does. Whatever the left's form (`x` / `*p` / `a[i]`), we first call `gen_addr(node->lhs)` for the lvalue's address, push it, then compute the right-hand side.

```c
/* inside gen_expr's switch — uses an lvalue internally to write */
case NODE_ASSIGN: {
    gen_addr(node->lhs);    /* compute lhs's address (section 4) */
    emit_push();            /* save it */
    gen_expr(node->rhs);    /* %eax = rhs's value (section 6) */
    emit_pop("%rcx");       /* %rcx = lhs address */
    Type *t = expr_type(node->lhs);
    int sz = t->is_pointer ? 8 : t->base_size;
    emit_store(sz);         /* *(%rcx) = %eax / %rax */
    return;
}
```

`x = 5;`, `p = some_addr;`, `*p = 10;`, `a[i] = 7;` — all the same pattern. **Whether something is an lvalue funnels into `gen_addr`'s "not an lvalue" error.**

### 5.2 The `&` operator

`&x`'s value is "x's address." It's literally `gen_addr(x)`:

```c
/* inside gen_expr's switch — return &x's rvalue (the address) */
case NODE_UNARY:
    if (node->op == '&') {
        gen_addr(node->operand);    /* %rax = x's address */
        return;
    }
    ...
```

Just call `gen_addr` from inside `gen_expr`. The ordinary identifier read (`gen_expr(x)`, section 6.1) follows up with a load from that address (= dereference `%rax`, fetch the memory contents, overwrite `%rax`). With `&x`, we skip that load. As a result, **`%rax` retains the address as-is, untouched by the data at that address.**

## 6. The rvalue side — getting the value (`gen_expr`)

This is the side that puts the **value** of each form (`x` / `*p` / `a[i]`) into `%eax` / `%rax`. The basic pattern is "get the address with `gen_addr`, then load."

### 6.1 Reading an `IDENT` as a value (including array decay)

What we handle here is **a bare identifier in an expression** — `x`, `p`, `a` with no subscript or operator attached. A subscripted form like `a[i]` is built as `NODE_INDEX` by the parser, so it doesn't come into this `case NODE_IDENT` (it's section 6.3's job).

| Source form | AST built by the parser | Case |
|-----------|------------------|----------|
| Bare `a` | `NODE_IDENT("a")` | This 6.1 |
| `a[i]` | `NODE_INDEX(a, i)` | Section 6.3 |

In other words, the "with vs. without subscript" distinction is **already settled at AST construction**, so each codegen case can focus on its own form.

In ch03, `gen_expr(IDENT x)` was simply `movl -off(%rbp), %eax`. In ch06 we branch on type:

```c
/* inside gen_expr's switch — rvalue side, place the identifier's value in %eax/%rax */
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->type->is_array) {
        /* array names decay: value = address of the first element */
        gen_addr(node);
        return;
    }
    /* scalar or pointer: get the address and load */
    gen_addr(node);
    emit_load(type_size(v->type));
    return;
}
```

This is where C's **array-to-pointer decay** appears: an array name **used in an expression without a subscript** (`int *p = a;`, `foo(a)`, etc.) automatically becomes "the address of the first element."

```c
int a[5];
int *p = a;       /* a decays to &a[0] */
strlen(s);        /* if s is char*, s itself is an address */
```

Implementation-wise, **`gen_expr(IDENT array)` short-circuits to `gen_addr(IDENT array)`**. An array's "value" and its "address" are identified — that's why section 3's table said `a`'s rvalue "decays into the address."

The subscripted use `a[i]` is on a different path (section 6.3) via `NODE_INDEX`.

### 6.2 Reading `*p` as a value

`*p`'s value is "the value read from the address stored in p."

```c
/* inside gen_expr's switch — rvalue side, place *p's value in %eax/%rax */
case NODE_UNARY:
    if (node->op == '*') {
        gen_expr(node->operand);    /* %rax = p's value (= pointee address) */
        Type *t = expr_type(node->operand);
        emit_load(t->base_size);    /* load from that address */
        return;
    }
    ...
```

The size for `emit_load` is "the size of what p points to." 4 bytes for `int *p`; 1 byte for `char *s`.

Compared to `gen_addr(*p)` in section 4.2, the only difference is whether we add a load on top. That's what "`*` adds a load" means.

### 6.3 Reading `a[i]` as a value

```c
/* inside gen_expr's switch — rvalue side, place a[i]'s value in %eax/%rax */
case NODE_INDEX: {
    /* place the element address in %rax via gen_addr, then load from it */
    gen_addr(node);                            /* %rax = &a[i] (section 4.3) */
    Type *t = expr_type(node->lhs);
    emit_load(t->base_size);                   /* %rax = a[i]'s value */
    return;
}
```

One load on top of section 4.3's `gen_addr`.

### Same `a[2]`, rvalue vs. lvalue

| Source | Path | Final `%rax` | Follow-up |
|--------|------|--------------|---------|
| `int b = a[2];` (right side, rvalue) | `gen_expr(NODE_INDEX)` → internally `gen_addr` → then load | the **value** of `a[2]` | store into `b`'s slot |
| `a[2] = 3;` (left side, lvalue) | ASSIGN calls `gen_addr(NODE_INDEX)` directly | the **address** of `a[2]` | compute right side `3` and store at that address |

**The address computation is identical**; the only difference is whether we follow it with a load (to read) or a store (to write). The lvalue/rvalue symmetry makes `gen_addr` stand out as a shared component.

## 7. Size-aware load / store

### `emit_load`

```c
static void emit_load(int sz) {
    if (sz == 1)      fprintf(out, "  movsbl (%%rax), %%eax\n");
    else if (sz == 4) fprintf(out, "  movl (%%rax), %%eax\n");
    else              fprintf(out, "  movq (%%rax), %%rax\n");
}
```

`movsbl` (move-sign-extend-byte-to-long) sign-extends a 1-byte value to 4 bytes (`%eax`). char values get signed extension, which is fine for tiny-c's string handling (we only deal with values in the ASCII range).

### `emit_store`

```c
static void emit_store(int sz) {
    if (sz == 1)      fprintf(out, "  movb %%al, (%%rcx)\n");
    else if (sz == 4) fprintf(out, "  movl %%eax, (%%rcx)\n");
    else              fprintf(out, "  movq %%rax, (%%rcx)\n");
}
```

- `movb`: 1-byte store (char)
- `movl`: 4-byte store (int)
- `movq`: 8-byte store (pointer)

`%al` is the low 1 byte of `%rax`, `%eax` the low 4 bytes, `%rax` the full 8. We use different views of the same register depending on the store size.

## 8. Summary

- An lvalue is required only at **the left of `NODE_ASSIGN`** and **the operand of `&`** — everywhere else is rvalue context.
- `gen_expr` computes the **rvalue (value)**; `gen_addr` computes the **lvalue (address)** — both leave the result in `%rax` (or `%eax`).
- A pointer variable `p` involves **two locations** (`p` itself, and the place `*p` points to). Unlike a single-location type such as `int x`, both `p` and `*p` have lvalue and rvalue forms.
- An array name **decays** in rvalue context (becomes the first element's address) — `gen_expr(IDENT array)` short-circuits to `gen_addr`.
- ASSIGN absorbs the various lhs forms (`x` / `*p` / `a[i]`) via `gen_addr(lhs)`. The lhs case analysis disappears.
- Load/store sizes (`movb`/`movl`/`movq`, plus `movsbl` for sign-extended loads) come from the type.
- **In a sentence**: rvalue = lvalue + load. `&` removes a load; `*` adds one.

## Next

In the next section (`03_globals_strings.md`) we look at global variables (`.bss`) and string literals (`.rodata`) — data that lives **outside the stack**.
