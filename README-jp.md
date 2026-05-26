<img src="docs/images/top.png" width="800">

# tiny-c — A C subset compiler, built step by step

[English](README.md) | **日本語**

C言語の超サブセットで書かれたソースコードをコンパイルする、学習用Cコンパイラ。

コンパイラのソースコードは約1500行で、C言語で書かれている。また C言語の知識だけでtiny-cが理解できるようにドキュメントを整備した。第一章から読み進めることで、自然と約1500行のコード内容を把握できるようになっている。

## 何ができるか

```c
int fib(int n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);   // → 55
}
```

tiny-c はこのコードを x86-64 アセンブリにコンパイルする。再帰、ループ、ポインタ、配列、関数呼び出しに対応。

## ドキュメント

このプロジェクトの主な成果物はドキュメントだ。各章で**完全に動く小さなコンパイラ**を提示し、章が進むごとに言語機能が増えてコンパイラが育っていく構成になっている。全7章。

| 章 | コンパイルできるようになるもの | 焦点 |
|---|------|------|
| [章1: 42を返すだけのコンパイラ](docs/jp/ch01/00_overview.md) | `int main() { return 42; }` | パイプライン全体（lexer→parser→AST→codegen→build）の土台 |
| [章2: 電卓を作る](docs/jp/ch02/00_overview.md) | `return 2 + 3 * 4;`（四則演算と優先順位） | 文法が優先順位を表現する仕組み、スタックを使った中間値の管理 |
| [章3: 変数](docs/jp/ch03/00_overview.md) | `int x = 1; int y = 2; return x + y;` | スタックフレーム上のアドレスとして変数を実現、シンボルテーブル |
| [章4: 分岐とループ](docs/jp/ch04/00_overview.md) | `if/else` `while` と比較演算子（階乗計算など） | CPU は条件ジャンプしか知らない、ラベル生成 |
| [章5: 関数を呼ぶ・作る](docs/jp/ch05/00_overview.md) | 関数定義・引数・再帰・`printf` 呼び出し | System V AMD64 ABI、レジスタ渡し、16バイトスタックアライメント |
| [章6: ポインタと配列](docs/jp/ch06/00_overview.md) | `char *s = "hello"; printf("%s\n", s);`、ポインタ・配列・グローバル変数 | lvalue/rvalue の二面性、`gen_addr` 関数、`&` と `*` の対称性 |
| [章7: 最適化・バックパッチ・スコープ](docs/jp/ch07/00_overview.md) | `{ int x=1; }{ int x=2; }`（ブロックスコープ） | AST 最適化（定数畳み込み + 代数的単純化）、バックパッチで Phase 1 を消して codegen を単一パスに、その上にブロックスコープ（`locals` 切り戻し + スロット再利用）、ピープホール最適化 |

各章は `docs/jp/chNN/` フォルダに分かれ、`00_overview.md` から始まって `01_*.md` `02_*.md` ... と複数のサブファイルで構成されている。各章のソースコードは `steps/chNN/` 配下に置かれており、実際にビルド・実行可能な最小構成として保存されている。tiny-c の最終形は **`steps/ch07/src/`** にあり、トップレベルの `Makefile` はこれを直接ビルドする。

## ビルド・実行

前提: gcc, flex, bison, make

```bash
make                    # ビルド
./tinyc hello.c         # アセンブリを stdout に出力
./tinyc --dump-ast hello.c  # AST を表示

# 実行ファイルを作る (x86-64 Linux)
./tinyc hello.c > hello.s
gcc -o hello hello.s
./hello
```

## テスト

```bash
make test
```

`test/cases/*.c` 配下の機能チェック用ソースを順に `./tinyc` でアセンブリにコンパイルし、各ファイルの結果を検証する。各テストケースの先頭にコメントで `// expect: <終了コード>` や `// output: <stdout>` を書いておき、それと一致するか確認する形式。

- **x86-64 Linux 環境**: アセンブリを `gcc` でリンク・実行し、終了コードと stdout まで比較する。
- **それ以外の環境** (Mac など): 実行はスキップし、「アセンブリ生成が成功するか」だけを確認する。

ケースは章番号プレフィックス（`01_*.c` 〜 `07_*.c`）で機能ごとに整理されている。

## 対応する文法

| 機能 | 例 |
|------|-----|
| 型 | `int`, `char`, `void`, `int*`, `char*` |
| リテラル | `42`, `'a'`, `"hello"` |
| 演算子 | `+` `-` `*` `/` `%` `==` `!=` `<` `<=` `>` `>=` `!` `-`(単項) `&` `*`(間接参照) |
| 制御構造 | `if`/`else`, `while` |
| 関数 | 定義・呼び出し（引数最大6個） |
| 変数 | ローカル（初期化子必須）、グローバル |
| 配列 | 1次元のみ (`int a[10];`) |
| ポインタ | 1段階のみ (`int *p`) |

意図的に除外した機能: `for`, `switch`, `&&`, `||`, 構造体, float, プリプロセッサ, etc.



## クレジット

- 企画: t-ishii66(大学で物理を学ぶ。システムエンジニア。英会話奮闘中)
- 構成: t-ishii66
- コーディング: Claude Opus4.7
- ドキュメント: Claude Opus4.7
- コードレビュー: t-ishii66
- ドキュメントレビュー: t-ishii66
- イラスト: ChatGPT5.4
- 発行日: 2026/5/11
- バージョン: 1.0.0
- Copyright(C)2026 t-ishii66. All rights reserved.




<img src="docs/images/img-9.png" width="800">