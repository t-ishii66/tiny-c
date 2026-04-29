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

%type <node> func_def stmt expr add_expr mul_expr unary primary
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
    | stmts stmt        { $$ = new_node_list($2, $1); }
    ;

stmt
    : RETURN expr ';'   { $$ = new_return($2); }
    ;

expr
    : add_expr          { $$ = $1; }
    ;

add_expr
    : mul_expr                  { $$ = $1; }
    | add_expr '+' mul_expr     { $$ = new_binary('+', $1, $3); }
    | add_expr '-' mul_expr     { $$ = new_binary('-', $1, $3); }
    ;

mul_expr
    : unary                     { $$ = $1; }
    | mul_expr '*' unary        { $$ = new_binary('*', $1, $3); }
    | mul_expr '/' unary        { $$ = new_binary('/', $1, $3); }
    | mul_expr '%' unary        { $$ = new_binary('%', $1, $3); }
    ;

unary
    : primary                   { $$ = $1; }
    | '-' unary                 { $$ = new_unary('-', $2); }
    ;

primary
    : INT_LIT                   { $$ = new_int_lit($1); }
    | '(' expr ')'              { $$ = $2; }
    ;

%%
