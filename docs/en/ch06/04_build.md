# 04 — Complete files and build

We lay out ch06's main diffs against ch05, then run Hello, world and strlen.

## 1. lexer.l — new keywords and tokens

Add to ch05's lexer:

```flex
"char"      { return CHAR; }                                  /* added */
"void"      { return VOID; }                                  /* added */
"&"         { return '&'; }                                   /* added */
"["         { return '['; }                                   /* added */
"]"         { return ']'; }                                   /* added */

'(\\.|[^\\'])'      { /* added: char literal */ ... return CHAR_LIT; }
\"([^"\\]|\\.)*\"   { /* added: string literal */ ... return STRING_LIT; }
```

An `unescape` helper handles `\n`, `\t`, `\\`, `\'`, `\"`, `\0`. The full version is in `steps/ch06/src/lexer.l`.

## 2. parser.y — types, pointers, arrays, globals

Main additions and changes:

```yacc
%token <int_val> INT_LIT CHAR_LIT
%token <name> IDENT
%token <str> STRING_LIT                                     /* added */
%token INT CHAR VOID RETURN IF ELSE WHILE                   /* added: CHAR VOID */
%token EQ_OP NE_OP LE_OP GE_OP

%type <type> type                                           /* added */

program
    : top_levels                                            { program = new_program($1); }
    ;

top_levels                                                  /* added (renamed and extended from func_defs) */
    : /* empty */
    | top_level top_levels                                  { $$ = new_node_list($1, $2); }
    ;

top_level                                                   /* added */
    : func_def
    | type IDENT ';'                                        { $$ = new_global_var_decl($2, $1); }
    | type IDENT '[' INT_LIT ']' ';'                        { $$ = new_global_var_decl($2, type_array($1->base_size, $4)); }
    ;

type                                                        /* added */
    : INT                                                   { $$ = type_int(); }
    | CHAR                                                  { $$ = type_char(); }
    | VOID                                                  { ... }
    | INT '*'                                               { $$ = type_ptr(4); }
    | CHAR '*'                                              { $$ = type_ptr(1); }
    ;

func_def
    : type IDENT '(' params ')' '{' stmts '}'               /* changed: INT → type */
                                                            { $$ = new_func_def($2, $4, new_block($7)); }
    ;

param
    : type IDENT                                            /* changed: INT → type */
    ;

stmt
    : ...
    | type IDENT '=' expr ';'                               /* changed: INT → type */
    | type IDENT '[' INT_LIT ']' ';'                        /* added */
    | ...
    ;

unary
    : ...
    | '&' unary                                             /* added */
    | '*' unary                                             /* added */
    ;

primary
    : INT_LIT                                               { ... }
    | CHAR_LIT                                              { ... }                  /* added */
    | STRING_LIT                                            { ... }                  /* added */
    | IDENT
    | IDENT '(' args ')'
    | IDENT '[' expr ']'                                    { ... }                  /* added */
    | '(' expr ')'
    ;
```

## 3. ast.h — the Type struct and new nodes

```c
typedef enum {
    /* same as ch05, plus: */
    NODE_CHAR_LIT,        /* added */
    NODE_STRING_LIT,      /* added */
    NODE_GLOBAL_VAR_DECL, /* added */
    NODE_INDEX,           /* added */
    /* ... */
} NodeKind;

typedef struct Type Type;          /* added */
struct Type {                      /* added */
    int is_pointer;                /* added */
    int is_array;                  /* added */
    int base_size;                 /* added */
    int array_size;                /* added */
};                                 /* added */

struct Node {
    /* ... existing ... */
    Type *type;                    /* added: VAR_DECL, GLOBAL_VAR_DECL, IDENT(param) */
    char *str_val;                 /* added: NODE_STRING_LIT */
    int str_len;                   /* added */
};

/* Type constructors added */
Type *type_int(void);
Type *type_char(void);
Type *type_ptr(int base_size);
Type *type_array(int base_size, int n);
int type_size(Type *t);
int elem_size(Type *t);

/* Node constructors added */
Node *new_char_lit(int val);
Node *new_string_lit(char *str, int len);
Node *new_global_var_decl(char *name, Type *type);
Node *new_index(Node *base, Node *idx);
/* var_decl signature changed: type argument added */
Node *new_var_decl(char *name, Type *type, Node *init);
```

## 4. codegen.c — major overhaul

New machinery:

- **`gen_addr`**: place the address of an lvalue (IDENT, `*p`, `a[i]`) in `%rax`.
- **`expr_type`**: infer an expression's type (variable → symbol table, `&` → pointer promotion, `*` → pointer demotion).
- **`emit_load(sz)` / `emit_store(sz)`**: size-aware 1/4/8-byte reads/writes.
- **`globals` table**: symbol table for global variables.
- **`strs` table**: string literals. Placed in `.rodata` with labels `.LSn:`.

Key `gen_expr` cases:

```c
case NODE_IDENT: {
    LVar *v = find_var(node->name);
    if (v->type->is_array) {
        gen_addr(node);              /* array decay */
        return;
    }
    gen_addr(node);
    emit_load(type_size(v->type));   /* scalar/pointer load */
    return;
}
case NODE_UNARY:
    if (node->op == '&') { gen_addr(node->operand); return; }
    if (node->op == '*') {
        gen_expr(node->operand);
        emit_load(expr_type(node->operand)->base_size);
        return;
    }
    /* - and ! same as ch04 */
case NODE_INDEX:
    gen_addr(node);
    emit_load(expr_type(node->lhs)->base_size);
    return;
case NODE_ASSIGN:
    gen_addr(node->lhs);
    emit_push();
    gen_expr(node->rhs);
    emit_pop("%rcx");
    emit_store(...);
    return;
case NODE_STRING_LIT:
    int n = add_string(node->str_val, node->str_len);
    fprintf(out, "  leaq .LS%d(%%rip), %%rax\n", n);
    return;
```

The `codegen()` driver:

```c
void codegen(Node *prog, FILE *output) {
    /* Phase 1: register globals first */
    for (NodeList *l = prog->stmts; l; l = l->next)
        if (l->node->kind == NODE_GLOBAL_VAR_DECL)
            add_global(l->node->name, l->node->type);

    /* .text — functions */
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)
        if (l->node->kind == NODE_FUNC_DEF)
            gen_func(l->node);

    /* .bss — global variables */
    int has_globals = 0;
    for (NodeList *l = prog->stmts; l; l = l->next) {
        Node *n = l->node;
        if (n->kind != NODE_GLOBAL_VAR_DECL) continue;
        if (!has_globals) { fprintf(out, "  .bss\n"); has_globals = 1; }
        fprintf(out, "  .globl %s\n", n->name);
        fprintf(out, "%s:\n", n->name);
        fprintf(out, "  .zero %d\n", type_size(n->type));
    }

    /* .rodata — string literals */
    if (strs) {
        fprintf(out, "  .section .rodata\n");
        for (StrLit *s = strs; s; s = s->next)
            emit_string_literal(s);
    }
}
```

Full version in `steps/ch06/src/codegen.c`.

## 5. main.c / Makefile — no changes

## 6. Build & run

```bash
$ cd steps/ch06
$ make
$ cat hello.c
int main() {
    char *s = "hello";
    printf("%s\n", s);
    return 0;
}

$ ./tinyc hello.c > hello.s
$ gcc -o hello hello.s
$ ./hello
hello
```

A compiler that could only do `return 42;` in ch01 has grown, over six chapters, into one that can print strings via `printf`.

### Pointer operations

```bash
$ cat ptr.c
int main() {
    int x = 5;
    int *p = &x;
    *p = 10;
    return x;
}
$ ./tinyc ptr.c > ptr.s && gcc -o ptr ptr.s && ./ptr; echo $?
10
```

### Arrays

```bash
$ cat arr.c
int main() {
    int a[5];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    return a[0] + a[1] + a[2];
}
$ ./tinyc arr.c > arr.s && gcc -o arr arr.s && ./arr; echo $?
60
```

### Globals

```bash
$ cat g.c
int g;
int set(int v) { g = v; return 0; }
int main() {
    set(42);
    return g;
}
$ ./tinyc g.c > g.s && gcc -o g g.s && ./g; echo $?
42
```

### String handling (strlen)

```bash
$ cat strlen.c
int strlen(char *s) {
    int n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

int main() {
    char *s = "hello";
    int len = strlen(s);
    printf("len=%d\n", len);
    return len;
}
$ ./tinyc strlen.c > strlen.s && gcc -o strlen strlen.s && ./strlen
len=5
$ echo $?
5
```

`s[n]` reads one byte at a time, counting until the null terminator.

## 7. Reading the Hello, world assembly

Translating `int main() { char *s = "hello"; printf("%s\n", s); return 0; }`:

```
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp                # local s's slot (8 bytes + alignment)

  ; char *s = "hello";
  leaq -8(%rbp), %rax           # &s
  pushq %rax
  leaq .LS0(%rip), %rax         # address of "hello"
  popq %rcx
  movq %rax, (%rcx)             # *(&s) = address of "hello" → assigned to s

  ; printf("%s\n", s);
  leaq -8(%rbp), %rax           # &s
  movq (%rax), %rax             # s's value = address of "hello"
  pushq %rax                    # push arg 2
  leaq .LS1(%rip), %rax         # address of "%s\n"
  pushq %rax                    # push arg 1
  popq %rdi                     # %rdi = "%s\n"
  popq %rsi                     # %rsi = s
  movl $0, %eax                 # variadic ABI
  call printf

  ; return 0;
  movl $0, %eax
  leave
  ret
  movl $0, %eax
  leave
  ret

  .section .rodata
.LS1:
  .byte 37, 115, 10, 0          # "%s\n\0"
.LS0:
  .byte 104, 101, 108, 108, 111, 0  # "hello\0"
```

`.LS0` and `.LS1` sit in `.rodata`. `leaq ...(%rip), %rax` fetches their addresses, which we line up in `%rdi` and `%rsi` before calling `printf`. The stack is kept 16-aligned per the System V ABI (the 16-byte local allocation for `s` ensures alignment right before `call printf`).

## 8. tiny-c's journey: what we've built

By the end of Chapter 6, tiny-c covers most of the language spec we initially set out:

- Integer types (`int`, `char`), pointers, 1-D arrays, `void` return type
- Arithmetic / comparison / negation / assignment, with precedence expressed in the grammar
- Local variables (initializer required), global variables (zero-initialized)
- Control flow (`if`/`else`, `while`)
- Function definitions, up to 6 parameters, recursion, mutual recursion, and external function calls like `printf`
- Character literals, string literals
- Pointer operations (`&`, `*`, `[]`)

**The compiler's main machinery** — lexer / parser / AST / codegen / lvalue-rvalue — we've built up over six chapters.

## Summary

- Pointers, arrays, globals, string literals — we now handle data that lives outside the stack.
- Introducing `gen_addr` makes the lvalue/rvalue symmetry explicit in codegen.
- `&` removes a load; `*` adds one. Array names decay.
- Globals in `.bss`, string literals in `.rodata`, accessed via `%rip`-relative addressing.
- Size-aware load/store for char/int/pointer: `movsbl`/`movl`/`movq`, `movb`/`movl`/`movq`.
- `printf` now works, and tiny-c has reached a practical subset of C.
