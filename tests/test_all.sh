#!/bin/bash
COMPILER="${1:-./build/compiler}"
PASS=0
FAIL=0
TOTAL=0

echo "=== 自动测试所有 ToyC 文件 ==="
echo ""

# 合法用例
echo "--- 合法用例测试 ---"
for tc in $(find tests -name "*.tc" -not -path "*/milestone6/*" | sort); do
    TOTAL=$((TOTAL + 1))
    echo -n "  [$TOTAL] $tc ... "
    if "$COMPILER" < "$tc" > /dev/null 2>&1; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
    fi
done

echo ""

# 非法用例
echo "--- 非法用例测试 ---"
for tc in $(find tests/milestone6 -name "*.tc" | sort); do
    TOTAL=$((TOTAL + 1))
    echo -n "  [$TOTAL] $tc ... "
    if "$COMPILER" < "$tc" > /dev/null 2>&1; then
        echo "FAIL (应该报错但通过了)"
        FAIL=$((FAIL + 1))
    else
        echo "PASS (正确拒绝)"
        PASS=$((PASS + 1))
    fi
done

echo ""
echo "========================================"
echo "  结果: $PASS/$TOTAL 通过, $FAIL 失败"
echo "========================================"
