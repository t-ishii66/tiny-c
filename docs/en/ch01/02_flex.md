# 02 — Intro to flex and lexer.l

The **lexer** is a program that converts the source string into a stream of **tokens**.

```
"int main() { return 42; }"
              ↓ lexer
INT  IDENT(main)  '('  ')'  '{'  RETURN  INT_LIT(42)  ';'  '}'
```

Writing one by hand is tedious and error-prone. That's where **flex** comes in — **a program that generates a lexer for you**. Given a definition file of regular expressions paired with actions (what to do when a pattern matches), flex produces C source code.

```
lexer.l   ─ flex ─→   lex.yy.c   (this is the lexer's C code)
```

Write `lexer.l`, run it through flex to produce `lex.yy.c`, then compile that with gcc and you have a lexer.


## 1. Structure of a flex file

A `.l` file (flex definition file) is divided into three sections, separated by lines containing `%%`.

```flex
%{
   /* C code (top) */                          ← Section 1
%}

   /* options */

%%

   /* pattern / action definitions */          ← Section 2

%%

   /* C code (bottom) */                       ← Section 3
```

| Section | Contents |
|------|------|
| Section 1 | C header `#include`s and option declarations. Everything inside `%{ ... %}` is pasted verbatim at the top of the generated code. |
| Section 2 | Patterns → actions. **This is the main body.** |
| Section 3 | Auxiliary C functions (we don't use it). |

Think of `%%` as "section divider."


## 2. Patterns and actions

Section 2 is written like this:

```flex
pattern     { action }
pattern     { action }
...
```

flex reads the input character by character and decides "which pattern matches." When a match is found, it runs the corresponding action (some C code).

Example:

```flex
"hello"      { printf("hello!\n"); }
"world"      { printf("world!\n"); }
[0-9]+       { printf("number: %s\n", yytext); }
.            { /* do nothing */ }
```

Here `yytext` is a variable that flex provides; **it holds the actual matched string**. If `[0-9]+` matched `42`, then `yytext` is the string `"42"`.


## 3. Two important rules

When flex picks a pattern, two implicit rules apply.

### Rule A: Longest match wins

When multiple patterns can match at the same position, **the one that matches the longest** is chosen.

For instance, suppose we have:

```flex
"if"          { return IF; }
[a-zA-Z_]+    { return IDENT; }
```

For the input `if` alone, both patterns match "the two characters `if`" — a tie.

But for the input `iframe`, `"if"` only matches 2 characters while `[a-zA-Z_]+` matches all 6. flex picks the longest match, so `iframe` is treated as an IDENT. That's the "longest-match principle."

### Rule B: Earlier wins ties

When the longest matches are the same length, **the pattern listed earlier** wins.

```flex
"int"                    { return INT; }
[a-zA-Z_][a-zA-Z0-9_]*   { return IDENT; }
```

For the input `int`, both match "the three characters `int`" — a tie. Since `"int"` comes first, it wins. So `int` becomes an INT token, not an IDENT.

**If the order were reversed, `int` would be recognized as an IDENT, and keywords and identifiers would become indistinguishable.** That's why keywords come before identifiers.


## 4. yylval — carrying a value along

When you write `return INT;`, flex returns just the token name (INT) to the caller. But for integer literals and identifiers, we want to send **the actual value** too. When we read `42`, we want to tell the caller "INT_LIT token, with value 42."

The mechanism for that is `yylval`. `yylval` is a variable shared between flex and bison — a place to stash the value associated with a token.

```flex
[0-9]+    { yylval.int_val = atoi(yytext); return INT_LIT; }
```

This says: "On a match against a run of digits, convert `yytext` (the matched string, e.g. `"42"`) to an integer with `atoi`, put it in `yylval.int_val`, and return INT_LIT."

`yylval` is a struct (a union, really) that can hold different kinds of values. We use `int_val` for integers, `name` for strings, and so on. We define this union in the next section (bison).


## 5. The full lexer.l for Chapter 1

```flex
%{
#include "ast.h"
#include "parser.tab.h"
%}

%option noyywrap

%%
"int"       { return INT; }
"return"    { return RETURN; }
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
"("         { return '('; }
")"         { return ')'; }
"{"         { return '{'; }
"}"         { return '}'; }
";"         { return ';'; }
[ \t\n]+    { /* skip whitespace */ }
"//".*      { /* skip line comments */ }
.           { fprintf(stderr, "unknown char: %c\n", *yytext); exit(1); }
%%
```

That's all it takes to get a working lexer.


## 6. Line by line

### Section 1 (top)

```flex
%{
#include "ast.h"
#include "parser.tab.h"
%}
```

Anything inside `%{ ... %}` is copied verbatim to the top of the generated C code.

- `ast.h` is needed so we can use AST node types (later on, `yylval` will hold `Node *` values).
- `parser.tab.h` is the header that bison generates. It contains the definitions of token names (INT, RETURN, INT_LIT, ...) and the type of `yylval`. Without it, the token names are undefined.

### Options

```flex
%option noyywrap
```

By default, flex tries to call a function called `yywrap` when it hits end-of-file — it's for processing multiple files in sequence. We only handle one file, so we don't need it. `noyywrap` disables it. Without this, the linker would demand an implementation of `yywrap`.

### Section 2 (patterns and actions)

```flex
"int"       { return INT; }
"return"    { return RETURN; }
```

Keywords. When the string `int` appears, return INT; for `return`, return RETURN. They carry no value, so we don't touch `yylval`. **They must come before the identifier pattern** — that's important.

```flex
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
```

Integer literal. On matching a run of digits, convert `yytext` (the matched string) to an int with `atoi`, store it in `yylval.int_val`, and return INT_LIT.

```flex
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
```

Identifier. "Starts with a letter or underscore, followed by letters/digits/underscores." `yytext` is a pointer into flex's internal buffer and gets overwritten on the next match, so we `strdup` it onto the heap and put the copy in `yylval.name`.

```flex
"("         { return '('; }
")"         { return ')'; }
"{"         { return '{'; }
"}"         { return '}'; }
";"         { return ';'; }
```

Punctuation. No value. We write `return '('` — the token name is the **character itself**. This is a bison convention: single-character punctuation uses its character code as the token number. Saves us from defining a name for it.

> **Aside: don't the token numbers collide?**
>
> `yylex()` returns a single `int`. `return '('` returns 40 (the ASCII code of `'('`), and `return IDENT` returns some other integer. We'd be in trouble if these collided.
>
> bison assigns named tokens (`INT`, `RETURN`, `INT_LIT`, `IDENT`, ...) **integers from 256 onwards**. So they never collide with single-character tokens in the ASCII range (0–255). When we look inside the `parser.tab.h` produced in the next chapter (03_bison.md), we'll see:
>
> ```c
> #define INT       258
> #define RETURN    259
> #define INT_LIT   260
> #define IDENT     261
> ```
>
> Summary:
>
> | Return value range | Meaning |
> |-----------|-----|
> | 0 | EOF |
> | 1–255 | Single-character tokens (the character's ASCII code) |
> | 256+ | Named tokens (assigned by bison) |
>
> The parser distinguishes them by range. We never need to think about the actual numbers — we just write `'('` or `IDENT` by name.

```flex
[ \t\n]+    { /* skip whitespace */ }
"//".*      { /* skip line comments */ }
```

Whitespace and comments. When the action matches but doesn't `return`, flex doesn't hand the token back to the caller and tries the next match instead. In other words, it **skips**.

```flex
.           { fprintf(stderr, "unknown char: %c\n", *yytext); exit(1); }
```

The fallback. Any single character that doesn't match anything above produces an error here. `.` is "any single non-newline character," but newlines are caught by `[ \t\n]+` above, so we don't miss them.


## 7. A walk-through of actual tokenization

Let's trace how this lexer processes `int main() { return 42; }`. flex scans left to right, applying longest-match plus earlier-wins.

| Position | Remaining input | Match | Action |
|-----|-----------|-------|------|
| 0 | `int main() { return 42; }` | `"int"` | return INT |
| 3 | ` main() { return 42; }` | `[ \t\n]+` (` `) | skip |
| 4 | `main() { return 42; }` | `[a-zA-Z_]...` (`main`) | yylval.name = "main"; return IDENT |
| 8 | `() { return 42; }` | `"("` | return '(' |
| 9 | `) { return 42; }` | `")"` | return ')' |
| 10 | ` { return 42; }` | `[ \t\n]+` | skip |
| 11 | `{ return 42; }` | `"{"` | return '{' |
| 12 | ` return 42; }` | `[ \t\n]+` | skip |
| 13 | `return 42; }` | `"return"` | return RETURN |
| 19 | ` 42; }` | `[ \t\n]+` | skip |
| 20 | `42; }` | `[0-9]+` | yylval.int_val = 42; return INT_LIT |
| 22 | `; }` | `";"` | return ';' |
| 23 | ` }` | `[ \t\n]+` | skip |
| 24 | `}` | `"}"` | return '}' |
| 25 | (end) | | return 0 (EOF) |

The token stream delivered to the caller (the bison parser of the next chapter) looks like:

```
INT, IDENT(main), '(', ')', '{', RETURN, INT_LIT(42), ';', '}'
```

A flat character sequence has been turned into nine meaningful chunks. That's lexical analysis.


## 8. The code flex generates

Running `lexer.l` through flex produces a C source file `lex.yy.c`.

```bash
$ flex -o lex.yy.c lexer.l
```

The contents are hundreds to thousands of lines of C (mostly tables and a loop implementing an automaton). We never have to read them directly. There are only two things to remember:

- A function **`int yylex(void)`** is defined. Each call returns the next token. On EOF it returns 0.
- A global variable **`yyin`** of type `FILE *` selects the input file. The default is stdin.

The bison-generated parser calls `yylex()` internally to pull tokens. All we need to do is set `yyin` to our input file and call the bison parser.


## Next

In the next section (`03_bison.md`) we build the parser — the part that assembles this token stream into a **tree**. bison is the auto-generation tool that pairs with flex.
