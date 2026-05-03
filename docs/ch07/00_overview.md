# 第7章 — 簡単な最適化

## 0. はじめに

ch01 から ch06 まで、生成されるアセンブリは「**素直で愚直**」だった。`return 2 + 3 * 4;` をコンパイルすると 11 行の asm が出た ── レジスタは `%eax` しか使わず、`pushq` と `popq` で中間値を退避し、AT&T 流の3命令で計算する。読みやすいが、**明らかに無駄が多い**。

第7章では、この無駄を取る **最適化** を導入する。tiny-c で扱える範囲は限られているが、**2 つのレベル**で行うと十分に体感できる効果が出る。

| レベル | 名前 | 内容 |
|------|------|------|
| AST レベル | **定数畳み込み + 代数的単純化** | パース後・コード生成前に AST を書き換える |
| 生成 asm レベル | **ピープホール最適化** | コード生成後に隣接命令を眺めて書き換える |

ch01〜ch06 で「コンパイラの中身（lexer → parser → AST → codegen）」を一通り作ったが、ch07 で **「最適化はそれら の **間** に挟まる別のパス」** という考え方が入る。

```
ソース → 字句解析 → 構文解析 → AST → [AST 最適化] → codegen → asm → [ピープホール] → 出力 asm
                                  ↑                                ↑
                                  ch07 で追加                       ch07 で追加
```

## 1. 効果のサンプル

`return 2 + 3 * 4;` のコンパイル結果:

**最適化なし** (11 行)

```asm
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $4, %eax
  pushq %rax
  movl $3, %eax
  popq %rcx
  imull %ecx, %eax
  pushq %rax
  movl $2, %eax
  popq %rcx
  addl %ecx, %eax
  leave
  ret
```

**最適化あり** (5 行)

```asm
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $14, %eax
  leave
  ret
```

`2 + 3 * 4` が **コンパイル時に 14 に畳まれ**、しかも末尾の暗黙 `return` の dead code も除去された結果。CPU が走る命令数が劇的に減った。

`printf("hello\n")` も:

**最適化なし**: `pushq %rax; popq %rdi` が残る。
**最適化あり**: `movq %rax, %rdi` 1 命令に置き換わり、末尾の dead code も消える。

## 2. ch06 との差分

| ファイル | 変更 |
|---------|------|
| `lexer.l` / `parser.y` / `ast.h` / `ast.c` / `codegen.c` | **変更なし** |
| `optimize.h` / `optimize.c` | **新規** ── `optimize_ast` と `peephole` の2関数 |
| `main.c` | パース後に `optimize_ast` を呼び、codegen 後に `peephole` を通す。`--no-opt` オプションを追加 |
| `Makefile` | `optimize.c` をビルド対象に追加 |

**lexer / parser / AST / codegen は一切触らない**。最適化は完全に別パスとして外側に追加される。これも「コンパイラの構造」の重要な側面 ── 各パスが独立していると、新しい最適化を後から付け足しやすい。

## 3. 使い方

```bash
$ ./tinyc test.c              # 最適化あり（デフォルト）
$ ./tinyc --no-opt test.c     # 最適化なし（比較用）
$ ./tinyc --dump-ast test.c   # 最適化後の AST を表示
$ ./tinyc --no-opt --dump-ast test.c  # 最適化前の AST を表示
```

`--no-opt` オプションがあるので、ビフォー・アフターを直接見比べられる。ch07 の docs ではこの形でデモを示す。

## 4. サブ章の構成

| サブ章 | 内容 |
|--------|------|
| 00（この文書）| 章の見取り図 |
| 01 | AST レベル最適化 — 定数畳み込み + 代数的単純化 |
| 02 | ピープホール最適化 — 生成 asm を眺めて書き換える |
| 03 | 全ファイルの差分、ビルド、最適化前後の比較 |

## 5. 次へ

サブ章 01（`01_const_fold.md`）では、AST 最適化の中身を見る。`2 + 3 * 4` がなぜ codegen に渡る前に `14` になるか、`x + 0` がなぜ `x` だけに化けるか ── 仕組みは小さな再帰関数1つ。
