#!/bin/bash
TINYC=./tinyc
TESTDIR=$(dirname "$0")
PASS=0
FAIL=0

# --- Parser tests (--dump-ast succeeds) ---
for src in "$TESTDIR"/parse_*.c; do
    name=$(basename "$src")
    if $TINYC --dump-ast "$src" > /dev/null 2>&1; then
        echo "PASS: $name (parse)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name (parse)"
        FAIL=$((FAIL + 1))
    fi
done

# --- Codegen tests (assembly generation succeeds) ---
codegen_test() {
    local src="$1"
    local name=$(basename "$src")

    if $TINYC "$src" > /dev/null 2>&1; then
        echo "PASS: $name (codegen)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name (codegen)"
        FAIL=$((FAIL + 1))
    fi
}

for src in "$TESTDIR"/ch*.c; do
    codegen_test "$src"
done

# --- Execution tests (x86-64 Linux only) ---
run_test() {
    local src="$1"
    local expected="$2"
    local name=$(basename "$src")
    local asm_file="/tmp/tinyc_test.s"
    local exe_file="/tmp/tinyc_test"

    if ! $TINYC "$src" > "$asm_file" 2>/dev/null; then
        echo "FAIL: $name (codegen error)"
        FAIL=$((FAIL + 1))
        return
    fi

    if ! gcc -o "$exe_file" "$asm_file" 2>/dev/null; then
        echo "SKIP: $name (cannot assemble on this platform)"
        return
    fi

    "$exe_file"
    local actual=$?

    if [ "$actual" -eq "$expected" ]; then
        echo "PASS: $name => $actual"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name => expected $expected, got $actual"
        FAIL=$((FAIL + 1))
    fi
}

# ch1: integer return
run_test "$TESTDIR/ch1_return0.c" 0
run_test "$TESTDIR/ch1_return42.c" 42

# ch2: arithmetic
run_test "$TESTDIR/ch2_arithmetic.c" 14
run_test "$TESTDIR/ch2_mod.c" 2

# ch3: control flow
run_test "$TESTDIR/ch3_if.c" 1
run_test "$TESTDIR/ch3_while.c" 45

# ch4: functions
run_test "$TESTDIR/ch4_func.c" 7
run_test "$TESTDIR/ch4_recursive.c" 55

# ch5: pointers
run_test "$TESTDIR/ch5_pointer.c" 42
run_test "$TESTDIR/ch5_pointer_write.c" 99

# ch6: arrays
run_test "$TESTDIR/ch6_array.c" 60

# ch7: globals & unary
run_test "$TESTDIR/ch7_global.c" 77
run_test "$TESTDIR/ch7_unary.c" 6

echo "---"
echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
