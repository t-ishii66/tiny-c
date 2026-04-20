# 章2: 字句解析 — flex

## この章で学ぶこと

ソースコードは人間にとっては意味のある文章だが、コンパイラにとっては**ただの文字の並び**だ。字句解析（lexical analysis）は、この文字の並びから意味のある単位——**トークン**——を切り出す最初のステップである。

```
"return 2 + 3 * 4;"     ← ただの文字列（22文字）
         ↓ 字句解析
T_RETURN  T_INT_LIT(2)  '+'  T_INT_LIT(3)  '*'  T_INT_LIT(4)  ';'
                         ↑ 意味のある7つの単位
```

tiny-c では、字句解析器（lexer）を **flex** というツールで生成する。

## flex の仕組み

flex のファイル（`src/lexer.l`）は3つのセクションからなる:

```
%{ Cコード（ヘッダ等） %}
%option ...
%%
パターン    { アクション }
パターン    { アクション }
...
%%
```

`%%` で区切られた中央のセクションが本体だ。左側に**正規表現パターン**、右側にマッチしたときの **C コード（アクション）**を書く。

flex はこのファイルから C のソースコード（`lex.yy.c`）を自動生成する。生成された関数 `yylex()` は、呼ばれるたびに入力から次のトークンを1つ切り出して返す。

## トークンの種類

tiny-c のトークンは大きく4種類に分かれる:

### 1. キーワード

```
"int"               { return T_INT; }
"char"              { return T_CHAR; }
"void"              { return T_VOID; }
"if"                { return T_IF; }
"else"              { return T_ELSE; }
"while"             { return T_WHILE; }
"return"            { return T_RETURN; }
```

固定の文字列にマッチし、対応するトークン定数を返す。`T_INT`, `T_RETURN` などのトークン名は bison が自動生成する（章3で説明）。

### 2. リテラル（値を持つトークン）

```
[0-9]+              { yylval.int_val = atoi(yytext); return T_INT_LIT; }
```

この1行を分解する:

| 要素 | 意味 |
|------|------|
| `[0-9]+` | 正規表現。1文字以上の数字列にマッチ |
| `yytext` | マッチした文字列。例: `"42"` |
| `atoi(yytext)` | 文字列 `"42"` を整数 `42` に変換 |
| `yylval.int_val` | パーサに渡す値の格納場所 |
| `return T_INT_LIT` | 「整数リテラル」というトークンの種類を返す |

トークンには**種類**と**値**の2つがある。`return` で種類を返し、`yylval` で値を渡す。

文字リテラルも同様:

```
\'.\'               { yylval.char_val = yytext[1]; return T_CHAR_LIT; }
\'\\n\'             { yylval.char_val = '\n'; return T_CHAR_LIT; }
```

`'a'` にマッチすると、`yytext` は `"'a'"` (3文字) になる。`yytext[1]` で引用符の中身 `'a'` を取り出す。`'\n'` のようなエスケープは専用のルールで処理する。

文字列リテラル:

```
\"([^"\\]|\\.)*\"   { int len = strlen(yytext) - 2;
                      yylval.str_val = strndup(yytext + 1, len);
                      return T_STRING_LIT; }
```

`"hello"` にマッチすると、前後のダブルクォートを除いた `hello` を `yylval.str_val` にコピーする。

### 3. 識別子

```
[a-zA-Z_][a-zA-Z0-9_]*  { yylval.str_val = strdup(yytext); return T_IDENT; }
```

英字またはアンダースコアで始まり、英数字・アンダースコアが続く文字列。変数名や関数名がこれにマッチする。`strdup` で文字列をコピーして `yylval` に渡す。

### 4. 演算子・記号

```
"=="                { return T_EQ; }
"!="                { return T_NE; }
"<="                { return T_LE; }
">="                { return T_GE; }

"+"                 { return '+'; }
"-"                 { return '-'; }
"*"                 { return '*'; }
";"                 { return ';'; }
```

1文字の演算子は文字コードをそのまま返す（`'+'` は整数 43）。`==` のような2文字の演算子は専用のトークン定数 `T_EQ` を返す。

## キーワードと識別子の区別

`int` はキーワードか、変数名か？ flex は**先に書かれたルールを優先する**:

```
"int"                       { return T_INT; }        ← 先にマッチ
[a-zA-Z_][a-zA-Z0-9_]*     { return T_IDENT; }      ← "int" 以外の識別子
```

入力が `int` なら、両方のパターンにマッチするが、先に書かれた `"int"` ルールが勝つ。`integer` のような変数名は `"int"` にはマッチしないので、下の識別子ルールにマッチする（flex は**最長一致**を優先する）。

## 読み飛ばし

空白やコメントはトークンではない。アクションで何も返さなければ、flex は次のマッチに進む:

```
[ \t\r]+            { /* skip whitespace */ }
\n                  { yyline++; }
"//".*              { /* skip line comment */ }
```

改行は読み飛ばしつつ行番号 `yyline` を数える。エラーメッセージで行番号を表示するためだ。

## yylval — パーサへの値の受け渡し

`yylval` はパーサとレキサが共有する共用体（union）だ。定義は `parser.y` にある:

```c
%union {
    int int_val;       // 整数リテラルの値
    char char_val;     // 文字リテラルの値
    char *str_val;     // 文字列リテラル・識別子の名前
    Node *node;        // ASTノード（パーサ側で使う）
    NodeList *list;    // ノードリスト（パーサ側で使う）
    Type type;         // 型（パーサ側で使う）
}
```

レキサは `int_val`, `char_val`, `str_val` の3つを使い、パーサは `node`, `list`, `type` を使う。どのメンバを使うかは `%token` 宣言で結びつける（章3で説明）。

## 具体例: トークン化の過程

入力 `return 2 + 3 * 4;` が `yylex()` の呼び出しごとにどうトークン化されるか:

```
呼び出し   yytext      yylval          返り値
───────────────────────────────────────────────
1回目    "return"    (なし)          T_RETURN
         " "        (読み飛ばし)
2回目    "2"        .int_val = 2    T_INT_LIT
         " "        (読み飛ばし)
3回目    "+"        (なし)          '+'
         " "        (読み飛ばし)
4回目    "3"        .int_val = 3    T_INT_LIT
         " "        (読み飛ばし)
5回目    "*"        (なし)          '*'
         " "        (読み飛ばし)
6回目    "4"        .int_val = 4    T_INT_LIT
7回目    ";"        (なし)          ';'
8回目    (EOF)                      0 (入力終了)
```

パーサは `yylex()` を繰り返し呼び、トークンを1つずつ受け取る。これがパーサの入力になる。

## ビルドの流れ

```
src/lexer.l  →[flex]→  build/lex.yy.c  →[gcc]→  lex.yy.o
```

Makefile の該当部分:

```makefile
$(BUILDDIR)/lex.yy.c: $(SRCDIR)/lexer.l $(BUILDDIR)/parser.tab.h
	$(LEX) -o $@ $<
```

flex が生成した `lex.yy.c` は `parser.tab.h` を include する。`parser.tab.h` には `T_INT`, `T_RETURN` などのトークン定数の定義が入っている。つまり、**トークン名は bison 側で定義し、flex 側で使う**。

## この章の要点

1. lexer は文字列を**トークン列**に分解する。トークンには**種類**（return 値）と**値**（yylval）がある
2. flex のルールは**正規表現 + アクション**。先に書いたルールが優先される
3. 空白・コメントは読み飛ばす。lexer が処理するので、パーサは空白を気にしなくてよい
4. `yylval` がレキサからパーサへの値の受け渡し窓口になる

## 次の章へ

lexer がトークン列を作った。しかしトークン列はまだ**平ら**だ。`2 + 3 * 4` がトークン7個に分かれただけで、`*` が `+` より先に計算されるべきだという構造は見えない。次の章で、bison がトークン列から**木構造**を組み立てる仕組みを見る。
