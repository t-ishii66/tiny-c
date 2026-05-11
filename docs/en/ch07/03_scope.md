# 03 — Block scope

`{ ... }` in C isn't just statement grouping — it also delimits the **scope** of variables declared inside. In `{ int x = 1; } { int x = 2; }`, the same name can be re-declared in another block. tiny-c through ch06 rejected that as a "redeclaration error." This section lifts that restriction.

**The previous section (backpatching) is a prerequisite.** Adding scope to ch06's two-phase codegen creates the complexity of tracking the same scope state **in sync** across Phase 1 (collection) and Phase 2 (emission). With the single-pass codegen from the last section, scope operations are integrated into the codegen flow **just once**.

## 1. Data structures — scope tracking

We add the following state for scope management:

```c
#define MAX_SCOPE 64
static LVar *scope_top[MAX_SCOPE];   /* locals head at scope entry */
static int frame_save[MAX_SCOPE];    /* frame_size at scope entry */
static int scope_depth;

static int max_frame_size;           /* the largest frame_size reached so far */
```

The idea is simple:

- The **`locals` linked list** always represents "exactly the variables currently visible."
- On **entering** a scope, save the current `locals` head and `frame_size`.
- On **leaving**, restore those saved values (locals get truncated; frame_size rewinds).

`scope_top[scope_depth]` holds "**the locals head at the moment we entered this scope**" — on exit, just restoring `locals` to that value removes everything declared inside the block. `frame_save[scope_depth]` follows the same idea, so **sibling blocks reuse the same stack slots**.

But rewinding `frame_size` loses the value we needed for the prologue's `subq $N`. So **`max_frame_size`** separately tracks "the largest `frame_size` ever reached." Backpatching uses this `max_frame_size` to write the `subq`.

## 2. Two scope functions

```c
static void enter_scope(void) {
    scope_top[scope_depth] = locals;
    frame_save[scope_depth] = frame_size;
    scope_depth++;
}

static void exit_scope(void) {
    scope_depth--;
    locals = scope_top[scope_depth];      /* truncate → block-internal names go away */
    frame_size = frame_save[scope_depth]; /* rewind → sibling blocks reuse slots */
}
```

Six lines. **`enter_scope`** saves the current state; **`exit_scope`** restores it. That makes "variables declared in this scope" automatically disappear from `locals`, and the associated `frame_size` is released.

## 3. `add_local`'s scope boundary

We want to reject same-name re-declaration within a scope but allow it across scopes — this falls out from controlling **the scan range** in `add_local`'s duplicate check:

```c
static LVar *add_local(char *name, Type *type) {
    LVar *bound = (scope_depth > 0) ? scope_top[scope_depth - 1] : NULL;
    for (LVar *v = locals; v != bound; v = v->next) {   /* scan only the current scope */
        if (strcmp(v->name, name) == 0) {
            fprintf(stderr, "redeclared variable: %s\n", name);
            exit(1);
        }
    }
    int sz = round_up_8(type_size(type));
    frame_size += sz;
    if (frame_size > max_frame_size) max_frame_size = frame_size;  /* update peak */
    LVar *v = calloc(1, sizeof(LVar));
    v->name = name; v->type = type; v->offset = frame_size;
    v->next = locals; locals = v;
    return v;
}
```

The scan range is the half-open interval **head ↓ scope_top[depth-1]** — it covers only "the variables already declared in this scope." Variables in outer scopes aren't touched, so **shadowing** (declaring a new variable in an inner scope with the same name as an outer one, temporarily hiding the outer) just works (no redeclaration error; a new variable is registered in a different slot).

## 4. Integration into codegen

Scope operations are called in three places:

```c
case NODE_BLOCK:
    enter_scope();
    for (NodeList *l = node->stmts; l; l = l->next)
        gen_stmt(l->node);
    exit_scope();
    return;
```

And the start and end of `gen_func` open and close the function scope:

```c
static void gen_func(Node *fn) {
    locals = NULL;
    frame_size = 0;
    max_frame_size = 0;
    scope_depth = 0;
    /* ... prologue + subq placeholder ... */

    enter_scope();   /* function scope: parameters live here */
    /* ... add_local + spill for parameters ... */
    gen_stmt(fn->body);
    exit_scope();

    /* ... epilogue ... */

    /* backpatch: write subq using max_frame_size */
    int aligned_frame = (max_frame_size + 15) & ~15;
    ...
}
```

Key points:

- Backpatching uses **`max_frame_size`, not `frame_size`**. `frame_size` rewinds when scopes exit, but `max_frame_size` holds the "biggest value ever reached" as a high-water mark.
- Single-pass means scope operations live in one continuous codegen flow. The "Phase 1 scope state has to be re-created in Phase 2" complexity of the two-phase design is gone.

## 5. Example: slot reuse

```c
int main() {
    { int a = 1; int b = 2; }   /* a, b */
    { int c = 3; int d = 4; }   /* c, d (← same slots as a, b) */
    return 0;
}
```

The progression:

| Step | locals (head→) | frame_size | max_frame_size |
|---------|---------------|-----------|----------------|
| enter function scope | (empty) | 0 | 0 |
| enter first `{` | (empty) | 0 | 0 |
| `int a` | a | 8 | 8 |
| `int b` | b → a | 16 | 16 |
| exit first `}` | (empty) | 0 | 16 ← peak retained |
| enter second `{` | (empty) | 0 | 16 |
| `int c` | c | 8 | 16 |
| `int d` | d → c | 16 | 16 |
| exit second `}` | (empty) | 0 | 16 |

Final `max_frame_size = 16`. Backpatching fills `subq $16, %rsp` — four variables but only 16 bytes of stack.

## 6. Shadowing an outer variable

```c
int main() {
    int x = 5;          // outer scope
    { int x = 10;       // inner scope; hides the outer x
      return x;         // → 10
    }
    return x;           // unreachable; if reached, would be 5
}
```

When we declare `int x = 10;` inside, `add_local`'s duplicate scan covers the half-open interval **up to `scope_top[1] = outer x`** (i.e., the inner locals are empty), so no collision is detected. A new LVar in a different slot (offset = 16) is created and pushed to the head of `locals`.

The inner `return x;` does `find_var("x")`, which **starts at the head of `locals`**, so it finds the inner x (offset 16, value 10) first.

When the inner `}` runs `exit_scope`, `locals` rolls back up to the outer x, and `frame_size` returns to 8. From here on, `find_var("x")` returns the outer x (offset 8, value 5).

## 7. ch06 vs. ch07

| | ch06 (the main text) | ch07 (this section) |
|---|------------|-----------|
| Block scope | not supported (flat table) | supported |
| `{ int x=1; }{ int x=2; }` | redeclaration error | OK |
| Slot reuse | none | yes (across sibling blocks) |
| `add_local` scan range | all of locals | current scope only |

## 8. Next

In the next section (`04_peephole.md`) we cover the last optimization piece — **peephole optimization**, which looks at the emitted asm and rewrites adjacent instructions. Like backpatching, it works on the memory buffer.
