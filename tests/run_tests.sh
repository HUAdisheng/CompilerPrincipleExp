#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
COMPILER="$BUILD_DIR/compiler"
VALID_LIST="$ROOT_DIR/tests/smoke/valid_cases.txt"
INVALID_LIST="$ROOT_DIR/tests/smoke/invalid_cases.txt"
OUT_DIR="$ROOT_DIR/tests/.out"
RUNTIME_DIR="$ROOT_DIR/tests/runtime"
RISCV_ASM_CC="${RISCV_ASM_CC:-clang}"
RISCV_LINKER="${RISCV_LINKER:-ld.lld}"
QEMU_BIN="${QEMU_BIN:-qemu-riscv32}"

mkdir -p "$OUT_DIR"

has_line() {
    local pattern="$1"
    local file="$2"

    if command -v rg >/dev/null 2>&1 && rg --version >/dev/null 2>&1; then
        rg -q "$pattern" "$file"
    else
        grep -Eq "$pattern" "$file"
    fi
}

has_fixed_text() {
    local text="$1"
    local file="$2"

    if command -v rg >/dev/null 2>&1 && rg --version >/dev/null 2>&1; then
        rg -F -q "$text" "$file"
    else
        grep -Fq "$text" "$file"
    fi
}

build_compiler() {
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" >/dev/null &&
    cmake --build "$BUILD_DIR" >/dev/null
}

check_runtime_tools() {
    command -v "$RISCV_ASM_CC" >/dev/null 2>&1 &&
    command -v "$RISCV_LINKER" >/dev/null 2>&1 &&
    command -v "$QEMU_BIN" >/dev/null 2>&1
}

run_elf_and_get_exit() {
    local elf_file="$1"
    local runtime_log="$2"

    "$QEMU_BIN" "$elf_file" >"$runtime_log" 2>&1
    printf '%s' "$?"
    return 0
}

run_valid_case() {
    local line="$1"
    local rel="${line%%|*}"
    local expected="${line#*|}"
    local src="$ROOT_DIR/$rel"
    local base
    base="$(basename "$src" .tc)"
    local asm_file="$OUT_DIR/${base}.valid.s"
    local compile_stderr="$OUT_DIR/${base}.valid.compile.stderr"
    local start_obj="$OUT_DIR/${base}.linux_start.o"
    local asm_obj="$OUT_DIR/${base}.valid.o"
    local elf_file="$OUT_DIR/${base}.valid.elf"
    local link_stderr="$OUT_DIR/${base}.valid.link.stderr"
    local runtime_log="$OUT_DIR/${base}.valid.runtime.log"
    local actual

    "$COMPILER" < "$src" > "$asm_file" 2> "$compile_stderr"
    local code=$?
    if [ "$code" -ne 0 ]; then
        echo "FAIL valid  $rel (exit=$code)"
        sed -n '1,6p' "$compile_stderr"
        return 1
    fi

    if ! has_line "^    \\.text$" "$asm_file"; then
        echo "FAIL valid  $rel (missing .text)"
        return 1
    fi

    if ! has_line "^    \\.globl main$" "$asm_file"; then
        echo "FAIL valid  $rel (missing .globl main)"
        return 1
    fi

    "$RISCV_ASM_CC" --target=riscv32-linux-gnu -c "$RUNTIME_DIR/linux_start.s" \
        -o "$start_obj" > /dev/null 2>"$link_stderr"
    code=$?
    if [ "$code" -ne 0 ]; then
        echo "FAIL valid  $rel (assemble start failed)"
        sed -n '1,10p' "$link_stderr"
        return 1
    fi

    "$RISCV_ASM_CC" --target=riscv32-linux-gnu -c "$asm_file" \
        -o "$asm_obj" > /dev/null 2>"$link_stderr"
    code=$?
    if [ "$code" -ne 0 ]; then
        echo "FAIL valid  $rel (assemble generated asm failed)"
        sed -n '1,10p' "$link_stderr"
        return 1
    fi

    "$RISCV_LINKER" -m elf32lriscv -static -e _start \
        "$start_obj" "$asm_obj" -o "$elf_file" > /dev/null 2>"$link_stderr"
    code=$?
    if [ "$code" -ne 0 ]; then
        echo "FAIL valid  $rel (link failed)"
        sed -n '1,10p' "$link_stderr"
        return 1
    fi

    actual="$(run_elf_and_get_exit "$elf_file" "$runtime_log")"
    code=$?
    if [ "$code" -ne 0 ]; then
        echo "FAIL valid  $rel (runtime failed)"
        sed -n '1,20p' "$runtime_log"
        return 1
    fi

    if [ "$actual" != "$expected" ]; then
        echo "FAIL valid  $rel (expected=$expected actual=$actual)"
        sed -n '1,20p' "$runtime_log"
        return 1
    fi

    echo "PASS valid  $rel"
    return 0
}

run_invalid_case() {
    local line="$1"
    local rel="${line%%|*}"
    local expected="${line#*|}"
    local src="$ROOT_DIR/$rel"
    local base
    base="$(basename "$src" .tc)"
    local stdout_file="$OUT_DIR/${base}.invalid.stdout"
    local stderr_file="$OUT_DIR/${base}.invalid.stderr"

    "$COMPILER" < "$src" > "$stdout_file" 2> "$stderr_file"
    local code=$?
    if [ "$code" -eq 0 ]; then
        echo "FAIL invalid $rel (unexpected success)"
        return 1
    fi

    if ! has_fixed_text "$expected" "$stderr_file"; then
        echo "FAIL invalid $rel (missing expected error)"
        echo "expected: $expected"
        sed -n '1,6p' "$stderr_file"
        return 1
    fi

    echo "PASS invalid $rel"
    return 0
}

main() {
    local fail=0
    local valid_total=0
    local invalid_total=0
    local valid_pass=0
    local invalid_pass=0

    if ! build_compiler; then
        echo "build failed"
        exit 1
    fi

    if ! check_runtime_tools; then
        echo "missing runtime tools: need $RISCV_ASM_CC, $RISCV_LINKER and $QEMU_BIN"
        exit 1
    fi

    while IFS= read -r line; do
        [ -n "$line" ] || continue
        valid_total=$((valid_total + 1))
        if run_valid_case "$line"; then
            valid_pass=$((valid_pass + 1))
        else
            fail=1
        fi
    done < "$VALID_LIST"

    while IFS= read -r line; do
        [ -n "$line" ] || continue
        invalid_total=$((invalid_total + 1))
        if run_invalid_case "$line"; then
            invalid_pass=$((invalid_pass + 1))
        else
            fail=1
        fi
    done < "$INVALID_LIST"

    echo
    echo "Summary:"
    echo "  valid   $valid_pass/$valid_total passed"
    echo "  invalid $invalid_pass/$invalid_total passed"
    echo "  outputs saved in $OUT_DIR"

    if [ "$fail" -ne 0 ]; then
        exit 1
    fi
}

main "$@"
