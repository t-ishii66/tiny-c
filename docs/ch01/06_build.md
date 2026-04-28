# 06 — main.c と Makefile、そして全体を組み上げる

ここまでで、`lexer.l`、`parser.y`、`ast.h/ast.c`、`codegen.c/codegen.h` が揃った。あとはこれらをつなぐ**エントリポイント**と、ビルド手順を記述する **Makefile** が要る。

このサブ章で、最後の2ファイルを書き上げ、コンパイラを実際に動かす。


## 1. 役割分担を整理する

すでに何度か見てきたが、各ファイルの役割を最終確認しよう。

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

`main.c` は司令塔だ。引数を見て、ファイルを開いて、`yyparse()` を呼んで、その結果（AST）を `codegen()` に渡す。それだけ。

ロジックの中身は他のファイルに任せ、**main.c は最も外側の橋渡し**を担う。


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

簡素な手書き処理だが、第1章のコンパイラには十分だ。

### 入力ファイルを開く

```c
yyin = fopen(filename, "r");
if (!yyin) { perror(filename); return 1; }
```

`yyin` に開いた `FILE *` を入れる。これだけで flex はそのファイルから入力を読むようになる。`stdin` でなく特定ファイルを処理させたいときの定石だ。

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

`yyparse()` の戻り値は成功なら 0、失敗なら 0 以外。今は構文エラーが起きたら `yyerror` で `exit(1)` してしまうので戻り値は確認していない。本格的なエラー処理は後の改善ポイントだ。

### 結果の利用

```c
if (dump_ast) {
    print_ast(program, 0);
} else {
    codegen(program, stdout);
}
return 0;
```

`--dump-ast` が指定されていれば AST を表示。そうでなければアセンブリを `stdout` に出力する。アセンブリを `stdout` に書くから、ユーザは `./tinyc file.c > file.s` のようにリダイレクトする。これは UNIX らしい設計だ。


## 3. main.c はこれ以降、ほぼ変わらない

これが第1章で書く最後の C コードだ。そして驚くべきことに、**`main.c` は第6章まで本質的に変わらない**。

なぜか。`main.c` がやっているのは「外側の段取り」だけだからだ。

- 引数を見る
- ファイルを開く
- `yyparse` を呼ぶ
- AST を出力する

これは言語の機能が増えても変わらない。第2章で四則演算を導入しても、第3章で変数を導入しても、`main.c` の役割は同じ。

肉付けが起きるのは：

- `lexer.l`（新しいトークン）
- `parser.y`（新しい文法ルール）
- `ast.h/ast.c`（新しいノード種別）
- `codegen.c`（新しいコード生成パターン）

の4ファイルだ。**コンパイラの成長は、この4つで起きる**。`main.c` は橋渡し役として安定している。


## 4. Makefile を読む

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


## 5. ビルド依存グラフを見る

`Makefile` の中身を依存グラフで描くとこうなる。

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

`make` はこの依存関係を読んで、必要なものから順にコマンドを実行する。`src/parser.y` を変更したら parser.tab.c の再生成、それを使う lex.yy.c の再生成、`.o` の再コンパイル、最後にリンクが連鎖的に走る。

逆に、変更がなければビルドはスキップされる。これが make の力だ。


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

**動いた**。本物のコンパイラが、本物の実行ファイルを生成した。

（注：実行は x86-64 Linux 環境で。Mac や Windows 上では、コンパイラ自体は動くが、出力されたアセンブリのアセンブル＆実行は別環境が要る。）


## 7. 振り返り：何ができて、何ができないか

第1章で作ったコンパイラの**できること**：

- 関数定義 `int main() { ... }` の解析
- `return` 文の処理
- 整数リテラルを `%eax` にセットして関数から戻る
- AST を `--dump-ast` で表示

**できないこと**：

- 算術演算（`+`、`-`、`*`、`/`）
- 変数（`int x = 1;`）
- `if` や `while`
- 関数の引数や複数の関数
- 文字列、配列、ポインタ
- `printf` などの外部関数呼び出し

これらは全部、第2章以降で順番に追加していく。

ファイル別の行数：

| ファイル | 行数 | 役割 |
|---------|-----|------|
| `ast.h` | 38 | ノードの型定義 |
| `ast.c` | 62 | ノード生成とデバッグ出力 |
| `lexer.l` | 19 | 文字列をトークンに分解 |
| `parser.y` | 42 | トークンから AST を組み立てる |
| `codegen.h` | 8 | コード生成のインターフェース |
| `codegen.c` | 42 | AST からアセンブリを出力 |
| `main.c` | 33 | 全体をつなぐ |
| `Makefile` | 30 | ビルド手順 |

合計 **約270行**。これがコンパイラだ。

そして、これは**一度ちゃんと作ってしまえば、第6章までのコンパイラの骨格として最後まで通用する**。第2章以降は、この骨格に肉を足していくだけだ。flex のルールが少し増え、bison のルールが少し増え、AST のノード種別が少し増え、`codegen` の switch case が少し増える。それだけ。


## 8. 第1章で身につけたもの

ここまで読んできた読者は、もう次のことができるようになっている。

- **正規表現**を読み、書ける（`[0-9]+`、`[a-zA-Z_]...` の意味が分かる）
- **flex** がレキサを生成する仕組みを理解している（`.l` の3区画、`yytext`、`yylval`）
- **bison** がパーサを生成する仕組みを理解している（`%union`、`%token`、ルールとアクション、`$1`/`$$`）
- **AST** の概念と、Cでのデータ構造としての実装を知っている
- **x86 アセンブリ**の最小限を読める（`movl`、`pushq`、`leave`、`ret`、レジスタ、AT&T 構文）
- **Makefile** で flex/bison を使ったビルドを記述できる
- **コンパイラ全体の流れ**（lexer → parser → AST → codegen）を理解している

第2章以降では、新しい機能ごとに lexer/parser/AST/codegen がどう変わるかだけを追っていく。**骨格はもう頭の中にある**。だから先の章はずっとテンポよく進める。


## 9. 第2章への橋渡し

今の `tinyc` は固定値しか返せない。だが、まもなく**計算ができるコンパイラ**になる。

```c
int main() {
    return 2 + 3 * 4;
}
```

このプログラムをコンパイルすると、終了コードは何になるべきか。`2 + 3 * 4 = 20`？　それとも `2 + 3 * 4 = 14`？

正解は **14**。「掛け算を先に計算する」というルールがあるからだ。

このルールを、コンパイラはどうやって知るのか。第2章で見るのは、**bison の文法規則の書き方ひとつで、演算子の優先順位が自然と表現できる**という、ちょっとした魔法だ。

骨格はもうある。次は機能を足していこう。
