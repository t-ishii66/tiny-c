# 01 — Types and grammar additions

Through ch05, tiny-c treated **everything implicitly as `int`**. ch06 introduces type distinctions for the first time: `int`, `char`, `int *`, `char *`, plus arrays `int[N]` and `char[N]`, and `void` for function return types.

## 1. The Type struct

A small struct that represents the type of each variable and parameter. tiny-c's types are quite limited (one level of pointer, one-dimensional arrays), so a flat struct is enough:

```c
typedef struct {
    int is_pointer;   /* 1 if T*  */
    int is_array;     /* 1 if T[N] */
    int base_size;    /* 1 (char) or 4 (int) */
    int array_size;   /* the N in T[N] */
} Type;
```

The combinations:

| C type | is_pointer | is_array | base_size | array_size |
|--------|-----------|----------|-----------|-----------|
| `int`     | 0 | 0 | 4 | 0 |
| `char`    | 0 | 0 | 1 | 0 |
| `int *`   | 1 | 0 | 4 | 0 |
| `char *`  | 1 | 0 | 1 | 0 |
| `int[10]` | 0 | 1 | 4 | 10 |
| `char[10]`| 0 | 1 | 1 | 10 |
| `void`    | 0 | 0 | 0 | 0 |

Helper functions:

- `type_size(t)`: the bytes the variable occupies (ptr = 8, array = base_size × array_size, scalar = base_size)
- `elem_size(t)`: element size of an array/pointer (base_size)

The value of `int *p` is itself 8 bytes (an address), but `*p` reads 4 bytes (an int). We need to distinguish `base_size` from "the size of the value itself."

## 2. New tokens

```flex
"char"      { return CHAR; }
"void"      { return VOID; }
"&"         { return '&'; }
"["         { return '['; }
"]"         { return ']'; }

'(\\.|[^\\'])'      { /* char literal */ ... return CHAR_LIT; }
\"([^"\\]|\\.)*\"   { /* string literal */ ... return STRING_LIT; }
```

Character literals: `'a'`, `'\n'`, `'\\'`, etc. Convert escape sequences to the actual character and put the char code (as `int`) in `yylval.int_val`.

String literals: `"hello"`, `"%s\n"`, etc. Strip the surrounding `"`s, resolve escapes, and store the bytes in `yylval.str.s` (the string) and the length in `yylval.str.n` (including `\0`).

Escape examples:

```c
"hello\n"   →  bytes: 'h' 'e' 'l' 'l' 'o' 0x0A '\0'   length 7
'\n'        →  value: 10 (ASCII LF, 0x0A)
'\0'        →  value: 0  (ASCII NUL)
```

## 3. The type rule

```yacc
type
    : INT                  { $$ = type_int(); }
    | CHAR                 { $$ = type_char(); }
    | VOID                 { /* base_size = 0 */ }
    | INT '*'              { $$ = type_ptr(4); }
    | CHAR '*'             { $$ = type_ptr(1); }
    ;
```

Note that `*` is used as a token here, as in `INT '*'`. The `*` is **both** a binary operator (multiplication) and a unary operator (dereference). bison distinguishes them by context.

## 4. Global variables: extending the top level

Through ch05, `program : func_defs`. ch06 rewrites the top level so that function definitions and global declarations can sit side by side.

```yacc
program
    : top_levels                             { program = new_program($1); }
    ;

top_levels
    : /* empty */
    | top_level top_levels                   { $$ = new_node_list($1, $2); }
    ;

top_level
    : func_def                               { $$ = $1; }
    | type IDENT ';'                         { $$ = new_global_var_decl($2, $1); }
    | type IDENT '[' INT_LIT ']' ';'         { $$ = new_global_var_decl($2, type_array($1->base_size, $4)); }
    ;
```

In tiny-c, globals have **no initializer** — only `int g;` and `int a[10];` are allowed. At code generation time, they land in `.bss` (zero-initialized).

## 5. Local declarations now use type

In ch03 it was `INT IDENT '=' expr ';'`; in ch06 it's `type IDENT '=' expr ';'`. The type information rides in the AST.

```yacc
stmt
    : ...
    | type IDENT '=' expr ';'                { $$ = new_var_decl($2, $1, $4); }
    | type IDENT '[' INT_LIT ']' ';'         { $$ = new_var_decl($2, type_array($1->base_size, $4), NULL); }
    | ...
    ;
```

`int x = 5;` becomes `var_decl(name="x", type=int, init=5)`. `int a[5];` becomes `var_decl(name="a", type=int[5], init=NULL)` (arrays have no initializer).

## 6. Parameters use type too

```yacc
param
    : type IDENT                             {
        Node *p = new_ident($2);
        p->type = $1;
        $$ = p;
    }
    ;
```

Now `int x` and `char *s` are handled by the same machinery.

## 7. Subscript `a[i]`

A new node, `NODE_INDEX`. In the AST it holds a left (array/pointer) and a right (index).

```yacc
primary
    : ...
    | IDENT '[' expr ']'                     { $$ = new_index(new_ident($1), $3); }
    | ...
    ;
```

`NODE_INDEX` reuses the existing `lhs` / `rhs` fields — `lhs` is the base (array/pointer); `rhs` is the index.

`a[0]` and `a[i+1]` are handled by the same rule (the index can be any expression). In practice, tiny-c supports only the `IDENT '[' expr ']'` form (no complex left like `(p+1)[i]`).

## 8. Unary `&` and `*`

```yacc
unary
    : primary
    | '-' unary                              { $$ = new_unary('-', $2); }
    | '!' unary                              { $$ = new_unary('!', $2); }
    | '&' unary                              { $$ = new_unary('&', $2); }    /* added */
    | '*' unary                              { $$ = new_unary('*', $2); }    /* added */
    ;
```

`*` appears **both** as binary `mul_expr '*' unary` and as unary `'*' unary`. `a * b` matches `mul_expr`; `*p` matches `unary`.

## 9. Character / string literals

```yacc
primary
    : INT_LIT                                { $$ = new_int_lit($1); }
    | CHAR_LIT                               { $$ = new_char_lit($1); }       /* added */
    | STRING_LIT                             { $$ = new_string_lit($1.s, $1.n); } /* added */
    | ...
    ;
```

- `CHAR_LIT` is just an integer value (in C, the type of `'a'` is `int`).
- `STRING_LIT` holds the bytes and the length. At codegen time, the bytes land in `.rodata` and the expression's value becomes the **address** of those bytes (equivalent to `char *`).

## 10. New AST fields

```c
struct Node {
    /* ... existing ... */
    Type *type;        /* NODE_VAR_DECL, NODE_GLOBAL_VAR_DECL, IDENT (param) */
    char *str_val;     /* NODE_STRING_LIT */
    int str_len;       /* NODE_STRING_LIT */
};
```

`type` attaches to variable declarations and parameters. Expression nodes (`IDENT`, `INDEX`, etc.) don't carry types — codegen looks them up via the symbol table (or infers from the expression's structure).

## 11. AST example

```c
int main() {
    char *s = "hello";
    return s[0];
}
```

AST:

```
PROGRAM
  FUNC_DEF main
    BLOCK
      VAR_DECL s : char*
        STRING_LIT "hello"
      RETURN
        INDEX
          IDENT s
          INT_LIT 0
```

`s` has type `char*`, `STRING_LIT` holds `"hello"`, and `s[0]` becomes an `INDEX` node. Codegen looks at this type information and emits a single-byte read for char-units (covered in the next section).

## 12. Next

Grammar and AST now carry types. In the next section (`02_lvalue.md`) we look at the heart of code generation — **the `gen_addr` function** and **the symmetry of lvalue / rvalue**. How `*` and `&` correspond in code generation, why an array name "decays" into an address — all of it falls out from how we use `gen_addr` vs. `gen_expr`.
