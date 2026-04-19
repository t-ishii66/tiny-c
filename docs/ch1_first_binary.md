# 章1: 最初の実行ファイル

## ゴール

```c
int main() {
    return 42;
}
```

これをコンパイルして、終了コード 42 で終了する実行ファイルを作る。

## 生成されるアセンブリ

```asm
    .text
    .globl main
main:
    pushq %rbp          # 呼び出し元の rbp を保存
    movq  %rsp, %rbp    # 新しいスタックフレームを開始
    movl  $42, %eax     # 戻り値を eax にセット
    leave                # rbp を復元し rsp を戻す (= movq %rbp,%rsp + popq %rbp)
    ret                  # 呼び出し元に戻る
```

## スタックフレーム

x86-64 の関数呼び出しでは、`rbp`（ベースポインタ）と `rsp`（スタックポインタ）の2つのレジスタでスタックフレームを管理する。

```
関数の入口:                関数の本体:
┌──────────┐              ┌──────────┐
│ 戻りアドレス │  ← rsp       │ 戻りアドレス │
└──────────┘              ├──────────┤
                          │ 古い rbp  │  ← rbp, rsp
                          └──────────┘
```

- `pushq %rbp` — 呼び出し元の rbp をスタックに退避
- `movq %rsp, %rbp` — 現在の rsp を rbp にコピー（これがこの関数のベースになる）
- `leave` — `movq %rbp, %rsp` + `popq %rbp` の省略形。フレームを巻き戻す
- `ret` — スタックから戻りアドレスを pop して、そこにジャンプ

この **プロローグ** (`push`+`mov`) と **エピローグ** (`leave`+`ret`) は、今後すべての関数で同じ形が現れる。

## 戻り値の規約

x86-64 System V ABI では、関数の戻り値は `eax`（32bit）または `rax`（64bit）レジスタに置く。`main` の戻り値がプロセスの終了コードになる。

## コード生成の実装

`src/codegen.c` の構造:

```
codegen(program)
  └─ gen_func(func_def)      ... プロローグ出力
       └─ gen_stmt(block)
            └─ gen_stmt(return)
                 └─ gen_expr(int_lit)  ... movl $N, %eax
```

ASTを再帰的にたどるだけ。この章では `gen_expr` は整数リテラルのみ対応。

## テスト

```bash
$ ./tinyc test/ch1_return42.c > /tmp/test.s
$ gcc -o /tmp/test /tmp/test.s
$ /tmp/test; echo $?
42
```

## 次の章へ

今は `return 42;` しかできない。次の章で四則演算を足し、`return 2 + 3 * 4;` を動かす。
