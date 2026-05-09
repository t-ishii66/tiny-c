# 02 — ピープホール最適化

ピープホール（peephole = 覗き穴）最適化は、生成された asm を上から順に **覗き窓ぐらいの幅** で眺めながら、無駄なパターンを書き換えていくシンプルな後処理だ。tiny-c では覗き窓は **隣接 2 行** か、**`ret` 以降のブロック**。

## 1. パイプラインの中の位置

ch01〜ch06 までのフローは「codegen が `fprintf` で直接 stdout に書く」だった。ch07 ではこれを変更する:

```
codegen → メモリバッファ (open_memstream) → peephole → stdout
```

codegen 自体は **無変更**。出力先を `stdout` ではなく一時的にメモリに向け、生成し終わった asm をピープホールに渡してから本物の出力に流す。

```c
char *buf = NULL;
size_t len = 0;
FILE *mem = open_memstream(&buf, &len);
codegen(program, mem);
fclose(mem);
peephole(buf, (int)len, stdout);
```

`open_memstream` は POSIX の関数で、`FILE *` インターフェースのままメモリに書き込める。Linux と macOS で動く。

## 2. 行に分割する

ピープホールは「行単位」で考えると分かりやすい。バッファを `\n` で割って配列に入れる:

```c
static char *lines[MAX_LINES];
static int n_lines;

static void split_lines(const char *buf, int len) {
    /* buf を \n で区切って lines[] に格納 */
}
```

各 `lines[i]` は1行（末尾の `\n` を含む）。ピープホールは `lines[]` 上で動き、書き換えたい位置を NULL にしたり別文字列に差し替えたりする。最後に NULL でない行だけを `stdout` にフラッシュする。

## 3. パターン1: `pushq %rax; popq %reg` → `movq %rax, %reg`

tiny-c の codegen が出すアセンブリで一番目につく無駄が、関数呼び出しの引数渡しの最後の部分にある:

```asm
movl $4, %eax
pushq %rax
movl $3, %eax
pushq %rax    ← arg 1 を push
popq %rdi     ← その直後に pop
popq %rsi
movl $0, %eax
call f
```

最後の `pushq %rax` の **直後** に `popq %rdi` が並ぶ。これは「`%rax` に置いた値をスタック経由で `%rdi` に移しているだけ」── ストレートに `movq %rax, %rdi` で済む。

```asm
movl $4, %eax
pushq %rax
movl $3, %eax
movq %rax, %rdi   ← 2行が1行に
popq %rsi
movl $0, %eax
call f
```

実装:

```c
for (int i = 0; i + 1 < n_lines; i++) {
    if (strcmp(lines[i], "  pushq %rax\n") != 0) continue;

    /* pushq %rax; popq REG → movq %rax, REG */
    if (strncmp(lines[i+1], "  popq ", 7) == 0) {
        char reg[16];
        sscanf(lines[i+1] + 7, "%15s", reg);
        free(lines[i]); free(lines[i+1]);
        char *combined = malloc(64);
        snprintf(combined, 64, "  movq %%rax, %s\n", reg);
        lines[i] = combined;
        lines[i+1] = NULL;
        i++;
    }
}
```

「lines[i] が `pushq %rax\n` で、lines[i+1] が `popq REG\n` の形」── このパターンを strcmp / strncmp で見て、合致したら lines[i] を `movq` 命令に置き換え、lines[i+1] を NULL にする。

## 4. パターン2: `pushq %rax; popq %rax` → 消去

これも同じ枠組みで処理できる ── push してすぐ同じレジスタに pop しているなら、その push/pop ペア自体が無意味なので両方消す。

実装はパターン1と並べる:

```c
if (strcmp(lines[i+1], "  popq %rax\n") == 0) {
    free(lines[i]);   lines[i]   = NULL;
    free(lines[i+1]); lines[i+1] = NULL;
    i++;
    continue;
}
```

このパターンは tiny-c の通常の codegen ではあまり出ないが、AST 最適化で式が縮んだ結果として出てくることがある。「保険」として入れておく。

## 5. パターン3: `ret` 後のデッドコード除去

ch05 で導入した「**`gen_func` の末尾に常に `movl $0, %eax; leave; ret` を出す**」（落っこち防止）── `return` 文が明示的にある関数では、これらは到達不能（dead code）になる。

```asm
main:
  ...
  movl $42, %eax    ← return 42 から
  leave
  ret               ← ここで関数を抜ける
  movl $0, %eax     ← 到達しない
  leave             ← 到達しない
  ret               ← 到達しない
.globl set          ← ここから次の関数 (label)
set:
  ...
```

`ret` の後ろは、次のラベルに到達するまで「実行されないコード」── 削除して問題ない。

実装:

```c
int dead = 0;
for (int i = 0; i < n_lines; i++) {
    if (!lines[i]) continue;
    if (!dead) {
        if (strcmp(lines[i], "  ret\n") == 0) dead = 1;
        continue;
    }
    /* dead 状態 */
    if (!is_instr_line(lines[i])) {
        /* ラベル行 or ディレクティブに到達 → デッド状態解除 */
        dead = 0;
        continue;
    }
    /* 命令行 → 消去 */
    free(lines[i]);
    lines[i] = NULL;
}
```

「`ret` を見たら dead 状態に入る、命令行は消す、ラベルやディレクティブが来たら dead 状態を抜ける」── これだけ。

`ret` は **関数の途中** にも現れる。たとえば `if (cond) return 1; else return 2;` のように `if` の各分岐で early return する場合、関数末尾以外でも `ret` が出る:

```asm
  cmpl $0, %eax
  je .Lelse_0
  movl $1, %eax        ← then 節
  leave
  ret                  ← 途中の ret
  jmp .Lendif_0        ← 到達しないが消える対象
.Lelse_0:              ← ここでデッド状態が解除される
  movl $2, %eax
  leave
  ret
.Lendif_0:
  ...
```

`ret` の次の `jmp .Lendif_0` は到達不能（直前で関数を抜けるので）。デッド状態のまま命令行として消える。次の `.Lelse_0:` ラベルでデッド状態が解除され、`else` 節のコードはちゃんと残る。**「途中 ret」も「末尾 ret」も同じルールで処理される**。

`is_instr_line` は **「実行可能な命令の行か」** を判定するヘルパー:

- 先頭が空白（インデントされている）→ 命令ぽい
- ただし最初の非空白文字が `.` なら **ディレクティブ**（`.text` `.globl` `.section` など）── 命令ではない
- 先頭が空白でなければ **ラベル** （`main:` `.LS0:` など）

ディレクティブやラベルを「命令」と誤判定すると、`.section .rodata` まで消してしまう。慎重に分類する。

```c
static int is_instr_line(const char *line) {
    if (!line) return 0;
    if (line[0] != ' ' && line[0] != '\t') return 0;  /* ラベル */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '.') return 0;                          /* ディレクティブ */
    if (*p == '\n' || *p == '\0') return 0;           /* 空行 */
    return 1;
}
```

## 6. ピープホールの限界

このやり方で簡単に取れる無駄はかなり残っている。例えば:

```asm
leaq -8(%rbp), %rax    ← &x
pushq %rax              ← save addr
movl $5, %eax           ← rhs を計算
popq %rcx               ← restore addr to %rcx
movl %eax, (%rcx)       ← *rcx = eax
```

これは `int x = 5;` の生成コード。`leaq -8(%rbp), %rax; ...; movl %eax, (%rcx)` の中で `pushq` と `popq` が **離れている**（間に `movl $5, %eax` を挟む）ため、隣接2行の peephole では見つけられない。本当は `movl $5, -8(%rbp)` 1行で済むはず。

これを取るには:

- AST レベルで「**rhs が即値の代入**」を特殊に扱う（codegen 内で直接 `movl $C, addr` を出す）。
- もしくは codegen 後に **3〜4行のウィンドウ** で push と pop の対応を辿るピープホール（より複雑）。

tiny-c の peephole は隣接2行と `ret` 後ブロックの2種類だけ。

## 7. ピープホールパスは順序に依存しない

実装した3パターンは **互いに干渉しない**:

- pushq/popq の融合は隣接2行の置換、ret 後の削除には影響しない。
- ret 後の削除はそれ以降の行を消すだけ、push/pop の融合に影響しない。

そのため `peephole_pushpop()` を先に呼ぶか `peephole_dead_after_ret()` を先に呼ぶかは結果に影響しない。複雑な最適化では順序が結果を変える（→ pass scheduling）。

## 8. 次へ

最後の節（`04_build.md`）で、`optimize.c` 全体の差分と `main.c` の変更を並べ、ビフォー・アフターのデモを示す。
