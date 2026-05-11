# 03 — Intro to bison and parser.y

The lexer turned the source into a token stream. Next comes the **parser**.

The parser's job is to decide whether the token stream is "grammatically valid," and at the same time assemble it into a **tree** (AST).

```
INT  IDENT(main)  '('  ')'  '{'  RETURN  INT_LIT(42)  ';'  '}'
                                  ↓ parser
                          FUNC_DEF main
                            BLOCK
                              RETURN
                                INT_LIT 42
```

The tool for this is **bison**. Same idea as flex: from a definition file of grammar rules and actions, it generates C source code.

```
parser.y  ─ bison ─→  parser.tab.c   (the parser's C code)
                      parser.tab.h   (token-definition header)
```


## 1. What grammar is

To understand parsing, you have to grasp the idea of a **grammar** first.

A grammar is a set of rules that describes "**what counts as a valid structure**." For example, English has grammar:

```
sentence → subject verb object
subject  → "I" | "You" | "He" | ...
verb     → "see" | "love" | ...
object   → "you" | "him" | ...
```

"`I see you` is a valid sentence (subject verb object)." "`see you I` is not." That kind of judgment is what grammar gives us.

The same idea applies to programming-language grammars.

```
return statement → "return" expression ";"
expression       → integer literal
```

"`return 42;` is a valid return statement." "`return ;` is not valid in tiny-c (expression missing)." We write rules like that.

In bison the syntax looks like:

```
ret_stmt
    : RETURN expr ';'
    ;

expr
    : INT_LIT
    ;
```

The names on the left, `ret_stmt` and `expr`, are called **nonterminals** — names that expand into other things.
The things on the right — `RETURN`, `';'`, `INT_LIT` — are **terminals** (= tokens), the things that actually flow in from the lexer.

"Nonterminals can be expanded further by other rules; terminals can't be broken down any more." That distinction is key.


## 2. The smallest example

Let's look at a slightly larger example written in bison's style. Consider a parser for a tiny language where the only expressions are integer literals.

```
program
    : stmt
    ;

stmt
    : RETURN expr ';'
    ;

expr
    : INT_LIT
    ;
```

Three rules. Said in words:

- **"A `program` consists of a `stmt`."** `program` is the start symbol — the entire source matches against `program`. We're declaring "a program is, at heart, a single statement."
- **"A `stmt` consists of a RETURN token, an `expr`, and a `;` token in that order."** A statement has the form "`return`, some expression, semicolon," in that order.
- **"An `expr` consists of an INT_LIT token."** An expression is just a single integer literal.

The left of `:` is "the name being defined"; the right is "what that name contains (a sequence)."

Rules can reference each other. The definition of `stmt` uses `expr`, and `expr` is defined as its own rule. By chaining rules through names like this we can express nested grammatical structure.

`program` is the **start symbol** — the entry point of the grammar. bison treats the left-hand side of the first rule in the file as the start symbol. It's a declaration that "the entire program can be explained by expanding from `program`."

That gives us the grammar for "a language that accepts only `return N;`." When the token stream `RETURN INT_LIT ';'` arrives, the rules `expr → INT_LIT`, `stmt → RETURN expr ';'`, and `program → stmt` apply in that order, and we can climb back up to `program`. bison decides mechanically whether such a climb is possible.

bison reads the token stream left to right, checking against the rules. At the same time it can run an action (some C code) at the moment a rule matches. We use actions to build the AST.


## 3. Actions and `$1`, `$2`, `$$`

Write an action with `{ ... }` after the right-hand side of a rule.

```
ret_stmt
    : RETURN expr ';'   { /* this is the action */ }
    ;
```

This is the heart of using bison. **The moment a rule matches (when the incoming token stream fits the rule), the corresponding action's C code runs.** With the rule above, the moment the sequence "`RETURN`, `expr`, `;`" is recognized, the `{ ... }` body executes.

The grammar rule "defines a shape," and the action "says what to do when the shape is found." The two come as a pair. bison checks the shape automatically; we only have to write **"what to do once the shape is built."** In our case, we build and stitch together AST nodes. The real rules look like this:

```
expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;

ret_stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;
```

The values an action handles come in two flavors.

- **`$1`, `$2`, `$3`, ...** — the values of the elements on the right-hand side. The number is **the position** (1st, 2nd, 3rd).
- **`$$`** — the value of the nonterminal on the left (`expr`, `ret_stmt`, etc.) itself. Whatever you assign with `$$ = ...` is what a parent rule sees (as `$2`, etc.) when it references this nonterminal.

The `expr` rule: the right-hand side has just one element, `INT_LIT`. `$1` is its value (the integer `42`). We pass it to `new_int_lit` to build a leaf node and store it in `$$`. Now `expr` is a rule that returns `Node *`.

A subtle point: "the value of `$1` is `42`." On the flex side we wrote `[0-9]+ { yylval.int_val = atoi(yytext); return INT_LIT; }`. The `INT_LIT` in `return INT_LIT;` internally expands to a token number (say, the integer 260), and that's what tells bison "the next token is INT_LIT." **What goes into `$1` is not that 260, but the value (`42`) that was stashed in `yylval.int_val` just before.** "The value of a token" and "the number identifying the kind of token" are different things — we'll see exactly how `$1`'s type and content are determined in the next section (`%union` / `%token`).

The `ret_stmt` rule: the right-hand side has three elements `RETURN`, `expr`, `;`, corresponding to `$1`, `$2`, `$3`. `$2` is the `expr` — the node that the rule above stored in `$$`. We wrap it with `new_return` to build a return-statement node.

Each match builds a small node, which then becomes an argument to a larger rule's match, and eventually a whole tree comes together. The picture is "growing the tree from leaves toward the root."


## 4. `%union`, `%token`, `%type` — telling bison the types

bison's rules deal with values (`$1`, `$$`), but we need to tell bison what type those values have. "This `$2` is `Node *`," and so on.

There are three mechanisms for that.

### `%union` lists all the types

```
%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}
```

This declares "the value types in this parser are: integer, string, node, or node list." Behind the scenes, a `union` is defined, and that's also the type of `yylval`.

### `%token <field> NAME` gives a type to a token

```
%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN
```

- The value of an `INT_LIT` token is the `int_val` field (i.e. `int`).
- The value of an `IDENT` token is the `name` field (i.e. `char *`).
- `INT` and `RETURN` carry no value.

`%token <int_val> INT_LIT` is the declaration that tells bison "the value of an `INT_LIT` token comes from `yylval.int_val`." That lines up with what we wrote on the flex side: `yylval.int_val = atoi(yytext);`.

### `%type <field> RULE` gives a type to a nonterminal

```
%type <node> func_def stmt expr
%type <list> stmts
```

- The result of the `func_def`, `stmt`, and `expr` rules is the `node` field (`Node *`).
- The result of the `stmts` rule is the `list` field (`NodeList *`).

With these declarations in place, bison treats the `$1`s and `$$`s in your actions as the correct type.


## 5. The full parser.y for Chapter 1

```c
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;  /* parser result */
%}

%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}

%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN

%type <node> func_def stmt expr
%type <list> stmts

%%

program
    : func_def          { program = $1; }
    ;

func_def
    : INT IDENT '(' ')' '{' stmts '}'
                        { $$ = new_func_def($2, new_block($6)); }
    ;

stmts
    : /* empty */       { $$ = NULL; }
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;

stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;

expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;

%%
```

Sections are divided by `%%` — the same convention as flex.


## 6. Section by section

### Section 1 (C code at the top)

```c
%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;  /* parser result */
%}
```

- `#include "ast.h"` makes the AST node types and `new_xxx` constructors available.
- `yylex` is the function flex generates (returns the next token). The `extern` declaration tells the compiler "it lives in another file."
- `yyerror` is the function bison calls on a syntax error. We define it. For now it just prints an error and exits.
- `Node *program` is the global where the parse result lands. `main` reads it.

### Declaration section (`%union`, `%token`, `%type`)

As covered above — the section that tells bison the type information.

### Section 2 (grammar rules)

Let's go through them in order.

```
program
    : func_def          { program = $1; }
    ;
```

"**A program consists of a single function definition.**" The node (`Node *`) returned by the `func_def` rule is stored in the global variable `program`. That is the compiler's final result.

We don't set `$$` here — but nothing reads the result of the `program` rule, so that's fine.

```
func_def
    : INT IDENT '(' ')' '{' stmts '}'
                        { $$ = new_func_def($2, new_block($6)); }
    ;
```

"**A function definition has the form `int name ( ) { statements }`.**"

- `$1` is `INT` (no value)
- `$2` is the value of `IDENT`, i.e. the function name as a string (`char *`, e.g. `"main"`)
- `$6` is the result of `stmts`, i.e. the statement list (`NodeList *`)

`new_block($6)` wraps the statement list in a block node, and `new_func_def($2, ...)` wraps that into a function-definition node.

```
stmts
    : /* empty */       { $$ = NULL; }
    | stmt stmts        { $$ = new_node_list($1, $2); }
    ;
```

"**A statement list is either empty, or a single statement followed by another statement list.**" This is the **recursive** definition of "list of statements."

`|` means "or," presenting two alternatives.

- When empty: `$$` is NULL.
- When a statement is followed by another statement list: `new_node_list($1, $2)` builds a list whose head is this statement and whose tail is the rest.

For example, when there's exactly one statement, the inner `stmts` first matches the empty rule and becomes `NULL`, and then `new_node_list($1, NULL)` is built — "a list with just this one statement."

> **Aside: right vs. left recursion**
>
> The rule above is called **right recursion** (`stmts : stmt stmts`) — the recursion is on the right side of the right-hand side. There's another style, **left recursion** (`stmts : stmts stmt`), and bison prefers left recursion for memory efficiency (the internal parse stack stays shallow). But left recursion builds lists **in reverse**, so we'd need to flip them afterward. tiny-c only deals with small programs, so the memory difference is effectively zero. We choose **right recursion** for the simpler code.

```
stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;
```

"**A statement has the form `return expr;`.**" `$2` is the expression node. `new_return` builds a return-statement node. The current grammar has no other statement kinds, but later chapters add `if`, `while`, variable declarations, blocks, assignment expressions, and so on.

```
expr
    : INT_LIT           { $$ = new_int_lit($1); }
    ;
```

"**An expression is just an integer literal.**" `$1` is the value of the INT_LIT token (`int`). `new_int_lit` builds an integer-literal node.

## 7. Walking through a parse

Let's trace what happens when the token stream for `int main() { return 42; }` flows through this parser.

Token stream:

```
INT  IDENT(main)  '('  ')'  '{'  RETURN  INT_LIT(42)  ';'  '}'
```

bison reads tokens left to right, and as rules match, it builds the tree **in reverse** (leaves to root). This is called **bottom-up parsing**.

Step by step:

1. **Read `INT_LIT(42)`** → matches the rule `expr : INT_LIT`. Build a node with `new_int_lit(42)` and make it the result (`$$`) of `expr`.
   ```
   INT_LIT(42) ──→ INT_LIT(42) node
   ```

2. **`RETURN expr ';'` — all three elements present** → matches `stmt : RETURN expr ';'`. Build a return node with `new_return(...)`.
   ```
   RETURN ── INT_LIT(42) node ── ';'
                  ↓
              RETURN node
              └─ INT_LIT(42) node
   ```

3. **`stmt stmts` is complete** (one `stmt`, and the trailing `stmts` is empty) → matches `stmts : stmt stmts`. Build a 1-element list with `new_node_list(stmt, NULL)`.

4. **`INT IDENT '(' ')' '{' stmts '}'` — all seven elements present** → matches `func_def : ...`. Build a block node with `new_block(stmts)`, then a function-definition node with `new_func_def("main", block)`.
   ```
              FUNC_DEF main
              └─ BLOCK
                  └─ RETURN
                      └─ INT_LIT 42
   ```

5. **`func_def` is complete** → matches `program : func_def`. Assigned to the global `program`.

The AST is complete; from `program` we can reach the whole tree.

Displayed with `--dump-ast`, it looks like what we saw earlier:

```
FUNC_DEF main
  BLOCK
    RETURN
      INT_LIT 42
```

A nice consequence of bottom-up parsing: the action for each rule runs when "the child nodes are already in place." By the time `stmt : RETURN expr ';'` fires, the `expr` has already been parsed and a node has been built. That's why `new_return($2)` reads naturally.


## 8. The code bison generates

Running `parser.y` through bison produces two files:

```bash
$ bison -d -o parser.tab.c parser.y
```

- **`parser.tab.c`**: the C source for the parser itself, containing a function `yyparse()`.
- **`parser.tab.h`**: a header containing `#define`s for the token names (INT, RETURN, INT_LIT, ...) and the type definition of `yylval` (generated from `%union`). flex's `lexer.l` includes this so the lexer and parser share token names and `yylval`'s type.

The `-d` option is the key to generating the header. Forget it and `parser.tab.h` won't be produced, and the flex side will fail to build.

When `yyparse()` runs, it calls `yylex()` internally to consume tokens, firing actions whenever a rule matches. By the end, the global variable `program` holds the AST.


## 9. flex and bison working together

```
+-----------+
| input file|   tiny-c source (e.g. `int main() { return 42; }`)
+-----+-----+
      |
      v
+-----------+
|  yyin     |   global of type FILE *
+-----+-----+
      |
      v
+-----------+
|  yylex()  |   generated by flex; one call → one token
+-----+-----+
      |
      v
+-----------+
|  yyparse()|   generated by bison; calls yylex internally and builds the tree
+-----+-----+
      |
      v
+-----------+
|  program  |   global; the root of the AST lands here
+-----------+
```

In `main.c` we only do three things:

1. Set `yyin` to the input file
2. Call `yyparse()`
3. Use the AST via `program`


## Next

In the next section (`04_ast.md`) we look at the tree itself — the implementation (`ast.h` and `ast.c`) — starting from the design intent.
