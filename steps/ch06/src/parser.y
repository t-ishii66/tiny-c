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
    struct { char *s; int n; } str;
    Node *node;
    NodeList *list;
    Type *type;
}

%token <int_val> INT_LIT CHAR_LIT
%token <name> IDENT
%token <str> STRING_LIT
%token INT CHAR VOID RETURN IF ELSE WHILE
%token EQ_OP NE_OP LE_OP GE_OP

%type <node> top_level func_def stmt expr assign equality relational add_expr mul_expr unary primary param
%type <list> top_levels stmts params param_list args arg_list
%type <type> type

%expect 1   /* dangling-else */

%%

program
    : top_levels                            { program = new_program($1); }
    ;

top_levels
    : /* empty */                           { $$ = NULL; }
    | top_level top_levels                  { $$ = new_node_list($1, $2); }
    ;

top_level
    : func_def                              { $$ = $1; }
    | type IDENT ';'                        { $$ = new_global_var_decl($2, $1); }
    | type IDENT '[' INT_LIT ']' ';'        { $$ = new_global_var_decl($2, type_array($1->base_size, $4)); }
    ;

type
    : INT                                   { $$ = type_int(); }
    | CHAR                                  { $$ = type_char(); }
    | VOID                                  { Type *t = calloc(1, sizeof(Type)); $$ = t; }
    | INT '*'                               { $$ = type_ptr(4); }
    | CHAR '*'                              { $$ = type_ptr(1); }
    ;

func_def
    : type IDENT '(' params ')' '{' stmts '}'
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
    : type IDENT                            {
        Node *p = new_ident($2);
        p->type = $1;
        $$ = p;
    }
    ;

stmts
    : /* empty */                           { $$ = NULL; }
    | stmt stmts                            { $$ = new_node_list($1, $2); }
    ;

stmt
    : RETURN expr ';'                       { $$ = new_return($2); }
    | RETURN ';'                            { $$ = new_return(NULL); }
    | type IDENT '=' expr ';'               { $$ = new_var_decl($2, $1, $4); }
    | type IDENT '[' INT_LIT ']' ';'        { $$ = new_var_decl($2, type_array($1->base_size, $4), NULL); }
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
    | '&' unary                             { $$ = new_unary('&', $2); }
    | '*' unary                             { $$ = new_unary('*', $2); }
    ;

primary
    : INT_LIT                               { $$ = new_int_lit($1); }
    | CHAR_LIT                              { $$ = new_char_lit($1); }
    | STRING_LIT                            { $$ = new_string_lit($1.s, $1.n); }
    | IDENT                                 { $$ = new_ident($1); }
    | IDENT '(' args ')'                    { $$ = new_call($1, $3); }
    | IDENT '[' expr ']'                    { $$ = new_index(new_ident($1), $3); }
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
