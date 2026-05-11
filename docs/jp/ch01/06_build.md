# 06 — main.c と Makefile、そして全体を組み上げる

`lexer.l`、`parser.y`、`ast.h/ast.c`、`codegen.c/codegen.h` が揃った。残りは、これらをつなぐ **エントリポイント** (`main.c`) と、ビルド手順 (`Makefile`) だ。


## 1. 役割分担

```
+---------+                                                +---------+
| 入力 .c |                                                | 出力 .s |
+----+----+                                                +----+----+
     |                                                          ^
     v                                                          |
+----------+   +-----------+   +-------+   +-----------+   +----+----+
|  lexer   |─→ |  parser   |─→ |  AST  |─→ | codegen   |─→ |   FILE  |
| (flex)   |   | (bison)   |   |       |   |           |   |  (stdout)|
+----------+   +-----------+   +-------+   +-----------+   +---------+
                  ^                            ^
                  |                            |
            +-----+----+                +------+-----+
            | main.c   |                | main.c     |
            | yyparse  |                | codegen()  |
            +----------+                +------------+
```

`main.c` は引数を見て、ファイルを開いて、`yyparse()` を呼んで、結果の AST を `codegen()` に渡す ── **外側の橋渡し** だけを担当する。


## 2. main.c を読む

```c
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "codegen.h"

extern int yyparse(void);
extern FILE *yyin;
extern Node *program;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: tinyc [--dump-ast] <file>\n");
        return 1;
    }

    int dump_ast = 0;
    char *filename = argv[1];

    if (strcmp(argv[1], "--dump-ast") == 0) {
        dump_ast = 1;
        if (argc < 3) { fprintf(stderr, "usage: tinyc --dump-ast <file>\n"); return 1; }
        filename = argv[2];
    }

    yyin = fopen(filename, "r");
    if (!yyin) { perror(filename); return 1; }

    yyparse();
    fclose(yyin);

    if (dump_ast) {
        print_ast(program, 0);
    } else {
        codegen(program, stdout);
    }
    return 0;
}
```

順を追って見ていこう。

### `extern` 宣言

```c
extern int yyparse(void);
extern FILE *yyin;
extern Node *program;
```

これは「これらは別のファイルで定義されている」という宣言だ。

- `yyparse()` は bison が生成した `parser.tab.c` で定義される関数。呼ぶと構文解析が始まる。
- `yyin` は flex が生成した `lex.yy.c` で定義されているグローバル変数。`FILE *` 型で、ここから入力を読む。
- `program` は **我々が** `parser.y` で定義したグローバル変数。構文解析が終わると AST の根ノードが入る。

`extern` は「リンク時に他のオブジェクトファイルから引っ張ってくるよ」という宣言だ。実体はない。

### 引数処理

```c
if (argc < 2) {
    fprintf(stderr, "usage: tinyc [--dump-ast] <file>\n");
    return 1;
}

int dump_ast = 0;
char *filename = argv[1];

if (strcmp(argv[1], "--dump-ast") == 0) {
    dump_ast = 1;
    if (argc < 3) { fprintf(stderr, "usage: tinyc --dump-ast <file>\n"); return 1; }
    filename = argv[2];
}
```

`./tinyc file.c` または `./tinyc --dump-ast file.c` の2通りを受け付ける。最低1引数（ファイル名）が必要。`--dump-ast` が指定されたらフラグを立て、ファイル名は2つめの引数から取る。

### 入力ファイルを開く

```c
yyin = fopen(filename, "r");
if (!yyin) { perror(filename); return 1; }
```

`yyin` に開いた `FILE *` を入れる。これだけで flex はそのファイルから入力を読むようになる。

### パース実行

```c
yyparse();
fclose(yyin);
```

`yyparse()` が呼ばれると、内部で次のことが起きる：

1. bison のループが始まる
2. flex の `yylex()` を呼んでトークンを1つ取り出す
3. 文法ルールを試して、マッチしたらアクション（`new_xxx` でノードを作る）を実行
4. これを EOF まで繰り返す
5. 最終的に `program` グローバル変数に AST が組み上がる

`yyparse()` の戻り値は成功なら 0、失敗なら 0 以外。今は構文エラーが起きたら `yyerror` で `exit(1)` してしまうので戻り値は確認していない。

### 結果の利用

```c
if (dump_ast) {
    print_ast(program, 0);
} else {
    codegen(program, stdout);
}
return 0;
```

`--dump-ast` が指定されていれば AST を表示。そうでなければアセンブリを `stdout` に出力する。アセンブリを `stdout` に書くから、ユーザは `./tinyc file.c > file.s` のようにリダイレクトする。


## 3. main.c はこれ以降、ほぼ変わらない

`main.c` は **第6章まで本質的に変わらない**。やっているのが「引数処理 → `yyparse` 呼び出し → AST 出力」という外側の段取りだけだからだ。

肉付けが起きるのは `lexer.l`（新しいトークン）、`parser.y`（新しい文法ルール）、`ast.h/ast.c`（新しいノード種別）、`codegen.c`（新しいコード生成パターン）の4ファイル。**この４ファイルでコンパイラを拡張していく**。


## 4. Makefile を読む

> 以降の Makefile 解説は **雰囲気がわかれば十分**。興味がなければ 6. ビルド & 実行 へ進んでも良い。実務でゼロから Makefile を書く機会は、最近ではそれほど多くない。本書で `Makefile` を採用しているのも、依存関係を簡潔に表現できる教材として優れているという理由だ。本筋（コンパイラの中身）から脇道に逸れすぎないように、「こういう仕組みでビルドが回っている」と眺める程度で次に進んでよい。

`Makefile` は `make` コマンドが読むビルド手順書だ。何を作るには何が必要か、その依存関係を記述する。

```makefile
CC      = gcc
CFLAGS  = -Wall -g -Isrc -Ibuild
BUILD   = build

SRCS    = src/ast.c src/codegen.c src/main.c
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o

tinyc: $(OBJS)
	$(CC) -o $@ $^

$(BUILD)/parser.tab.c $(BUILD)/parser.tab.h: src/parser.y | $(BUILD)
	bison -d -o $(BUILD)/parser.tab.c $<

$(BUILD)/lex.yy.c: src/lexer.l $(BUILD)/parser.tab.h | $(BUILD)
	flex -o $@ $<

$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) tinyc

.PHONY: clean
```

`make` の構文は独特だが、考え方はシンプルだ。各ルールは：

```
ターゲット: 依存ファイル
	コマンド
```

「ターゲットを作るには、これらの依存ファイルが必要。それらが揃ったら、このコマンドを実行して」という意味。コマンド行の頭は**タブ文字**でなければいけない（スペースではない、はまりやすい罠）。

### 変数定義

```makefile
CC      = gcc
CFLAGS  = -Wall -g -Isrc -Ibuild
BUILD   = build

SRCS    = src/ast.c src/codegen.c src/main.c
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o
```

- `CC` はコンパイラ。
- `CFLAGS` はコンパイル時のオプション。`-Wall` で警告を全部出す、`-g` でデバッグ情報を含める、`-Isrc -Ibuild` でヘッダ検索パスに `src/` と `build/` を追加。
- `BUILD` はビルド成果物の置き場所。`build/` ディレクトリ。
- `SRCS` は手書きの C ソースのリスト、`OBJS` は最終リンクで使うオブジェクトファイルのリスト。
- `\` は行の継続。

### tinyc 本体のリンク

```makefile
tinyc: $(OBJS)
	$(CC) -o $@ $^
```

「`tinyc` を作るには `$(OBJS)` がすべて必要」。コマンドは「`gcc -o tinyc <全 .o ファイル>`」。

`$@` はターゲット名（ここでは `tinyc`）、`$^` は依存ファイルすべて（ここでは `$(OBJS)` の中身全部）を表す `make` の自動変数。

### bison でパーサを生成

```makefile
$(BUILD)/parser.tab.c $(BUILD)/parser.tab.h: src/parser.y | $(BUILD)
	bison -d -o $(BUILD)/parser.tab.c $<
```

`build/parser.tab.c` と `build/parser.tab.h` を作るには `src/parser.y` が必要。コマンドは bison を `-d`（ヘッダも生成）と `-o`（出力先指定）で呼び出す。

`|` の後ろの `$(BUILD)` は **order-only prerequisite**。「`build/` ディレクトリがなければ作るが、その作成日時はターゲットの再ビルド判定に使わない」という指定。これがないと、`build/` ディレクトリの mtime が変わるたびに bison が再実行されて鬱陶しい。

`$<` は「最初の依存ファイル」を表す自動変数（ここでは `src/parser.y`）。

### flex でレキサを生成

```makefile
$(BUILD)/lex.yy.c: src/lexer.l $(BUILD)/parser.tab.h | $(BUILD)
	flex -o $@ $<
```

`build/lex.yy.c` を作るには `src/lexer.l` と `build/parser.tab.h` が必要。**parser.tab.h が必須**なのは、lexer.l の中で `#include "parser.tab.h"` しているから。bison が先に走ってヘッダができてからでないと、flex の出力をコンパイルできない。

依存関係を正しく書くと、`make` がこの順番を自動で守ってくれる。

### 個別の .o コンパイルルール

```makefile
$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
```

3つのコンパイルルール。

- `lex.yy.o` には `-Wno-unused-function` を追加。flex が生成するコードに使われない関数があり、警告が出るので抑制する。
- `parser.tab.o` は普通にコンパイル。
- `%` はワイルドカード。`build/foo.o` は `src/foo.c` から作る、というパターンルール。`ast.o`、`codegen.o`、`main.o` はこれで処理される。

### ディレクトリ作成と clean

```makefile
$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) tinyc

.PHONY: clean
```

- `build/` ディレクトリを作るルール。
- `clean` ターゲットでビルド成果物を全部消す。
- `.PHONY: clean` は「`clean` というファイルが存在しても無視して、必ずコマンドを実行せよ」という宣言。


## 5. ビルド依存グラフ

```
src/parser.y ─→ build/parser.tab.c ─→ build/parser.tab.o ──┐
            └─→ build/parser.tab.h ─┐                        │
                                    │                        │
src/lexer.l ────────────────────────┴→ build/lex.yy.c ─→ build/lex.yy.o ──┤
                                                                          │
src/ast.c    ─────────────────────────────────────→ build/ast.o ─────────┤
                                                                          │
src/codegen.c ────────────────────────────────────→ build/codegen.o ─────┤
                                                                          │
src/main.c   ─────────────────────────────────────→ build/main.o ────────┤
                                                                          ↓
                                                                       tinyc
```

`make` はこの依存関係を読んで、必要なものから順にコマンドを実行する。変更がなければスキップされる。


## 6. ビルド & 実行

ファイルがすべて揃ったら、ビルドできる。

```bash
$ make
mkdir -p build
gcc -Wall -g -Isrc -Ibuild -c -o build/ast.o src/ast.c
gcc -Wall -g -Isrc -Ibuild -c -o build/codegen.o src/codegen.c
gcc -Wall -g -Isrc -Ibuild -c -o build/main.o src/main.c
bison -d -o build/parser.tab.c src/parser.y
gcc -Wall -g -Isrc -Ibuild -c -o build/parser.tab.o build/parser.tab.c
flex -o build/lex.yy.c src/lexer.l
gcc -Wall -g -Isrc -Ibuild -Wno-unused-function -c -o build/lex.yy.o build/lex.yy.c
gcc -o tinyc build/ast.o build/codegen.o build/main.o build/parser.tab.o build/lex.yy.o
```

`tinyc` ができた。動かしてみる。

```bash
$ echo 'int main() { return 42; }' > test.c

$ ./tinyc --dump-ast test.c
FUNC_DEF main
  BLOCK
    RETURN
      INT_LIT 42

$ ./tinyc test.c
  .text
  .globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $42, %eax
  leave
  ret

$ ./tinyc test.c > test.s
$ gcc -o test test.s
$ ./test
$ echo $?
42
```

**動いた**。コンパイラが実行ファイルを生成した。

（注: 実行は x86-64 Linux 環境で。Mac や Windows 上ではコンパイラ自体は動くが、出力アセンブリのアセンブル＆実行には別環境が要る。）


## 7. 振り返り

第1章で作ったコンパイラの**できること**:

- 関数定義 `int main() { ... }` の解析
- `return` 文の処理
- 整数リテラルを `%eax` にセットして関数から戻る
- AST を `--dump-ast` で表示

**できないこと**: 算術演算、変数、`if`/`while`、引数や複数関数、文字列、配列、ポインタ、外部関数呼び出し ── 第2章以降で順次追加。

ファイル一覧:

| ファイル | 役割 |
|---------|------|
| `ast.h` | ノードの型定義 |
| `ast.c` | ノード生成とデバッグ出力 |
| `lexer.l` | 文字列をトークンに分解 |
| `parser.y` | トークンから AST を組み立てる |
| `codegen.h` | コード生成のインターフェース |
| `codegen.c` | AST からアセンブリを出力 |
| `main.c` | 全体をつなぐ |
| `Makefile` | ビルド手順 |

第2章以降はこの骨格に lexer / parser / AST / codegen の肉を足していくだけになる。


## 8. 次へ

次の章では、`return 2 + 3 * 4;` のような算術式を扱えるようにする。優先順位を文法でどう表現するか、複数の中間値をどう管理するかが主題。
