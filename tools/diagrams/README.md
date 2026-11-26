# tools/diagrams — ドキュメントの SVG 図を作るスクリプト

`docs/images/diagrams/` の SVG 図（日本語版 `jp/` と英語版 `en/`）を作り直すためのスクリプト。図は `fig-章-番号.svg` という名前で、日本語版と英語版でファイル名をそろえてある。

## 図の種類と作り方

| 図 | 作り方 |
|----|--------|
| 木構造図（AST、codegen の呼び出し経路）12 枚: 1-9, 1-11, 2-2, 2-4, 3-4, 3-5, 4-1, 4-2, 6-1, 6-2, 7-2, 7-3 | `gen_trees.py` が木の定義から日本語版・英語版の両方を生成する |
| それ以外 23 枚 | `jp/` の SVG を手で編集する。英語版は `translate_svgs.py` が日本語版から作る |

## 使い方

リポジトリのどこからでも実行できる（出力先はスクリプトの位置から決まる）。

```bash
# 木構造図を作り直す（日本語版・英語版）
python3 tools/diagrams/gen_trees.py jp
python3 tools/diagrams/gen_trees.py en

# 手描きの図の英語版を、日本語版から作り直す
python3 tools/diagrams/translate_svgs.py
```

## 図を直すとき

- **木構造図**: `gen_trees.py` の末尾にある図の定義を直し、`jp` と `en` の両方で実行する。日本語と英語の文字列は `t('日本語', 'English')` の形で並べて書く。
- **手描きの図**: `jp/` の SVG を直してから `translate_svgs.py` を実行する。日本語の文字列を変えたときは、`translate_svgs.py` の対応表（`T`）にも英訳を足す。対応表の文字列が SVG に見つからないとき、または英語版に日本語が残ったときは、エラーで止まる。
