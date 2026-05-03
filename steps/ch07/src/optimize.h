#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include <stdio.h>
#include "ast.h"

/* AST-level optimization: constant folding + algebraic simplification.
   Returns the (possibly new) optimized node. */
Node *optimize_ast(Node *node);

/* Peephole optimization at the generated-asm level.
   Reads a complete asm text from `in_buf` (length in_len) and writes
   the optimized asm to `out`. */
void peephole(const char *in_buf, int in_len, FILE *out);

#endif
