#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"

/* Generate x86-64 assembly from AST, writing to out */
void codegen(Node *program, FILE *out);

#endif
