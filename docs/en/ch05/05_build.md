# 05 — Complete files and build

We lay out the diff against ch04, build, and run recursion. With the nested call (`f(1) + f(2)`) we'll watch **the moment padding is inserted** in the generated assembly.

## 1. lexer.l — add the comma

Just one line on top of ch04.

```flex
","         { return ','; }                           /* added */
```

## 2. parser.y — substantial expansion

Many changes; just the main points here. Full version in `steps/ch05/src/parser.y`.

```yacc
%type <node> ... param
%type <list> stmts func_defs params param_list args arg_list

program
    : func_defs                  { program = new_program($1); }
    ;

func_defs                                              /* added */
    : /* empty */                { $$ = NULL; }        /* added */
    | func_def func_defs         { $$ = new_node_list($1, $2); }   /* added */
    ;                                                  /* added */

func_def
    : INT IDENT '(' params ')' '{' stmts '}'           /* params added */
                                 { $$ = new_func_def($2, $4, new_block($7)); }
    ;

params                                                 /* added */
    : /* empty */                { $$ = NULL; }        /* added */
    | param_list                 { $$ = $1; }          /* added */
    ;                                                  /* added */
param_list                                             /* added */
    : param                      { $$ = new_node_list($1, NULL); }     /* added */
    | param ',' param_list       { $$ = new_node_list($1, $3); }       /* added */
    ;                                                  /* added */
param                                                  /* added */
    : INT IDENT                  { $$ = new_ident($2); }               /* added */
    ;                                                  /* added */

primary
    : INT_LIT                    { $$ = new_int_lit($1); }
    | IDENT                      { $$ = new_ident($1); }
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }    /* added */
    | '(' expr ')'               { $$ = $2; }
    ;

args                                                   /* added */
    : /* empty */                { $$ = NULL; }        /* added */
    | arg_list                   { $$ = $1; }          /* added */
    ;                                                  /* added */
arg_list                                               /* added */
    : expr                       { $$ = new_node_list($1, NULL); }     /* added */
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }       /* added */
    ;                                                  /* added */
```

`%expect 1` (dangling-else) carries over from ch04.

## 3. ast.h — two new node kinds, two new fields

```c
typedef enum {
    /* ... same as ch04 ... */
    NODE_CALL,           /* added */
    NODE_FUNC_DEF,       /* signature changed (params added) */
    NODE_BLOCK,
    NODE_PROGRAM,        /* added */
} NodeKind;

struct Node {
    /* ... existing ... */
    NodeList *params;    /* added: NODE_FUNC_DEF */
    NodeList *args;      /* added: NODE_CALL */
};

Node *new_call(char *name, NodeList *args);                  /* added */
Node *new_func_def(char *name, NodeList *params, Node *body);/* signature changed */
Node *new_program(NodeList *funcs);                          /* added */
```

## 4. ast.c — constructors and print_ast

Add `new_call` and `new_program`. Change `new_func_def` to accept `params`. Add `NODE_CALL` and `NODE_PROGRAM` cases to `print_ast`, and have the `NODE_FUNC_DEF` case display the parameter list too.

Full version in `steps/ch05/src/ast.c`.

## 5. codegen.c — major overhaul

We **redesign** the `codegen.c` we've been growing since ch03.

Main changes:

- **Two-phase codegen**: `collect_locals` builds the symbol table first; `gen_func` emits the body.
- **Pre-pass allocation**: one `subq $aligned_frame, %rsp` in the prologue. `var_decl` no longer emits `subq`.
- **`stack_offset` tracking**: incremented/decremented by `emit_push`/`emit_pop`, used to decide whether to pad at `call`.
- **`gen_func`**: emit each function independently; reset symbol table and frame.
- **`NODE_CALL` codegen**: push arguments in reverse, then pop into registers. Pad alignment dynamically.
- **`codegen` itself**: loop over the functions hanging under PROGRAM.

Full version in `steps/ch05/src/codegen.c`. Highlights:

```c
static const char *arg_regs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8",  "%r9"};   /* added */
static const char *arg_regs32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};  /* added */

static int stack_offset;                                        /* added */

static void emit_push(void) {                                   /* added */
    fprintf(out, "  pushq %%rax\n");                            /* added */
    stack_offset++;                                             /* added */
}                                                               /* added */
static void emit_pop(const char *reg) {                         /* added */
    fprintf(out, "  popq %s\n", reg);                           /* added */
    stack_offset--;                                             /* added */
}                                                               /* added */

/* new case in gen_expr's switch */
case NODE_CALL: {                                               /* added */
    int pad = (stack_offset % 2) != 0;                          /* added */
    if (pad) {                                                  /* added */
        fprintf(out, "  subq $8, %%rsp\n");                     /* added */
        stack_offset++;                                         /* added */
    }                                                           /* added */
    int n_args = push_args(node->args);                         /* added */
    for (int i = 0; i < n_args; i++)                            /* added */
        emit_pop(arg_regs64[i]);                                /* added */
    fprintf(out, "  movl $0, %%eax\n");                         /* added */
    fprintf(out, "  call %s\n", node->name);                    /* added */
    if (pad) {                                                  /* added */
        fprintf(out, "  addq $8, %%rsp\n");                     /* added */
        stack_offset--;                                         /* added */
    }                                                           /* added */
    return;                                                     /* added */
}                                                               /* added */

static void gen_func(Node *fn) {                                /* added */
    locals = NULL; frame_size = 0; stack_offset = 0;            /* added */
    for (NodeList *l = fn->params; l; l = l->next)              /* added */
        add_local(l->node->name);                               /* added */
    collect_locals(fn->body);                                   /* added */
    int aligned = (frame_size + 15) & ~15;                      /* added */

    fprintf(out, "  .globl %s\n", fn->name);                    /* added */
    fprintf(out, "%s:\n", fn->name);                            /* added */
    fprintf(out, "  pushq %%rbp\n");                            /* added */
    fprintf(out, "  movq %%rsp, %%rbp\n");                      /* added */
    if (aligned > 0) fprintf(out, "  subq $%d, %%rsp\n", aligned);  /* added */

    int i = 0;                                                  /* added */
    for (NodeList *l = fn->params; l; l = l->next) {            /* added */
        int off = find_local(l->node->name);                    /* added */
        fprintf(out, "  movl %s, -%d(%%rbp)\n", arg_regs32[i], off);  /* added */
        i++;                                                    /* added */
    }                                                           /* added */
    gen_stmt(fn->body);                                         /* added */
    fprintf(out, "  movl $0, %%eax\n  leave\n  ret\n");         /* added: implicit return */
}                                                               /* added */

void codegen(Node *prog, FILE *output) {                        /* changed: ch04 emitted the prologue here */
    out = output;
    label_count = 0;
    fprintf(out, "  .text\n");
    for (NodeList *l = prog->stmts; l; l = l->next)             /* changed: single function → loop over functions */
        gen_func(l->node);                                      /* changed */
}
```

## 6. main.c / Makefile — no changes

## 7. Build & run

```bash
$ cd steps/ch05
$ make
mkdir -p build
gcc -Wall -g -Isrc -Ibuild -c -o build/ast.o src/ast.c
gcc -Wall -g -Isrc -Ibuild -c -o build/codegen.o src/codegen.c
gcc -Wall -g -Isrc -Ibuild -c -o build/main.o src/main.c
bison -d -o build/parser.tab.c src/parser.y
gcc -Wall -g -Isrc -Ibuild -c -o build/parser.tab.o build/parser.tab.c
flex -o build/lex.yy.c src/lexer.l
gcc -Wall -g -Isrc -Ibuild -Wno-unused-function -c -o build/lex.yy.o build/lex.yy.c
gcc -o tinyc build/ast.o build/codegen.o build/main.o build/parser.tab.o build/lex.yy.o
```

Function with parameters:

```bash
$ cat > test.c <<'EOF'
int add(int a, int b) {
    return a + b;
}
int main() {
    return add(3, 4);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
7
```

Factorial:

```bash
$ cat > test.c <<'EOF'
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    return fact(5);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
120
```

Fibonacci:

```bash
$ cat > test.c <<'EOF'
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    return fib(10);
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
55
```

Mutual recursion:

```bash
$ cat > test.c <<'EOF'
int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}
int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}
int main() {
    return is_even(10);   // 1
}
EOF
$ ./tinyc test.c > test.s && gcc -o test test.s && ./test; echo $?
1
```

When `is_odd` is called, it **isn't yet defined** (it appears later in the source). tiny-c doesn't do semantic analysis, so forward references work just fine.

## 8. Assembly for the nested call — the moment padding kicks in

Let's see the actual assembly for the `f(1) + f(2)` trace from section 04.5.

```
; compute f(2)
movl $2, %eax            ; arg = 2
pushq %rax               ; so=1
popq %rdi                ; so=0
movl $0, %eax
call f                   ; ★ so=0 (16-aligned), no padding needed
pushq %rax               ; save f(2)'s result, so=1

; now compute f(1)
subq $8, %rsp            ; ★★ padding! so=1 (odd) → so=2, aligned
movl $1, %eax            ; arg = 1
pushq %rax               ; so=3
popq %rdi                ; so=2
movl $0, %eax
call f                   ; ★ so=2 (16-aligned), OK
addq $8, %rsp            ; undo padding, so=1
popq %rcx                ; restore f(2), so=0
addl %ecx, %eax          ; eax = f(1) + f(2)
```

At the first `call f` (= `f(2)`), `stack_offset = 0` (even) → no padding.

After the subsequent `pushq %rax` (to save `f(2)`'s result), `stack_offset = 1`.

Before the second `call f` (= `f(1)`), we detect `stack_offset = 1` → emit `subq $8, %rsp` to pad. After `call`, `addq $8, %rsp` to undo it.

## 9. Next

Chapter 6 adds **pointers and arrays**. `&x`, `*p`, `a[i]`, string literals, global variables. Codegen for addresses (`gen_addr`), the symmetry between `*` and `&`, and address arithmetic for array access.
