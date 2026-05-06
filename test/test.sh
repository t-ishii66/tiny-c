#!/usr/bin/env bash
# Test runner for tiny-c.
#
# For each test/cases/*.c file:
#   1. Compile with ./tinyc to .s
#   2. If host can run x86-64 Linux binaries, assemble with gcc and execute
#   3. Compare exit code against `// expect: N` and stdout against `// output: TEXT`
#
# On non-x86-Linux hosts the run step is skipped (asm-only check still happens).

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TINYC="$ROOT/tinyc"
CASES="$ROOT/test/cases"
TMP="$ROOT/build/test"
mkdir -p "$TMP"

if [ ! -x "$TINYC" ]; then
    echo "tinyc not found. Run 'make' first." >&2
    exit 1
fi

# Detect whether we can assemble + run the generated x86-64 Linux asm.
CAN_RUN=0
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]; then
    CAN_RUN=1
fi

pass=0
fail=0
skip=0
fails=()

for src in "$CASES"/*.c; do
    name="$(basename "$src" .c)"
    asm="$TMP/$name.s"
    bin="$TMP/$name"

    expect_code=""
    expect_out=""
    while IFS= read -r line; do
        case "$line" in
            "// expect: "*) expect_code="${line#// expect: }" ;;
            "// output: "*) expect_out="${line#// output: }" ;;
        esac
    done < "$src"

    # Step 1: compile to asm. tinyc must succeed.
    if ! "$TINYC" "$src" > "$asm" 2> "$TMP/$name.err"; then
        printf "FAIL %-20s (tinyc failed)\n" "$name"
        cat "$TMP/$name.err" | sed 's/^/    /'
        fail=$((fail+1))
        fails+=("$name")
        continue
    fi

    if [ "$CAN_RUN" -eq 0 ]; then
        printf "SKIP %-20s (asm OK; cannot run on $(uname -s)/$(uname -m))\n" "$name"
        skip=$((skip+1))
        continue
    fi

    # Step 2: assemble + link.
    if ! gcc -no-pie -o "$bin" "$asm" 2> "$TMP/$name.lnk"; then
        printf "FAIL %-20s (gcc link failed)\n" "$name"
        cat "$TMP/$name.lnk" | sed 's/^/    /'
        fail=$((fail+1))
        fails+=("$name")
        continue
    fi

    # Step 3: run.
    actual_out="$("$bin")"
    actual_code=$?

    ok=1
    if [ -n "$expect_code" ] && [ "$actual_code" != "$expect_code" ]; then
        ok=0
        msg="exit $actual_code (want $expect_code)"
    fi
    if [ -n "$expect_out" ] && [ "$actual_out" != "$expect_out" ]; then
        ok=0
        msg="${msg:+$msg, }stdout '$actual_out' (want '$expect_out')"
    fi

    if [ "$ok" -eq 1 ]; then
        printf "PASS %-20s\n" "$name"
        pass=$((pass+1))
    else
        printf "FAIL %-20s %s\n" "$name" "$msg"
        fail=$((fail+1))
        fails+=("$name")
    fi
done

echo
echo "----------------------------------------"
echo "PASS: $pass  FAIL: $fail  SKIP: $skip"
if [ "$fail" -gt 0 ]; then
    echo "Failures: ${fails[*]}"
    exit 1
fi
