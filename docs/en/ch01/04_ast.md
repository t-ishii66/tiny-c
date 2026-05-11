# 04 — The AST and ast.h / ast.c

`ast.h` defines the node types; `ast.c` writes the functions that build nodes (constructors). Chapter 1 needs only four kinds of nodes.


## 1. Why a "tree"?

Source code is a string, which is awkward for a compiler to work with. Answering "what's the expression after `return`?" from a string means slicing and parsing every time you ask. Add expressions like `2 + 3 * 4` and it gets worse.

So we convert the source, once, into **structured data in memory**. "Inside the return-statement node is an expression node. That expression node is an integer literal, value 42." If we hold the structure, later processing becomes simple pointer-chasing.

```
"return 42;"           RETURN node
                        └─ INT_LIT node (value: 42)
```

Working with strings ends at parsing. Everything from there on (type checking, optimization, code generation) happens **on the tree**.


## 2. What "abstract" means

The "A" in AST stands for **Abstract**. The word distinguishes it from the *concrete* syntax tree (CST).

A concrete syntax tree keeps **everything** that appears in the source: whitespace, newlines, comments, the position of parentheses — all of it. `return ( 42 ) ;` and `return 42 ;` become different CSTs.

An abstract syntax tree keeps **only what affects meaning**. Both `return ( 42 ) ;` and `return 42 ;` become the same AST. Semantically, both are just "return 42." The parentheses and the amount of whitespace are information later compiler stages don't use, so we **throw them away**.

"Abstract" means "strip off the inessentials." The AST is a tree that retains only the minimum information the compiler needs.

That's why `;`, `(`, and `{` never appear as nodes in the AST. They were there to help us parse the structure of the source; once the structure is known, they're not needed.


## 3. Designing the node: one struct, or one per kind?

When expressing AST nodes in C, there are two design choices.

**Option A: a separate struct per kind**

```c
typedef struct { int value; } IntLitNode;
typedef struct { Node *expr; } ReturnNode;
typedef struct { char *name; Node *body; } FuncDefNode;
typedef struct { NodeList *stmts; } BlockNode;
```

Each kind has only the fields it needs — no waste.

**Option B: one struct that covers every kind**

```c
typedef struct Node {
    NodeKind kind;       /* tag identifying the kind */
    int int_val;         /* for INT_LIT */
    char *name;          /* for FUNC_DEF */
    Node *body;          /* for FUNC_DEF */
    Node *expr;          /* for RETURN */
    NodeList *stmts;     /* for BLOCK */
} Node;
```

`kind` tells us the type; depending on the kind, certain fields are used and others aren't. The unused fields are wasted memory.

Chapter 1 takes **Option B**. The reason is **simplicity**.

With Option A, functions that work on nodes have to "branch by type." To pass them around as `Node *`, you end up needing a common header struct or a `union`, and the code gets more complex.

Option B leaves unused fields, sure, but that's only a few dozen wasted bytes per node. The total node count for a compiler like this is in the thousands, so memory isn't an issue.

The pattern "branch on `switch (node->kind)`" is simple, and it works consistently throughout the compiler.


## 4. The four node kinds in Chapter 1

Chapter 1 needs:

| Kind | Why we define it |
|------|------------|
| `NODE_INT_LIT` | Represents an integer literal like `42` |
| `NODE_RETURN` | Represents `return expr;` |
| `NODE_FUNC_DEF` | Represents `int main() { ... }` |
| `NODE_BLOCK` | Represents the contents of `{ ... }` (a statement list) |

In later chapters we'll add `NODE_BINARY` (binary operators), `NODE_VAR_DECL` (variable declarations), `NODE_IF`, `NODE_WHILE`, and others, but four are enough for Chapter 1.


## 5. Reading ast.h

```c
#ifndef AST_H
#define AST_H

/* Node types */
typedef enum {
    NODE_INT_LIT,    /* integer literal: 42 */
    NODE_RETURN,     /* return statement */
    NODE_FUNC_DEF,   /* function definition */
    NODE_BLOCK,      /* { ... } */
} NodeKind;

/* Forward declaration */
typedef struct Node Node;
typedef struct NodeList NodeList;

/* Linked list of nodes */
struct NodeList {
    Node *node;
    NodeList *next;
};

/* AST node */
struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT: value */
    char *name;        /* NODE_FUNC_DEF: function name */
    Node *body;        /* NODE_FUNC_DEF: function body (block) */
    Node *expr;        /* NODE_RETURN: return value */
    NodeList *stmts;   /* NODE_BLOCK: list of statements */
};

/* Constructors */
Node *new_int_lit(int val);
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

/* List operations */
NodeList *new_node_list(Node *node, NodeList *next);

/* Debug output */
void print_ast(Node *node, int indent);

#endif
```

Going through it:

### `NodeKind` enum

```c
typedef enum {
    NODE_INT_LIT,
    NODE_RETURN,
    NODE_FUNC_DEF,
    NODE_BLOCK,
} NodeKind;
```

An enum that names the node kinds. Each node holds one of these values in its `kind` field.

### Forward declaration

```c
typedef struct Node Node;
typedef struct NodeList NodeList;
```

`Node` and `NodeList` reference each other, so we announce their names up front. Without this, a later struct definition writing `NodeList *stmts;` would prompt "what's `NodeList`?"

### `NodeList` struct

```c
struct NodeList {
    Node *node;
    NodeList *next;
};
```

A plain singly-linked list. `node` is the current item, `next` points to the next. We use a list rather than an array because the number of statements isn't known in advance, so it's easier to build dynamically.

### `Node` struct

```c
struct Node {
    NodeKind kind;

    int int_val;       /* NODE_INT_LIT: value */
    char *name;        /* NODE_FUNC_DEF: function name */
    Node *body;        /* NODE_FUNC_DEF: function body (block) */
    Node *expr;        /* NODE_RETURN: return value */
    NodeList *stmts;   /* NODE_BLOCK: list of statements */
};
```

This is the common struct for every node. Check `kind` to decide which fields are in use.

| kind | Fields used |
|------|--------------|
| `NODE_INT_LIT` | `int_val` |
| `NODE_RETURN` | `expr` |
| `NODE_FUNC_DEF` | `name`, `body` |
| `NODE_BLOCK` | `stmts` |

Unused fields are zero-initialized by `calloc` (see below).

### Constructor and list-operation declarations

```c
Node *new_int_lit(int val);
Node *new_return(Node *expr);
Node *new_block(NodeList *stmts);
Node *new_func_def(char *name, Node *body);

NodeList *new_node_list(Node *node, NodeList *next);

void print_ast(Node *node, int indent);
```

Declarations for the constructors of each kind. The implementations live in `ast.c`. `print_ast` is for debugging and is called by `--dump-ast`.


## 6. Reading ast.c

```c
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *new_int_lit(int val) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_INT_LIT;
    n->int_val = val;
    return n;
}

Node *new_return(Node *expr) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_RETURN;
    n->expr = expr;
    return n;
}

Node *new_block(NodeList *stmts) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_BLOCK;
    n->stmts = stmts;
    return n;
}

Node *new_func_def(char *name, Node *body) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = NODE_FUNC_DEF;
    n->name = name;
    n->body = body;
    return n;
}

NodeList *new_node_list(Node *node, NodeList *next) {
    NodeList *l = calloc(1, sizeof(NodeList));
    l->node = node;
    l->next = next;
    return l;
}
```

All constructors follow the same pattern:

1. `calloc(1, sizeof(Node))` to allocate memory (**zero-initialized**)
2. Set `kind`
3. Set the necessary fields
4. Return the pointer

Why `calloc` instead of `malloc`? `calloc` fills the allocated memory with 0. That way, **fields not used by this node's kind automatically become NULL or 0**. Safer code, no risk of forgetting to initialize something.

We don't free any memory. The compiler exits right after compiling, so the OS reclaims it all at once. When the process ends, memory is gone. Simple and fine.

### `print_ast` — the debug-printing function

```c
static void indent(int level) {
    for (int i = 0; i < level; i++) printf("  ");
}

void print_ast(Node *node, int level) {
    if (!node) return;
    indent(level);
    switch (node->kind) {
    case NODE_INT_LIT:
        printf("INT_LIT %d\n", node->int_val);
        break;
    case NODE_RETURN:
        printf("RETURN\n");
        print_ast(node->expr, level + 1);
        break;
    case NODE_FUNC_DEF:
        printf("FUNC_DEF %s\n", node->name);
        print_ast(node->body, level + 1);
        break;
    case NODE_BLOCK:
        printf("BLOCK\n");
        for (NodeList *l = node->stmts; l; l = l->next)
            print_ast(l->node, level + 1);
        break;
    }
}
```

The function that prints the AST for `--dump-ast`. It walks the tree **recursively** and prints, with indentation, the kind and contents of each node.

You can see **the compiler's main processing pattern** here:

1. Take a `Node *`
2. Branch by kind with `switch (node->kind)`
3. Recurse into child nodes (`expr`, `body`, `stmts`)

This same pattern shows up again in the next section's code generator (`codegen.c`), unchanged in shape. **Recursive tree-walking** — that's the basic motion of a compiler.


## 7. Watching the AST come together

Let's trace, for the input `int main() { return 42; }`, which parser actions fire and what tree appears in memory.

Order of token reads and action executions:

```
1. Read INT_LIT(42)
   → the expr rule's action runs
   → new_int_lit(42) is called
   → in memory: [Node A: kind=INT_LIT, int_val=42]

2. RETURN expr ';' is complete
   → the stmt rule's action runs
   → new_return(A) is called
   → in memory: [Node B: kind=RETURN, expr=&A]

3. B is added to stmts
   → the stmts rule's action runs
   → new_node_list(B, NULL) is called
   → in memory: [NodeList L: node=&B, next=NULL]

4. INT IDENT ( ) { stmts } is complete
   → the func_def rule's action runs
   → new_block(L) is called
   → in memory: [Node C: kind=BLOCK, stmts=&L]
   → new_func_def("main", C) is called
   → in memory: [Node D: kind=FUNC_DEF, name="main", body=&C]

5. D is assigned to program
   → global variable program = &D
```

The resulting tree in memory:

```
program ──→ [D: FUNC_DEF "main"]
              │
              body: [C: BLOCK]
                      │
                      stmts: [L: NodeList]
                              │
                              node: [B: RETURN]
                                      │
                                      expr: [A: INT_LIT 42]
```

That is what `--dump-ast` is really showing.

```
FUNC_DEF main      ← Node D
  BLOCK            ← Node C
    RETURN         ← Node B
      INT_LIT 42   ← Node A
```


## Next

In the next section (`05_codegen.md`) we look at the code generator that walks this tree and emits x86 assembly.
