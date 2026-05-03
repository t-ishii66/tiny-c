%{
#include "ast.h"
#include "parser.tab.h"

/* Process escape sequences in char/string literals.
   Writes to dst, returns number of chars written (excluding terminator). */
static int unescape(const char *src, int len, char *dst) {
    int n = 0;
    for (int i = 0; i < len; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < len) {
            char e = src[++i];
            switch (e) {
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case '0':  c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            case '"':  c = '"';  break;
            default:   c = e;    break;
            }
        }
        dst[n++] = c;
    }
    return n;
}
%}

%option noyywrap

%%
"int"       { return INT; }
"char"      { return CHAR; }
"void"      { return VOID; }
"return"    { return RETURN; }
"if"        { return IF; }
"else"      { return ELSE; }
"while"     { return WHILE; }
[0-9]+      { yylval.int_val = atoi(yytext); return INT_LIT; }
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.name = strdup(yytext); return IDENT; }
'(\\.|[^\\'])'  {
    /* char literal */
    char buf[4];
    int n = unescape(yytext + 1, yyleng - 2, buf);
    (void)n;
    yylval.int_val = (unsigned char)buf[0];
    return CHAR_LIT;
}
\"([^"\\]|\\.)*\"  {
    /* string literal: strip surrounding quotes, unescape */
    char *buf = malloc(yyleng);
    int n = unescape(yytext + 1, yyleng - 2, buf);
    buf[n] = '\0';
    yylval.str.s = buf;
    yylval.str.n = n + 1;  /* include trailing \0 */
    return STRING_LIT;
}
"=="        { return EQ_OP; }
"!="        { return NE_OP; }
"<="        { return LE_OP; }
">="        { return GE_OP; }
"+"         { return '+'; }
"-"         { return '-'; }
"*"         { return '*'; }
"/"         { return '/'; }
"%"         { return '%'; }
"="         { return '='; }
"<"         { return '<'; }
">"         { return '>'; }
"!"         { return '!'; }
"&"         { return '&'; }
"("         { return '('; }
")"         { return ')'; }
"{"         { return '{'; }
"}"         { return '}'; }
"["         { return '['; }
"]"         { return ']'; }
";"         { return ';'; }
","         { return ','; }
[ \t\n]+    { /* skip whitespace */ }
"//".*      { /* skip line comments */ }
.           { fprintf(stderr, "unknown char: %c\n", *yytext); exit(1); }
%%
