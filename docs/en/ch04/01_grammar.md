# 01 — Grammar additions

## 1. Comparison operators and logical negation

Here's tiny-c's precedence table again. The same symbols `*` and `-` appear at multiple precedence levels, so we write the entries as actual expressions to make the usage clear.

| Precedence | Operators | Description |
|-------|---------------------|---------------------|
| 2 | `*p`, `&x`, `-x`, `!x` | Unary (dereference, address-of, negation, logical not) |
| 3 | `a * b`, `a / b`, `a % b` | Multiplicative |
| 4 | `a + b`, `a - b` | Additive |
| 5 | `<`, `<=`, `>`, `>=` | Relational |
| 6 | `==`, `!=` | Equality |
| 7 | `=` | Assignment |

Note that `*` and `-` each have **two meanings depending on context**. `*` is dereference as a unary (`*p`, introduced in ch06) and multiplication as a binary (`a * b`). `-` is sign-flip as a unary (`-x`) and subtraction as a binary (`a - b`). The example shapes in the left column let you tell which use is which.

Up through ch03, the hierarchy was `assign → add_expr → mul_expr → unary → primary`, five levels. We insert two more — **relational** and **equality** — between `assign` and `add_expr`. Logical negation `!` is just one extra line in `unary`.

```yacc
expr
    : assign
    ;

assign
    : equality
    | equality '=' assign
    ;

equality
    : relational
    | equality EQ_OP relational
    | equality NE_OP relational
    ;

relational
    : add_expr
    | relational '<' add_expr
    | relational LE_OP add_expr
    | relational '>' add_expr
    | relational GE_OP add_expr
    ;

add_expr   : ...   /* same as ch02 */
mul_expr   : ...
unary
    : primary
    | '-' unary
    | '!' unary                     /* added */
    ;
```

`EQ_OP`, `NE_OP`, `LE_OP`, and `GE_OP` are named tokens for bison so that we can use multi-character tokens. The single-character `<` and `>` stay as plain character tokens.

## 2. Extending the operator code

`==`, `!=`, `<=`, and `>=` don't fit in a single character. Through ch03, `Node->op` was a 1-byte `char`; we widen it to `int` and assign values outside the ASCII range to the multi-character operators.

```c
/* ast.h */
struct Node {
    ...
    int op;          /* ASCII code for single-char ops, or OP_xx for multi-char */
    ...
};

enum {
    OP_LE = 256,    /* <= */
    OP_GE,          /* >= */
    OP_EQ,          /* == */
    OP_NE,          /* != */
};
```

Now `node->op == '+'` and `node->op == OP_LE` are both plain `int` comparisons. Adding a new `case` to the `switch` in codegen is enough.

256 and above never collide with ASCII (0–255). It's the same allocation scheme as ch01's "named tokens start at 256."

## 3. Lexer additions

Add the multi-character tokens, the new keywords, and `!` `<` `>`.

```flex
"if"        { return IF; }
"else"      { return ELSE; }
"while"     { return WHILE; }
"=="        { return EQ_OP; }
"!="        { return NE_OP; }
"<="        { return LE_OP; }
">="        { return GE_OP; }
"<"         { return '<'; }     /* added */
">"         { return '>'; }     /* added */
"!"         { return '!'; }     /* added */
```

flex defaults to **longest match**, so even if the `==` rule appears below the `=` rule, the input `==` matches the former (the 2-character rule beats the 1-character one). Likewise for `!=` and `=`.

The keywords (`if`, `else`, `while`) must be **listed before** the identifier rule `[a-zA-Z_][a-zA-Z0-9_]*` (when the longest match ties, earlier wins). The same reason `int` and `return` came before identifiers in ch01.

## 4. New statements — if, while, block

```yacc
stmt
    : RETURN expr ';'
    | INT IDENT '=' expr ';'
    | expr ';'
    | '{' stmts '}'                         /* added: block statement */
    | IF '(' expr ')' stmt                  /* added: if */
    | IF '(' expr ')' stmt ELSE stmt        /* added: if-else */
    | WHILE '(' expr ')' stmt               /* added: while */
    ;
```

Each rule is straightforward. The body of `if` (the `then` part), the `else` part, and the `while` body are all just **stmt** — so they can be an expression statement, a block, another if/while, a return, anything. That's why you can nest `while` inside `if` and vice versa.

We add the block statement (`'{' stmts '}'`). Without it, you couldn't group multiple statements inside an `if` or `while`. `new_block` is the **AST node constructor** from ch01 (defined in `ast.c`) — we reuse it (it's the same one we used for `func_def`).

`new_if` and `new_while` are similar constructors we add to `ast.c`. They return `NODE_IF` and `NODE_WHILE` nodes respectively.

## 5. The dangling-else problem

```c
if (a) if (b) x = 1; else x = 2;
```

Does this `else` belong to the outer `if (a)` or the inner `if (b)`? In C the rule is "**it belongs to the innermost if**." That is:

```c
if (a)
    if (b)
        x = 1;
    else
        x = 2;
```

If you write the grammar naively, bison reports this as a **shift/reduce conflict**. The cause is these two alternatives in the `stmt` rule:

```yacc
stmt
    : ...
    | IF '(' expr ')' stmt
    | IF '(' expr ')' stmt ELSE stmt
    | ...
    ;
```

Just before reducing the outer `if (a)`, when an `else` token appears after the inner `if (b) x = 1;`, bison hesitates:

- Reduce: treat the inner `if (b)` as `IF '(' expr ')' stmt` (the without-else version) and **reduce it to a stmt**, then take the following `else` as belonging to the outer `if (a)`.
- Shift: **shift the `else`** and assemble the inner `if (b)` as `IF '(' expr ')' stmt ELSE stmt` (the with-else version).

bison's default is **shift**, which happens to match C's rule (bind to the innermost if). So the default behavior is correct.

To suppress the warning, we declare `%expect 1`, saying "exactly one conflict is expected."

```yacc
%expect 1
```

With this declared, a single conflict no longer warns. Zero or two-or-more do warn, so unexpected conflicts still come to our attention.

## 6. AST shapes

Three new node kinds.

```c
NODE_IF:
    cond        // condition expression
    then_body   // statement for the then-part
    else_body   // statement for the else-part (NULL if no else)

NODE_WHILE:
    cond        // condition expression
    body        // statement for the body

NODE_BLOCK:
    stmts       // list of statements (existing since ch01)
```

We add `cond`, `then_body`, and `else_body` fields to the `Node` struct. `body` is the field we created in ch01 for function definitions; we reuse it for `while`.

`new_if` and `new_while` are plain constructors.

```c
Node *new_if(Node *cond, Node *then_body, Node *else_body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_IF;
    n->cond = cond;
    n->then_body = then_body;
    n->else_body = else_body;       // may be NULL
    return n;
}

Node *new_while(Node *cond, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_WHILE;
    n->cond = cond;
    n->body = body;
    return n;
}
```

## 7. AST examples

Input:

```c
if (x > 0)
    y = 1;
else
    y = 2;
```

AST:

```
IF
├─ BINARY >          ← condition
│  ├─ IDENT x
│  └─ INT_LIT 0
├─ EXPR_STMT         ← then part
│  └─ ASSIGN
│     ├─ IDENT y
│     └─ INT_LIT 1
└─ EXPR_STMT         ← else part
   └─ ASSIGN
      ├─ IDENT y
      └─ INT_LIT 2
```

`while` is similar: a tree with two branches (condition and body).

Input:

```c
while (i < 10)
    i = i + 1;
```

AST:

```
WHILE
├─ BINARY <          ← condition
│  ├─ IDENT i
│  └─ INT_LIT 10
└─ EXPR_STMT         ← body
   └─ ASSIGN
      ├─ IDENT i
      └─ BINARY +
         ├─ IDENT i
         └─ INT_LIT 1
```

## 8. A note on block scope

ch04 treats `{ ... }` as a block statement, but **this chapter does not yet implement block scope**. At ch04 the codegen still uses one flat symbol table per function, so writing `int y = ...;` again in another block produces a "redeclared" error. **Block scope is introduced in ch07**, where we extend `locals` so its head is saved on scope entry and rolled back on exit (so `{ int x = 1; } { int x = 2; }` becomes legal).

## Next

In the next section (`02_compare.md`) we look at codegen for comparison and logical negation.
