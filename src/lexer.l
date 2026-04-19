%{
#include <string.h>
#include "ast.h"
#include "parser.tab.h"

/* Track line numbers for error messages */
int yyline = 1;
%}

%option noyywrap

%%

[ \t\r]+            { /* skip whitespace */ }
\n                  { yyline++; }
"//".*              { /* skip line comment */ }

"int"               { return T_INT; }
"char"              { return T_CHAR; }
"void"              { return T_VOID; }
"if"                { return T_IF; }
"else"              { return T_ELSE; }
"while"             { return T_WHILE; }
"return"            { return T_RETURN; }

[0-9]+              { yylval.int_val = atoi(yytext); return T_INT_LIT; }

\'\\n\'             { yylval.char_val = '\n'; return T_CHAR_LIT; }
\'\\t\'             { yylval.char_val = '\t'; return T_CHAR_LIT; }
\'\\0\'             { yylval.char_val = '\0'; return T_CHAR_LIT; }
\'\\.\'             { yylval.char_val = yytext[2]; return T_CHAR_LIT; }
\'.\'               { yylval.char_val = yytext[1]; return T_CHAR_LIT; }

\"([^"\\]|\\.)*\"   { /* strip quotes */
                      int len = strlen(yytext) - 2;
                      yylval.str_val = strndup(yytext + 1, len);
                      return T_STRING_LIT;
                    }

[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str_val = strdup(yytext); return T_IDENT; }

"=="                { return T_EQ; }
"!="                { return T_NE; }
"<="                { return T_LE; }
">="                { return T_GE; }

"+"                 { return '+'; }
"-"                 { return '-'; }
"*"                 { return '*'; }
"/"                 { return '/'; }
"%"                 { return '%'; }
"="                 { return '='; }
"<"                 { return '<'; }
">"                 { return '>'; }
"!"                 { return '!'; }
"&"                 { return '&'; }
"("                 { return '('; }
")"                 { return ')'; }
"{"                 { return '{'; }
"}"                 { return '}'; }
"["                 { return '['; }
"]"                 { return ']'; }
";"                 { return ';'; }
","                 { return ','; }

.                   { fprintf(stderr, "line %d: unexpected character '%s'\n", yyline, yytext); }

%%
