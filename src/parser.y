%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

extern int yylex(void);
extern int yyline;
extern char *yytext;

void yyerror(const char *msg) {
    fprintf(stderr, "line %d: %s\n", yyline, msg);
    exit(1);
}

/* Result of parsing */
Node *parse_result = NULL;

/* Helper: reverse a NodeList (bison builds lists in reverse) */
static NodeList *reverse_list(NodeList *list) {
    NodeList *prev = NULL;
    while (list) {
        NodeList *next = list->next;
        list->next = prev;
        prev = list;
        list = next;
    }
    return prev;
}
%}

%union {
    int int_val;
    char char_val;
    char *str_val;
    Node *node;
    NodeList *list;
    Type type;
}

/* Keywords */
%token T_INT T_CHAR T_VOID T_IF T_ELSE T_WHILE T_RETURN

/* Literals and identifiers */
%token <int_val>  T_INT_LIT
%token <char_val> T_CHAR_LIT
%token <str_val>  T_STRING_LIT
%token <str_val>  T_IDENT

/* Multi-character operators */
%token T_EQ T_NE T_LE T_GE

/* Non-terminals */
%type <node> program toplevel func_def
%type <node> stmt expr assign_expr
%type <node> eq_expr rel_expr add_expr mul_expr unary_expr postfix_expr primary_expr
%type <node> var_decl_stmt
%type <list> toplevel_list stmt_list param_list param_list_inner arg_list arg_list_inner
%type <node> param
%type <type> type_spec

/* Resolve dangling else */
%nonassoc T_LOWER_THAN_ELSE
%nonassoc T_ELSE

%%

program
    : toplevel_list         { parse_result = new_node(ND_PROGRAM);
                              parse_result->children = reverse_list($1); }
    ;

toplevel_list
    : toplevel              { $$ = new_node_list($1, NULL); }
    | toplevel_list toplevel { $$ = new_node_list($2, $1); }
    ;

toplevel
    : func_def              { $$ = $1; }
    | type_spec T_IDENT ';' { /* global variable */
                              $$ = new_node(ND_VAR_DECL);
                              $$->type = $1;
                              $$->name = $2; }
    | type_spec '*' T_IDENT ';'
                            { $$ = new_node(ND_VAR_DECL);
                              if ($1 == TY_INT) $$->type = TY_PTR_INT;
                              else $$->type = TY_PTR_CHAR;
                              $$->name = $3; }
    ;

func_def
    : type_spec T_IDENT '(' param_list ')' '{' stmt_list '}'
                            { $$ = new_node(ND_FUNC_DEF);
                              $$->type = $1;
                              $$->name = $2;
                              $$->children = reverse_list($4);
                              $$->body = new_node(ND_BLOCK);
                              $$->body->children = reverse_list($7); }
    | type_spec '*' T_IDENT '(' param_list ')' '{' stmt_list '}'
                            { $$ = new_node(ND_FUNC_DEF);
                              if ($1 == TY_INT) $$->type = TY_PTR_INT;
                              else $$->type = TY_PTR_CHAR;
                              $$->name = $3;
                              $$->children = reverse_list($5);
                              $$->body = new_node(ND_BLOCK);
                              $$->body->children = reverse_list($8); }
    ;

param_list
    : /* empty */           { $$ = NULL; }
    | param_list_inner      { $$ = $1; }
    ;

param_list_inner
    : param                 { $$ = new_node_list($1, NULL); }
    | param_list_inner ',' param { $$ = new_node_list($3, $1); }
    ;

param
    : type_spec T_IDENT     { $$ = new_node(ND_PARAM);
                              $$->type = $1;
                              $$->name = $2; }
    | type_spec '*' T_IDENT { $$ = new_node(ND_PARAM);
                              if ($1 == TY_INT) $$->type = TY_PTR_INT;
                              else $$->type = TY_PTR_CHAR;
                              $$->name = $3; }
    ;

type_spec
    : T_INT                 { $$ = TY_INT; }
    | T_CHAR                { $$ = TY_CHAR; }
    | T_VOID                { $$ = TY_VOID; }
    ;

stmt_list
    : /* empty */           { $$ = NULL; }
    | stmt_list stmt        { $$ = new_node_list($2, $1); }
    ;

stmt
    : expr ';'              { $$ = new_node(ND_EXPR_STMT);
                              $$->lhs = $1; }
    | T_RETURN expr ';'     { $$ = new_node(ND_RETURN);
                              $$->lhs = $2; }
    | T_RETURN ';'          { $$ = new_node(ND_RETURN); }
    | '{' stmt_list '}'     { $$ = new_node(ND_BLOCK);
                              $$->children = reverse_list($2); }
    | T_IF '(' expr ')' stmt  %prec T_LOWER_THAN_ELSE
                            { $$ = new_node(ND_IF);
                              $$->lhs = $3;
                              $$->rhs = $5; }
    | T_IF '(' expr ')' stmt T_ELSE stmt
                            { $$ = new_node(ND_IF);
                              $$->lhs = $3;
                              $$->rhs = $5;
                              $$->extra = $7; }
    | T_WHILE '(' expr ')' stmt
                            { $$ = new_node(ND_WHILE);
                              $$->lhs = $3;
                              $$->rhs = $5; }
    | var_decl_stmt         { $$ = $1; }
    ;

var_decl_stmt
    : type_spec T_IDENT '=' expr ';'
                            { $$ = new_node(ND_VAR_DECL);
                              $$->type = $1;
                              $$->name = $2;
                              $$->lhs = $4; }
    | type_spec '*' T_IDENT '=' expr ';'
                            { $$ = new_node(ND_VAR_DECL);
                              if ($1 == TY_INT) $$->type = TY_PTR_INT;
                              else $$->type = TY_PTR_CHAR;
                              $$->name = $3;
                              $$->lhs = $5; }
    | type_spec T_IDENT '[' T_INT_LIT ']' ';'
                            { $$ = new_node(ND_ARRAY_DECL);
                              $$->type = $1;
                              $$->name = $2;
                              $$->array_size = $4; }
    ;

/* Expressions: lowest to highest precedence */

expr
    : assign_expr           { $$ = $1; }
    ;

assign_expr
    : eq_expr               { $$ = $1; }
    | unary_expr '=' assign_expr
                            { $$ = new_binary(ND_ASSIGN, $1, $3); }
    ;

eq_expr
    : rel_expr              { $$ = $1; }
    | eq_expr T_EQ rel_expr { $$ = new_binary(ND_EQ, $1, $3); }
    | eq_expr T_NE rel_expr { $$ = new_binary(ND_NE, $1, $3); }
    ;

rel_expr
    : add_expr              { $$ = $1; }
    | rel_expr '<' add_expr { $$ = new_binary(ND_LT, $1, $3); }
    | rel_expr '>' add_expr { $$ = new_binary(ND_GT, $1, $3); }
    | rel_expr T_LE add_expr { $$ = new_binary(ND_LE, $1, $3); }
    | rel_expr T_GE add_expr { $$ = new_binary(ND_GE, $1, $3); }
    ;

add_expr
    : mul_expr              { $$ = $1; }
    | add_expr '+' mul_expr { $$ = new_binary(ND_ADD, $1, $3); }
    | add_expr '-' mul_expr { $$ = new_binary(ND_SUB, $1, $3); }
    ;

mul_expr
    : unary_expr            { $$ = $1; }
    | mul_expr '*' unary_expr { $$ = new_binary(ND_MUL, $1, $3); }
    | mul_expr '/' unary_expr { $$ = new_binary(ND_DIV, $1, $3); }
    | mul_expr '%' unary_expr { $$ = new_binary(ND_MOD, $1, $3); }
    ;

unary_expr
    : postfix_expr          { $$ = $1; }
    | '-' unary_expr        { $$ = new_unary(ND_NEG, $2); }
    | '!' unary_expr        { $$ = new_unary(ND_NOT, $2); }
    | '*' unary_expr        { $$ = new_unary(ND_DEREF, $2); }
    | '&' unary_expr        { $$ = new_unary(ND_ADDR, $2); }
    ;

postfix_expr
    : primary_expr          { $$ = $1; }
    | postfix_expr '[' expr ']'
                            { $$ = new_binary(ND_INDEX, $1, $3); }
    | T_IDENT '(' arg_list ')'
                            { $$ = new_call($1, reverse_list($3)); }
    ;

primary_expr
    : T_INT_LIT             { $$ = new_int_lit($1); }
    | T_CHAR_LIT            { $$ = new_char_lit($1); }
    | T_STRING_LIT          { $$ = new_string_lit($1); }
    | T_IDENT               { $$ = new_ident($1); }
    | '(' expr ')'          { $$ = $2; }
    ;

arg_list
    : /* empty */           { $$ = NULL; }
    | arg_list_inner        { $$ = $1; }
    ;

arg_list_inner
    : expr                  { $$ = new_node_list($1, NULL); }
    | arg_list_inner ',' expr { $$ = new_node_list($3, $1); }
    ;

%%
