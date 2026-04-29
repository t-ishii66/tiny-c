%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex(void);
void yyerror(const char *s) { fprintf(stderr, "parse error: %s\n", s); exit(1); }

Node *program;
%}

%union {
    int int_val;
    char *name;
    Node *node;
    NodeList *list;
}

%token <int_val> INT_LIT
%token <name> IDENT
%token INT RETURN IF ELSE WHILE
%token EQ_OP NE_OP LE_OP GE_OP

%type <node> func_def stmt expr assign equality relational add_expr mul_expr unary primary param
%type <list> stmts func_defs params param_list args arg_list

%expect 1   /* dangling-else */

%%

program
    : func_defs                             { program = new_program($1); }
    ;

func_defs
    : /* empty */                           { $$ = NULL; }
    | func_def func_defs                    { $$ = new_node_list($1, $2); }
    ;

func_def
    : INT IDENT '(' params ')' '{' stmts '}'
                                            { $$ = new_func_def($2, $4, new_block($7)); }
    ;

params
    : /* empty */                           { $$ = NULL; }
    | param_list                            { $$ = $1; }
    ;

param_list
    : param                                 { $$ = new_node_list($1, NULL); }
    | param ',' param_list                  { $$ = new_node_list($1, $3); }
    ;

param
    : INT IDENT                             { $$ = new_ident($2); }
    ;

stmts
    : /* empty */                           { $$ = NULL; }
    | stmt stmts                            { $$ = new_node_list($1, $2); }
    ;

stmt
    : RETURN expr ';'                       { $$ = new_return($2); }
    | INT IDENT '=' expr ';'                { $$ = new_var_decl($2, $4); }
    | expr ';'                              { $$ = new_expr_stmt($1); }
    | '{' stmts '}'                         { $$ = new_block($2); }
    | IF '(' expr ')' stmt                  { $$ = new_if($3, $5, NULL); }
    | IF '(' expr ')' stmt ELSE stmt        { $$ = new_if($3, $5, $7); }
    | WHILE '(' expr ')' stmt               { $$ = new_while($3, $5); }
    ;

expr
    : assign                                { $$ = $1; }
    ;

assign
    : equality                              { $$ = $1; }
    | equality '=' assign                   { $$ = new_assign($1, $3); }
    ;

equality
    : relational                            { $$ = $1; }
    | equality EQ_OP relational             { $$ = new_binary(OP_EQ, $1, $3); }
    | equality NE_OP relational             { $$ = new_binary(OP_NE, $1, $3); }
    ;

relational
    : add_expr                              { $$ = $1; }
    | relational '<' add_expr               { $$ = new_binary('<', $1, $3); }
    | relational LE_OP add_expr             { $$ = new_binary(OP_LE, $1, $3); }
    | relational '>' add_expr               { $$ = new_binary('>', $1, $3); }
    | relational GE_OP add_expr             { $$ = new_binary(OP_GE, $1, $3); }
    ;

add_expr
    : mul_expr                              { $$ = $1; }
    | add_expr '+' mul_expr                 { $$ = new_binary('+', $1, $3); }
    | add_expr '-' mul_expr                 { $$ = new_binary('-', $1, $3); }
    ;

mul_expr
    : unary                                 { $$ = $1; }
    | mul_expr '*' unary                    { $$ = new_binary('*', $1, $3); }
    | mul_expr '/' unary                    { $$ = new_binary('/', $1, $3); }
    | mul_expr '%' unary                    { $$ = new_binary('%', $1, $3); }
    ;

unary
    : primary                               { $$ = $1; }
    | '-' unary                             { $$ = new_unary('-', $2); }
    | '!' unary                             { $$ = new_unary('!', $2); }
    ;

primary
    : INT_LIT                               { $$ = new_int_lit($1); }
    | IDENT                                 { $$ = new_ident($1); }
    | IDENT '(' args ')'                    { $$ = new_call($1, $3); }
    | '(' expr ')'                          { $$ = $2; }
    ;

args
    : /* empty */                           { $$ = NULL; }
    | arg_list                              { $$ = $1; }
    ;

arg_list
    : expr                                  { $$ = new_node_list($1, NULL); }
    | expr ',' arg_list                     { $$ = new_node_list($1, $3); }
    ;

%%
