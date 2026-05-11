# 01 — Grammar additions

Here are the changes to `parser.y`. Three things to do:

1. Add **new statements** (variable declaration, expression statement).
2. Add an **assignment expression** to the expression hierarchy (at the lowest precedence).
3. Add **identifiers** to `primary`.

## 1. New statements

Through ch02, `stmt` had only one form: `RETURN expr ';'`. We add two more.

```yacc
stmt
    : RETURN expr ';'              { $$ = new_return($2); }
    | INT IDENT '=' expr ';'       { $$ = new_var_decl($2, $4); }    /* added */
    | expr ';'                     { $$ = new_expr_stmt($1); }       /* added */
    ;
```

- **`INT IDENT '=' expr ';'`** is a variable declaration. The shape `int x = 1;`. To keep tiny-c simple, **an initializer is required** — you can't write `int x;` without one.
- **`expr ';'`** is an expression statement. Run the expression, throw away its value. Thanks to this, an assignment like `x = x + 5;` can stand as a "statement."

`new_var_decl` and `new_expr_stmt` are new constructors in `ast.c`. They produce `NODE_VAR_DECL` and `NODE_EXPR_STMT` nodes respectively.

### Don't `INT IDENT` and `IDENT` collide?

A line beginning with `int` and a line beginning with an IDENT (a variable name) can be distinguished by bison's one-token lookahead. The `INT` token is a keyword (the lexer matches `"int"` with top priority), so it never becomes an IDENT. Hence the two rules don't conflict.

## 2. Assignment expressions

In C, assignment is "the lowest-precedence expression" and "right-associative." `a = b = 7` is interpreted as `a = (b = 7)`. We encode that in the grammar.

```yacc
expr
    : assign                       { $$ = $1; }                  /* changed: in ch02 this was : add_expr */
    ;

assign                                                           /* added */
    : add_expr                     { $$ = $1; }                  /* added */
    | add_expr '=' assign          { $$ = new_assign($1, $3); }  /* added */
    ;                                                            /* added */
```

The right-associativity is expressed by `assign : add_expr '=' assign` — the recursion **on the right** is `assign` again (placing the recursion on the left would make it left-associative). `expr` wraps `assign` once, giving the chain `expr → assign → add_expr → ...`.

### About lvalues

In C, only certain things can appear on the left of `=` — a variable, an array element, an indirection `*p`, and so on. These are called **lvalues**. Writing an integer literal on the left, like `5 = 3`, is invalid.

The grammar above says `add_expr '=' assign`, so bison accepts anything on the left. Whether it's a valid lvalue is **checked in codegen** (we'll see that in the next section). Distinguishing lvalues at the grammar level explodes the number of rules, so we defer the error check.

In ch03, the only thing we accept as an lvalue is `IDENT`. Array `[]` and indirection `*` come in ch06.

## 3. Add the identifier to expressions

In `x + y`, the `x` and `y` are identifiers appearing as expressions. We add one line to `primary`.

```yacc
primary
    : INT_LIT                      { $$ = new_int_lit($1); }
    | IDENT                        { $$ = new_ident($1); }       /* added */
    | '(' expr ')'                 { $$ = $2; }
    ;
```

`new_ident` produces a `NODE_IDENT` node. The only content is the name string. **The physical location like `-8(%rbp)` is decided by codegen** — the AST stops at the "name" level.

## 4. The big picture of ch03 parser.y

The grammar with the diffs applied (header section like `%union` elided):

```yacc
program
    : func_def

func_def
    : INT IDENT '(' ')' '{' stmts '}'

stmts
    : /* empty */
    | stmt stmts

stmt
    : RETURN expr ';'
    | INT IDENT '=' expr ';'              # added: variable declaration
    | expr ';'                            # added: expression statement

expr
    : assign

assign                                    # added: assignment level
    : add_expr
    | add_expr '=' assign                 # right-associative

add_expr : ...   (same as ch02)
mul_expr : ...
unary    : ...

primary
    : INT_LIT
    | IDENT                               # added: identifier
    | '(' expr ')'
```

bison **shouldn't issue a conflict warning** with `-d`. The assignment rule `add_expr '=' assign` looks like a shift/reduce hazard at first, but the only symbols that can follow `assign` are `;` and `)` — never `=`. One-token lookahead lets bison decide unambiguously.

## 5. AST shape

Input:

```c
int main() {
    int x = 10;
    x = x + 5;
    return x;
}
```

AST:

```
FUNC_DEF main
  BLOCK
    VAR_DECL x          ← int x = 10;
      INT_LIT 10
    EXPR_STMT           ← x = x + 5;
      ASSIGN
        IDENT x
        BINARY +
          IDENT x
          INT_LIT 5
    RETURN              ← return x;
      IDENT x
```

Three statements in a row, each made of new node kinds (`VAR_DECL`, `EXPR_STMT` wrapping `ASSIGN`, `RETURN`). `IDENT` also makes its debut, holding just the variable's name string.

## Next

Next, codegen — converting names like `x` and `y` into addresses like **`-8(%rbp)`**. The symbol table makes its entrance.
