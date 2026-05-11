# 01 — Grammar and AST

Make a program a list of functions, and let functions take parameters.

## 1. A program is a list of functions

Change `program : func_def ;` (the ch04 grammar) into "a list of functions."

```yacc
program
    : func_defs                  { program = new_program($1); }   /* changed: ch04 was : func_def */
    ;

func_defs                                                         /* added */
    : /* empty */                { $$ = NULL; }                   /* added */
    | func_def func_defs         { $$ = new_node_list($1, $2); }  /* added */
    ;                                                             /* added */
```

`func_defs` is right-recursive and assembles a sequence of functions as a `NodeList`. The empty case is allowed (in principle, a program with no functions). `new_program` is a new constructor that builds a `NODE_PROGRAM` node.

The global `Node *program` is still "the root of the parse result," but its contents go from `NODE_FUNC_DEF` to `NODE_PROGRAM`. `NODE_PROGRAM.stmts` is the list of function definitions.

## 2. Parameterized function definitions

```yacc
func_def
    : INT IDENT '(' params ')' '{' stmts '}'                        /* changed: params added */
                                 { $$ = new_func_def($2, $4, new_block($7)); }  /* changed: $4 added */
    ;

params                                                              /* added */
    : /* empty */                { $$ = NULL; }                     /* added */
    | param_list                 { $$ = $1; }                       /* added */
    ;                                                               /* added */

param_list                                                          /* added */
    : param                      { $$ = new_node_list($1, NULL); }  /* added */
    | param ',' param_list       { $$ = new_node_list($1, $3); }    /* added */
    ;                                                               /* added */

param                                                               /* added */
    : INT IDENT                  { $$ = new_ident($2); }            /* added */
    ;                                                               /* added */
```

`params` is "empty or one or more parameters." `param_list` is right-recursive and turns the comma-separated sequence into a `NodeList`.

`param` is **`INT IDENT`** — a type-and-name pair. In ch05, tiny-c only has `int`, so we can write the literal `INT` token directly. When `char *` and friends arrive in ch06, we'll split this into a separate rule `param : type IDENT`, but for now we hard-code `INT`.

Each `param` returns a `NODE_IDENT` via `new_ident(name)`. That puts the parameter name in the AST (we reuse the `IDENT` node kind for "has a name").

`new_func_def`'s signature changes from `(name, body)` to **`(name, params, body)`** — three arguments now.

## 3. Function-call expressions

Add one rule to `primary`.

```yacc
primary
    : INT_LIT                    { $$ = new_int_lit($1); }
    | IDENT                      { $$ = new_ident($1); }
    | IDENT '(' args ')'         { $$ = new_call($1, $3); }      /* added */
    | '(' expr ')'               { $$ = $2; }
    ;

args                                                             /* added */
    : /* empty */                { $$ = NULL; }                  /* added */
    | arg_list                   { $$ = $1; }                    /* added */
    ;                                                            /* added */

arg_list                                                         /* added */
    : expr                       { $$ = new_node_list($1, NULL); }    /* added */
    | expr ',' arg_list          { $$ = new_node_list($1, $3); }      /* added */
    ;                                                            /* added */
```

Note: a bare `IDENT` and `IDENT '(' args ')'` both start with the same token (`IDENT`). **bison resolves this without a conflict**: if `(` follows `IDENT`, it's a function call; otherwise it's a variable reference.

Why no conflict? When choosing a rule, bison **peeks at one upcoming token**. Just after reading `IDENT`, it looks at the next one:

- If it's `(` → assemble as the function-call rule (`IDENT '(' args ')'`).
- Otherwise (any of `+`, `*`, `;`, `)`, `,`, ... — symbols that can follow a `primary`) → reduce as the bare-identifier rule to `primary`.

The choice is unambiguous, so no rule conflict.

## 4. New AST nodes

```c
NODE_PROGRAM,    /* top level: stmts = list of func_defs */
NODE_CALL,       /* function call: name, args (NodeList of expr) */
```

Add two fields to `Node`: `params` and `args` (both `NodeList *`). **`params` is the formal parameters in a function definition; `args` is the actual arguments at a call site** — different roles, separate fields.

```c
struct Node {
    /* ... existing fields ... */
    NodeList *params;    /* for NODE_FUNC_DEF: formal parameters (list of IDENT) */
    NodeList *args;      /* for NODE_CALL:    actual arguments (list of expressions) */
};
```

Three new constructors:

```c
Node *new_call(char *name, NodeList *args);                  /* added */
Node *new_func_def(char *name, NodeList *params, Node *body);/* signature changed */
Node *new_program(NodeList *funcs);                          /* added */
```

`new_func_def` now stores `params` internally.

## 5. About forward references

tiny-c **doesn't require prototypes**. The front end doesn't check whether a function exists; it just emits `call name`. Resolution is **delegated to the linker**.

- A function defined later in the same source can be called (forward reference).
- External functions like `printf` can be called the same way (resolved from libc).
- A nonexistent function name produces a link error (no compile-time detection).

By skipping semantic analysis, we get mutual recursion and external function calls for free.

## 6. Argument-count checking

The "at most 6 arguments" constraint can't be expressed at the grammar level. We check at codegen time (we'll see this in section 04). Before `call`, "error if there are 7 or more."

Zero-argument calls (`foo()`) are accepted by `args : /* empty */`.

## 7. AST example

Input:

```c
int add(int a, int b) {
    return a + b;
}
int main() {
    return add(3, 4);
}
```

AST:

```
PROGRAM
  FUNC_DEF add
    PARAM a
    PARAM b
    BLOCK
      RETURN
        BINARY +
          IDENT a
          IDENT b
  FUNC_DEF main
    BLOCK
      RETURN
        CALL add
          INT_LIT 3
          INT_LIT 4
```

Under `PROGRAM` are two `FUNC_DEF`s. `add` has `PARAM a`, `PARAM b`, and a body that returns `IDENT a + IDENT b`. `main` contains a `CALL add` node with arguments `INT_LIT 3` and `INT_LIT 4`.

`PARAM` is `print_ast`'s label for `NODE_IDENT` in this context; the node kind itself is `NODE_IDENT`.

## Next

In the next section (`02_abi.md`) we look at the rules for calling a function — **the System V AMD64 ABI**.
