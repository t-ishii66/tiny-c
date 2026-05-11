# 01 — Regular expression basics

The lexer's job is to chop a source string into **meaningful chunks** (tokens).

```
"int main() { return 42; }"
              ↓
[int] [main] [(] [)] [{] [return] [42] [;] [}]
```

To do this, we need to tell the machine **"which patterns of characters become which tokens."** "If the string `return` appears, treat it as a keyword." "If digits run in a row, that's an integer literal." "An alphabetic string is an identifier." The tool for expressing rules like these is the **regular expression**.

flex writes lexing rules as regular expressions. Let's go through the minimum needed for Chapter 1.


## 1. Literal strings

The simplest pattern is a literal string.

```
int
```

This matches "the three-character sequence `int`."

Wrap it in double quotes to treat characters with special meaning as plain characters.

```
"return"
```

This also matches "the six-character sequence `return`." For alphanumerics, with or without quotes makes no difference, but symbols like `;` or `(` are safer quoted.

```
";"     ← single semicolon
"("     ← single left paren
```

In flex, we make a habit of quoting symbols to avoid confusion.


## 2. Character classes `[...]`

A **character class** represents "**any one of these characters**."

```
[0-9]      ← any single digit from 0 to 9
[a-z]      ← any single lowercase letter
[a-zA-Z]   ← any single letter (upper or lower)
[abc]      ← one of a, b, or c
```

`-` specifies a range. `[0-9]` is "0 or 1 or 2 or ... or 9" — any one digit.

Ranges can be combined.

```
[a-zA-Z_]  ← a letter or an underscore
```


## 3. Repetition `+` and `*`

**Quantifiers** express "repetition of the same pattern."

```
[0-9]+     ← one or more digits in a row
[0-9]*     ← zero or more digits in a row (i.e., "zero is OK")
```

`+` means "one or more," `*` means "zero or more."

| Pattern | Example matches |
|---------|------------|
| `[0-9]+` | `0`, `1`, `42`, `12345` |
| `[a-z]+` | `a`, `hello`, `xyz` |
| `[0-9]*` | (empty string), `0`, `42` |

`[0-9]+` works directly as the pattern for an "integer literal."


## 4. Concatenating patterns

Place patterns side by side and they mean "appear in this order."

```
[a-zA-Z_][a-zA-Z0-9_]*
```

This means:

- First character: a letter or an underscore
- The rest: zero or more letters, digits, or underscores

That's the pattern for an **identifier** (a C variable or function name). It matches `x`, `main`, `my_var`, `buf123`, and so on. It does *not* match something like `1var` that starts with a digit.


## 5. Any single character `.`

A period `.` matches **"any single character except a newline."**

```
.          ← any single non-newline character
```

Combined with `*`, it becomes "everything up to a newline."

```
"//".*     ← starting with //, everything up to the newline
```

That's the pattern for a C++-style line comment.


## 6. Whitespace and newlines

```
[ \t\n]+
```

This means "one or more consecutive whitespace, tab, or newline characters."

- ` ` is an ordinary space
- `\t` is a tab
- `\n` is a newline

Anything escaped with `\` is treated as "a single character with a special meaning." flex uses this pattern to skip whitespace.


## 7. Regex summary for Chapter 1

| Notation | Meaning |
|------|------|
| `"int"` | The literal string `int` |
| `[0-9]` | A single digit |
| `[a-zA-Z_]` | A single letter or underscore |
| `+` | One or more of the preceding pattern |
| `*` | Zero or more of the preceding pattern |
| `.` | Any single non-newline character |
| `\t` | Tab |
| `\n` | Newline |

There are other regex features — `?`, `|`, `{n,m}`, grouping `(...)`, etc. — that we don't need in Chapter 1. We'll introduce them later when they're needed.


## Next

In the next section (`02_flex.md`), we use these regular expressions to write the lexer.
