# 01 — AST-level optimization (constant folding + algebraic simplification)

The first round of optimization is **AST-level**. Right after parsing, before the AST reaches codegen, we collapse "obviously wasteful structures."

## 1. Constant folding

If both children of a binary operator are constants, we **compute the result at compile time** and replace it with a single constant node.

```
BINARY +              →   INT_LIT 5
├─ INT_LIT 2
└─ INT_LIT 3
```

Nesting works recursively:

```
BINARY +              →   BINARY +              →   INT_LIT 14
├─ INT_LIT 2              ├─ INT_LIT 2
└─ BINARY *               └─ INT_LIT 12
   ├─ INT_LIT 3
   └─ INT_LIT 4
```

By optimizing the children first and then looking at the parent, even deeply nested trees fold all the way through in one go.

The operators supported are the arithmetic `+ - * / %` and the comparisons `< > <= >= == !=`. Comparisons return `0` or `1` as `int` (per C convention). Here's the actual code:

```c
if (is_int_lit(L) && is_int_lit(R)) {
    int a = L->int_val, b = R->int_val, r = 0;
    switch (node->op) {
    case '+': r = a + b; break;
    case '-': r = a - b; break;
    case '*': r = a * b; break;
    case '/': if (b == 0) return node; r = a / b; break;
    case '%': if (b == 0) return node; r = a % b; break;
    case '<':   r = (a <  b); break;
    case '>':   r = (a >  b); break;
    case OP_LE: r = (a <= b); break;
    case OP_GE: r = (a >= b); break;
    case OP_EQ: r = (a == b); break;
    case OP_NE: r = (a != b); break;
    default: return node;
    }
    return new_int_lit(r);
}
```

`L` / `R` are aliases for `node->lhs` / `node->rhs` (we'll see them in §4). Division by zero we don't fold — we leave the original AST. It's runtime undefined behavior, but better to leave it in the asm and let link/runtime decide than have the compiler crash.

## 2. Algebraic simplification

When just one side is a particular constant, we rewrite to a simpler form without doing any arithmetic:

| Expression | Simplifies to | Reason |
|----|-------|------|
| `0 + x`, `x + 0` | `x` | additive identity |
| `x - 0` | `x` | subtractive identity |
| `1 * x`, `x * 1` | `x` | multiplicative identity |
| `0 * x`, `x * 0` | `0` | multiplicative zero |
| `x / 1` | `x` | division identity |

The code maps directly to the table. `is_int_lit_val(node, n)` tests "is `node` the integer literal `n`?":

```c
switch (node->op) {
case '+':
    if (is_int_lit_val(L, 0)) return R;       /* 0 + x → x */
    if (is_int_lit_val(R, 0)) return L;       /* x + 0 → x */
    break;
case '-':
    if (is_int_lit_val(R, 0)) return L;       /* x - 0 → x */
    break;
case '*':
    if (is_int_lit_val(L, 1)) return R;       /* 1 * x → x */
    if (is_int_lit_val(R, 1)) return L;       /* x * 1 → x */
    if (is_int_lit_val(L, 0) || is_int_lit_val(R, 0))
        return new_int_lit(0);                /* 0 * x → 0, x * 0 → 0 */
    break;
case '/':
    if (is_int_lit_val(R, 1)) return L;       /* x / 1 → x */
    break;
}
```

Notes:

- We **don't** rewrite `0 - x` to `-x` (the cost of building a unary node outweighs the benefit).
- We **don't** do `x - x → 0` (it drops a variable, which requires semantic analysis; getting it wrong is a real bug source).
- Floating-point simplifications are full of traps, but tiny-c only has `int`, so they don't apply.

## 3. Unary operators get the same treatment

We fold `-(constant)` and `!(constant)`:

```c
case NODE_UNARY: {
    node->operand = optimize_ast(node->operand);
    Node *X = node->operand;
    if (is_int_lit(X)) {
        switch (node->op) {
        case '-': return new_int_lit(-X->int_val);
        case '!': return new_int_lit(!X->int_val);
        }
    }
    return node;
}
```

`!0` becomes `1`, `!1` becomes `0`, `-(5)` becomes `-5`. A loop like `while (!0)` morphs into `while (1)`, and ch05's codegen then handles it as an infinite loop.

## 4. The structure of `optimize_ast`

```c
Node *optimize_ast(Node *node) {
    if (!node) return NULL;
    switch (node->kind) {
    case NODE_BINARY: {
        node->lhs = optimize_ast(node->lhs);
        node->rhs = optimize_ast(node->rhs);
        /* try algebraic simplification and constant folding here */
        ...
        return node;
    }
    case NODE_UNARY:
        node->operand = optimize_ast(node->operand);
        ...
        return node;
    case NODE_BLOCK:
        for (NodeList *l = node->stmts; l; l = l->next)
            l->node = optimize_ast(l->node);
        return node;
    /* ... similarly recurse over the other node kinds ... */
    }
}
```

**Key points**:

- Takes `Node *` and returns `Node *`. **Replacement** can happen, so the returned pointer might differ.
- Optimize children **first** (postorder). Deep nests fold from the bottom up.
- Always do `lhs = optimize_ast(lhs)` (re-receive) — never throw away the return value.
- Cover every node kind in the recursion. Add a `default` fallback so unhandled kinds aren't broken.

## 5. Beware of side effects

Algebraic simplification can **discard one expression and replace it with another**. For example, `x * 0 → 0` drops the code that computes the left `x`.

In C, when `x` is a plain variable reference, that's fine, but if `x` is an expression with a side effect (like a function call), it's not:

```c
f() * 0   /* C says f() should be called */
```

tiny-c's algebraic simplification **doesn't check purity**, so turning `f() * 0` into `0` would remove the call to `f()` — strictly speaking, an invalid transformation.

Production compilers determine "expression purity" before simplifying. tiny-c doesn't (we declare patterns like `f() * 0` outside the supported subset).

## 6. AST, before and after

Input:

```c
int main() {
    int x = 7;
    return x + 0 + x * 1;
}
```

The `--no-opt --dump-ast` AST:

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          BINARY +
            IDENT x
            INT_LIT 0
          BINARY *
            IDENT x
            INT_LIT 1
```

The optimized AST (`--dump-ast`):

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL x : int
        INT_LIT 7
      RETURN
        BINARY +
          IDENT x
          IDENT x
```

`x + 0` becomes `x`, `x * 1` becomes `x`, and the surrounding `+` collapses to `x + x`.

## 7. Next

AST-level optimization is done. In the next section (`02_backpatch.md`) we step away from optimization briefly to introduce **backpatching** — in ch06, Phase 1 determined `frame_size` first and then wrote the prologue, but if codegen output is buffered in memory we can "write `subq $N, %rsp` with a placeholder first, then fill in N later." Phase 1 disappears and codegen becomes single-pass.
