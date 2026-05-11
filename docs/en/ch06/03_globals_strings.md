# 03 — Global variables and string literals

Through ch05, all data lived **on the stack**: local variables in a function's frame, push/pop intermediates. ch06 is the first time we put data **off the stack** — globals in `.bss` and string literals in `.rodata`.

## 1. A few ELF sections

An x86-64 Linux executable (ELF) is divided into **sections**. The ones the tiny-c output uses:

| Section | Use | Properties |
|----------|------|------|
| `.text`     | function bodies (executable code) | readable + executable |
| `.bss`      | zero-initialized data | readable/writable; no bytes stored in the file (the loader zeros it) |
| `.rodata`   | read-only data (constants) | read-only, not executable |
| `.data`     | initialized data | readable/writable (tiny-c doesn't use this) |

So far we've only used `.text`. ch06 brings in the other two.

## 2. Global variables: `.bss`

Globals like `int g;` come **without an initializer** in tiny-c — they're zero-initialized, exactly what `.bss` is for.

Sample assembly:

```asm
  .bss
  .globl g
g:
  .zero 4

  .globl arr
arr:
  .zero 40
```

- `.bss` switches sections.
- `.globl g` exposes the name to the linker.
- `g:` is a **label** (a name at that location).
- `.zero 4` reserves 4 bytes of zero (in the ELF file, only the size is recorded, not the actual bytes).

`int g;` is 4 bytes; `int arr[10];` is 40; `char *gp;` is 8 — the `.zero` argument follows the type's size.

## 3. Accessing globals: `name(%rip)`

Locals used `-off(%rbp)`. Globals use **PC-relative addressing**:

```asm
  leaq g(%rip), %rax       # %rax = address of g
  movl (%rax), %eax        # read g's value

  movl %eax, g(%rip)       # write to g (the direct form also works)
```

`g(%rip)` means "the offset from `%rip` (the instruction pointer) to `g`," resolved by the linker. This works regardless of where the executable is loaded (**position-independent code**, PIC).

`gen_addr(IDENT g)` in tiny-c checks whether the variable is global or local and emits the right instruction:

```c
if (v->is_global)
    fprintf(out, "  leaq %s(%%rip), %%rax\n", v->name);
else
    fprintf(out, "  leaq -%d(%%rbp), %%rax\n", v->offset);
```

Since `gen_addr` absorbs the global/local distinction, `gen_expr` and `NODE_ASSIGN` need no changes. Another benefit of the lvalue abstraction.

## 4. Symbol table extension

We keep two symbol tables — locals and globals:

```c
static LVar *locals;     /* per-function */
static LVar *globals;    /* program-wide */
```

Add `is_global` and `Type *type` to the `LVar` struct:

```c
struct LVar {
    char *name;
    int offset;        /* for locals: -offset(%rbp) */
    Type *type;
    int is_global;     /* 1 ⇒ use %rip-relative; offset unused */
    LVar *next;
};
```

"With two separate lists, isn't `is_global` redundant?" you might ask. The reason: `find_var` searches both lists and returns a **unified `LVar*`**. The caller (e.g., `gen_addr`) holds just one `LVar*`, so the LVar itself needs to record which list it came from.

`find_var(name)` checks locals first, then globals. A local with the same name takes precedence — **shadowing between locals and globals** matches C. ch06 doesn't do block scope; within a function, the symbol table is flat (so redeclaring a name in another block is an error). Block scope arrives in ch07.

At the codegen entry point:

1. Walk the whole program once and register every `NODE_GLOBAL_VAR_DECL` in the `globals` table.
2. Then process each function with `gen_func`. Each function resets `locals`; globals stay visible.

This lets a function body write `g = 42` to a global.

## 5. String literals: `.rodata`

A string literal `"hello"` is **read-only data**. It goes in `.rodata`. Each literal gets a unique label `.LSn`, and the code references that address.

```asm
  .section .rodata
.LS0:
  .byte 104, 101, 108, 108, 111, 0
```

The `.byte` directive places the listed byte values. `104, 101, ...` are the ASCII codes for `'h'`, `'e'`, ... The final `0` is the C string's null terminator `'\0'`.

There's a convenient `.string "hello"` directive too, but tiny-c writes the bytes directly (to avoid differences in escape-sequence handling).

The **value** of a string literal is "the address of those bytes":

```asm
  leaq .LS0(%rip), %rax      # %rax = address of "hello"
```

This is what gets assigned to `char *s` or passed as a `printf` argument.

## 6. Managing string literals

To distinguish multiple literals, we accumulate them in a **string literal table** during codegen and dump them all in `.rodata` at the end:

```c
typedef struct StrLit {
    char *str;
    int len;
    int label;        /* the .LS0, .LS1, ... number */
    StrLit *next;
} StrLit;

static StrLit *strs;
static int str_count;

static int add_string(char *str, int len) {
    StrLit *s = calloc(1, sizeof(StrLit));
    s->str = str;
    s->len = len;
    s->label = str_count++;
    s->next = strs;
    strs = s;
    return s->label;
}
```

In `gen_expr(NODE_STRING_LIT)` we call `add_string` for a label number and emit `leaq .LSn(%rip), %rax`.

```c
case NODE_STRING_LIT: {
    int n = add_string(node->str_val, node->str_len);
    fprintf(out, "  leaq .LS%d(%%rip), %%rax\n", n);
    return;
}
```

At the end of `codegen()`, we switch to `.rodata` and emit all the literals as `.LS0`, `.LS1`, ...

## 7. Handling char values

A character literal `'a'` is just an `int` value (97). In the AST it's `NODE_CHAR_LIT(int_val=97)`. Codegen emits `movl $97, %eax` — same as `INT_LIT`.

```c
case NODE_CHAR_LIT:
    fprintf(out, "  movl $%d, %%eax\n", node->int_val);
    return;
```

For `char c = 'A';`:
- The right side `'A'` becomes `movl $65, %eax` (a 4-byte value)
- The left `c` is `char` type → `movb %al, (%rcx)` for a 1-byte store
- `%al` is the low 1 byte of `%eax`, so 65 (the value of `'A'`) is what gets written

For `return c;`:
- Read `c`: `movsbl -off(%rbp), %eax` (sign-extend 1 byte to 4)
- Return 65 via `%eax`

## 8. Size-aware parameter spill

In ch05 every parameter spill was `movl %edi, -off(%rbp)` (4 bytes). ch06 introduces 8-byte arguments (like `char *s`), so we branch:

```c
int sz = t->is_pointer ? 8 : 4;
if (sz == 8)
    fprintf(out, "  movq %s, -%d(%%rbp)\n", arg_regs64[i], v->offset);
else
    fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], v->offset);
```

In `int strlen(char *s)`, `s` is `char *` (a pointer), so `movq %rdi, -8(%rbp)`. In `int set(int v)`, `v` is `int`, so `movl %edi, -8(%rbp)`.

## 9. Tying it together: Hello, world

```c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}
```

1. Parser: receives `STRING_LIT "hello"` and stores it in the node.
2. Codegen begins: `.text` section, `main:` label, prologue.
3. `char *s = "hello";`:
   - `gen_addr(s)` → `leaq -8(%rbp), %rax`; `pushq %rax` saves it.
   - `gen_expr(STRING_LIT "hello")` → `add_string` returns label 0; `leaq .LS0(%rip), %rax` (`%rax` = address of `"hello"`).
   - `popq %rcx` (`%rcx` = address of `s`'s slot).
   - `movq %rax, (%rcx)` to store — the address of `"hello"` lands in `s`'s slot.
4. `printf("%s\n", s);`:
   - `push_args` **pushes arguments in reverse** (so the first argument ends up on top of the stack).
   - First push arg 2 = `s`: `gen_expr(s)` → `leaq -8(%rbp), %rax; movq (%rax), %rax`; push.
   - Then push arg 1 = `"%s\n"`: `gen_expr(STRING_LIT "%s\n")` → `add_string` returns label 1; `leaq .LS1(%rip), %rax`; push.
   - pop `%rdi` ← top of stack = address of `"%s\n"` (= arg 1); pop `%rsi` ← below it = `s`'s value (= address of `"hello"`, arg 2).
   - `movl $0, %eax; call printf`.
5. `return 0;` → `movl $0, %eax; leave; ret`.
6. End of codegen:
   - `.bss` section (empty in this program).
   - Switch to `.section .rodata`, emit `.LS0: .byte 104, 101, 108, 108, 111, 0` and `.LS1: .byte 37, 115, 10, 0`.

Linked and run, it prints `hello`. The moment tiny-c becomes a language that "**can call printf**."

## 10. Summary

- Globals go to `.bss` as `name: .zero N`, accessed via `leaq name(%rip), %rax`.
- `gen_addr` absorbs the global/local difference.
- String literals go to `.rodata` with a unique label `.LSn:`; access them with `leaq .LSn(%rip), %rax` to get the value (the address).
- A char literal `'a'` is just an integer value. A store to a `char` variable uses `movb`; a load uses `movsbl`.
- Pointer-argument spill uses `movq` (8 bytes).

## Next

In the next section (`04_build.md`) we lay out the complete files and run through the Hello, world demo.
