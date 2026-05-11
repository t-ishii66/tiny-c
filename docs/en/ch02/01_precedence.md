# 01 — Grammar determines precedence

Whether `2 + 3 * 4` becomes `14` or `20` is decided by how the grammar is written.

## 1. The problem with the naive style

Let's first see "what happens if we write it naively." Suppose we wrote this in bison:

```yacc
expr
    : INT_LIT
    | expr '+' expr
    | expr '-' expr
    | expr '*' expr
    | expr '/' expr
    ;
```

"An expression is either an integer or two expressions joined by an operator." Intuitive. But it's **ambiguous**.

For the input `2 + 3 * 4`, bison can produce two interpretations.

```
Interpretation A:  expr     Interpretation B:  expr
                   / | \                       / | \
                expr + expr                 expr * expr
                  2  / | \                  / | \   4
                   expr * expr           expr + expr
                     3     4               2     3
```

Interpretation A gives `2 + 12 = 14`; interpretation B gives `5 * 4 = 20`. bison decides "both are grammatically valid" and reports a conflict (a shift/reduce conflict). Our intended meaning (A) doesn't get through.

A quick word on what "shift" and "reduce" refer to. bison reads the input bit by bit from the left, assembling the parse tree as it goes. At any moment it has "the sequence of symbols read so far," and on each new symbol it picks one of two moves:

- **shift**: read one more symbol and append it to the current sequence (**continue building the right-hand side**).
- **reduce**: if the tail of the current sequence matches the right-hand side of a grammar rule, replace that section with the single symbol on the left. For instance, given `expr : expr '+' expr`, the **three symbols on the right** get **folded into a single `expr`** on the left.

In an ambiguous grammar there are moments when "should I shift the next symbol, or reduce what I have?" isn't uniquely determined. That's a **shift/reduce conflict**. While reading `2 + 3 * 4`, the moment `*` appears, bison has no basis to choose between "fold `2 + 3` into an `expr` right now (reduce)" and "shift in the `*` first to make `3 * 4` and then add (shift)."

There are two ways to fix this:

1. Use bison's `%left` declarations to specify precedence and associativity (easy, but the precedence doesn't show up in the grammar's structure).
2. **Express precedence in the grammar's layering itself.**

This chapter takes (2). Just by reading the grammar, you can see the precedence. It's the traditional BNF way.

## 2. A layered grammar

We turn the precedence levels into grammar levels.

```yacc
expr     : add_expr ;

add_expr : mul_expr
         | add_expr '+' mul_expr
         | add_expr '-' mul_expr
         ;

mul_expr : unary
         | mul_expr '*' unary
         | mul_expr '/' unary
         | mul_expr '%' unary
         ;

unary    : primary
         | '-' unary
         ;

primary  : INT_LIT
         | '(' expr ')'
         ;
```

This 5-level grammar expresses the precedence table **directly**.

| Level | Operators | Precedence |
|------|--------------|---------|
| `primary` | (literals and parentheses) | (leaf) |
| `unary` | `-` (unary) | high |
| `mul_expr` | `* / %` | middle |
| `add_expr` | `+ -` | low |
| `expr` | (same as `add_expr`) | (entry point) |

**The closer to `primary`, the tighter it binds** — because inner rules form their chunks first.

Let's walk a concrete example. Decompose the input `-2 + 3 * 4` by which level handles which part.

```
input:  -2  +  3 * 4

  (-2) + 3 * 4         ← first, the unary level grabs "-2" as a unit
  unary + (3 * 4)      ← then, the mul_expr level grabs "3 * 4" (shape: mul_expr(3) '*' unary(4))
  add_expr + mul_expr  ← finally, the left unary lifts up to add_expr, fitting add_expr '+' mul_expr
```

Each level's responsibility maps to a specific part of the input. `-2` is a unary operation, so it stops at `unary`; `3 * 4` is multiplication, so it stops at `mul_expr`; the whole is an addition, so it climbs up to `add_expr`.

In short, **"stronger operators (like `*`) form their chunks at inner levels, weaker operators (like `+`) at outer levels"** — that division of labor is what encodes precedence.

## 3. Associativity is also decided by the grammar

Is `10 - 3 - 2` equal to `(10 - 3) - 2 = 5`, or `10 - (3 - 2) = 9`? The C standard says the former (left associative). The grammar can express this too.

Look at the `add_expr` rule again:

```yacc
add_expr : mul_expr
         | add_expr '+' mul_expr
         | add_expr '-' mul_expr
         ;
```

In the second and third rules, **the left side is `add_expr`, the right side is `mul_expr`**. That's how left associativity is expressed.

Apply it to `10 - 3 - 2`:

```
add_expr
├─ add_expr ← "10 - 3"
│  ├─ add_expr ← "10"
│  ├─ '-'
│  └─ mul_expr ← "3"
├─ '-'
└─ mul_expr ← "2"
```

"There's another `add_expr` on the left of `add_expr`" — that's left associativity. Recursion grows to the left.

If we wanted right associativity, we'd flip the rule to `add_expr : mul_expr | mul_expr '+' add_expr`. Then `add_expr` extends to the right, and we get `10 - (3 - 2)`. The way the grammar is written even decides associativity.

C makes addition/subtraction and multiplication/division left associative, so our grammar follows suit.

## 4. Unary minus

Look again at the `unary` level:

```yacc
unary    : primary
         | '-' unary
         ;
```

"A `unary` is either a bare `primary` or a `-` followed by another `unary`." This also handles chains like `--5` (no one writes that, but the grammar permits it).

For something like `-(2 + 3)`, where parentheses follow, the path is `unary → primary → '(' expr ')'`. `primary` accepts `'(' expr ')'`, so the contents inside the parentheses can return to being a full expression. That naturally expresses parenthetical override of precedence.

## 5. The actual AST shape

The grammar's layering shows up in the AST shape.

| Input | AST |
|------|-----|
| `2 + 3 * 4` | <pre>BINARY +<br>├─ INT_LIT 2<br>└─ BINARY *<br>   ├─ INT_LIT 3<br>   └─ INT_LIT 4</pre> |
| `(2 + 3) * 4` | <pre>BINARY *<br>├─ BINARY +<br>│  ├─ INT_LIT 2<br>│  └─ INT_LIT 3<br>└─ INT_LIT 4</pre> |
| `-(2 + 3)` | <pre>UNARY -<br>└─ BINARY +<br>   ├─ INT_LIT 2<br>   └─ INT_LIT 3</pre> |

Two things to note:

- **Parentheses don't survive into the AST.** They're symbols that disambiguate the grammar; once the tree is built, their job is done. The thing "computed first" lives close to the leaves.
- `UNARY` has one child; `BINARY` has two. As AST node types they're distinct (we define `NODE_UNARY` and `NODE_BINARY` in `ast.h`).

## 6. The parser.y actions

When you write `{ ... }` after the right-hand side of a grammar rule, bison runs it on reduce. We build the AST inside these actions.

```yacc
add_expr
    : mul_expr                  { $$ = $1; }
    | add_expr '+' mul_expr     { $$ = new_binary('+', $1, $3); }
    | add_expr '-' mul_expr     { $$ = new_binary('-', $1, $3); }
    ;
```

- `$1`, `$2`, `$3` are the values of the symbols on the right-hand side (whose types we declared with `%type`).
- `$$` is the value to associate with the left-hand side (the chunk this rule creates).
- For the bare `mul_expr` rule, we just pass the right-side value through (`$$ = $1`).
- For the rules with operators, we build an AST node via `new_binary`.

`new_binary('+', $1, $3)` is the constructor defined in `ast.c`. The first argument is the operator character; the second and third are the left and right children (`$2` is `'+'` itself, no need to pass it).

The `'-' unary` rule under `unary` is analogous, calling `new_unary('-', $2)`.

The `'(' expr ')'` rule under `primary` just needs `$$ = $2`: "pass the contents (`expr`, i.e. `$2`) straight up." Parentheses thus disappear from the AST.

## Next

In the next section (`02_codegen.md`) we look at the other side — generating assembly from the AST. We still use only `%eax`, yet handle arbitrarily complex expressions. The stack does the heavy lifting.
