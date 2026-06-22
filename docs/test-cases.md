# ToyC 编译器测试用例说明

## 概述

本文档描述 ToyC 编译器的冒烟测试用例集，用于验证编译器各阶段功能的正确性。

测试用例分为两大类：

- **合法用例（37 个）**：语法语义正确的 ToyC 程序，验证编译器能否生成正确的 RISC-V32 汇编并得到预期退出码
- **非法用例（15 个）**：包含语义错误的程序，验证编译器能否检测并输出准确的错误信息

用例按里程碑（milestone）组织，对应 `docs/work.md` 中定义的开发阶段。

---

## 如何运行

```bash
# 冒烟测试（全部 52 个用例）
bash tests/run_smoke.sh ./build/compiler

# 完整测试（run_tests.sh）
bash tests/run_tests.sh ./build/compiler
```

---

## 合法用例（37 个）

### Milestone 1：基础返回与一元表达式（2 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 1 | `return_42.tc` | `int main() { return 42; }` | 42 | 最小可运行程序：定义 `main` 并返回整数字面量 |
| 2 | `unary.tc` | `int main() { return !-0; }` | 1 | 一元运算符组合：逻辑非 `!` + 取负 `-`，验证一元运算优先级 |

### Milestone 2：二元表达式与运算符（7 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 3 | `binary_precedence.tc` | `int main() { return 1 + 2 * 3 - 4; }` | 3 | 运算符优先级：`*` > `+`/`-`，`1+(2*3)-4 = 3` |
| 4 | `parentheses.tc` | `int main() { return (1 + 2) * (3 + 4); }` | 21 | 括号改变优先级：`(1+2)*(3+4) = 21` |
| 5 | `relational_logic.tc` | `int main() { return 1 < 2 && 3 >= 3 \|\| 0; }` | 1 | 关系运算符与逻辑运算符的优先级：`&&` > `\|\|` |
| 6 | `div_mod.tc` | `int main() { return 10 / 3 + 10 % 3; }` | 4 | 除法 `/` 和取模 `%`：`3 + 1 = 4` |
| 7 | `relational_all.tc` | 见附录 | 38 | 全部 6 种关系运算符（`>`, `<`, `<=`, `>=`, `==`, `!=`）+ `if` 语句组合 |
| 8 | `unary_plus.tc` | `int main() { return +42; }` | 42 | 一元正号运算符 `+` |
| 9 | `negative_literal.tc` | `int main() { return -5 + 10; }` | 5 | 负数与加法组合 |

### Milestone 3：变量、赋值与作用域（4 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 10 | `assignment.tc` | `int main() { int a = 1; a = a + 4; return a; }` | 5 | 变量声明、初始化、赋值语句 |
| 11 | `local_vars.tc` | `int main() { int a=1; int b=2; int c=a+b*3; return c; }` | 7 | 多局部变量，初始化可引用已声明的变量 |
| 12 | `scope_shadowing.tc` | 见附录 | 1 | 内层块同名变量遮蔽外层（shadowing），内层不影响外层 |
| 13 | `shadow_global.tc` | `int g=10; int main() { int g=20; return g; }` | 20 | 全局变量被局部变量遮蔽，局部优先 |

### Milestone 4：控制流（8 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 14 | `if_else.tc` | `int main() { int a=0; if(1){a=3;}else{a=4;} return a; }` | 3 | 标准 `if-else`，条件为真走 if 分支 |
| 15 | `if_no_else.tc` | 见附录 | 5 | `if` 不带 `else`，条件为真时执行 |
| 16 | `if_no_else_false.tc` | 见附录 | 0 | `if` 不带 `else`，条件为假时跳过 |
| 17 | `dangling_else.tc` | 见附录 | 3 | 悬空 else 匹配最近 if：`else` 匹配内层 `if(a+1)` |
| 18 | `while_loop.tc` | 见附录 | 6 | `while` 循环基本功能：累加 1+2+3=6 |
| 19 | `nested_while.tc` | 见附录 | 6 | 嵌套 `while`：外层 3 次 × 内层 2 次 = 6 |
| 20 | `break_continue.tc` | 见附录 | 8 | `break` 提前退出循环 + `continue` 跳过当前迭代 |
| 21 | `empty_stmt.tc` | 见附录 | 3 | 空语句 `;` 的正确处理 |

### Milestone 5：函数与全局变量（8 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 22 | `function_call.tc` | `int twice(int x){return x+x;} int main(){return twice(21);}` | 42 | 基本函数定义与调用 |
| 23 | `recursion.tc` | `int down(int x){if(x)return down(x-1);else return 0;} int main(){return down(3);}` | 0 | 递归函数：`down(3)→down(2)→down(1)→down(0)=0` |
| 24 | `global_var_const.tc` | 见附录 | 8 | 全局常量 `base=2` + 全局变量 `g=5` + 函数调用 `1+5+2=8` |
| 25 | `void_function.tc` | `void touch(int x){int y=x+1;} int main(){touch(3);return 0;}` | 0 | `void` 函数：调用后无返回值，main 返回 0 |
| 26 | `void_explicit_return.tc` | 见附录 | 0 | `void` 函数中使用 `return;`（无返回值）提前退出 |
| 27 | `three_params.tc` | `int sum3(int a,int b,int c){return a+b+c;} int main(){return sum3(10,20,30);}` | 60 | 三参数函数调用 |
| 28 | `eight_params.tc` | `int sum8(...){...} int main(){return sum8(1,2,3,4,5,6,7,8);}` | 36 | 八参数函数调用（覆盖寄存器传递 + 栈溢出场景） |
| 29 | `call_chain.tc` | 见附录 | 10 | 函数调用链：`main→twice(3)→add2+add2`，`(3+2)+(3+2)=10` |

### Milestone 7：高级特性与综合测试（8 个）

| # | 用例 | 源码 | 预期 | 说明 |
|---|------|------|:--:|------|
| 30 | `comments.tc` | 见附录 | 2 | 单行 `//` 和多行 `/* */` 注释，含块注释嵌在语句中 |
| 31 | `short_circuit.tc` | 见附录 | 0 | 短路求值：`a && (1/a)` 中 `a=0` 时不计算 `1/a` |
| 32 | `const_folding.tc` | `int main(){const int a=1+2*3; const int b=(a+4)*2; return b;}` | 22 | 常量折叠：编译期计算 `a=7`, `b=(7+4)*2=22` |
| 33 | `complex_expr.tc` | 见附录 | 1 | 复杂表达式：混合算术、关系、逻辑运算 |
| 34 | `many_locals.tc` | 见附录 | 78 | 大量局部变量（12 个），测试栈帧布局 |
| 35 | `complex_control_flow.tc` | 见附录 | 31 | 复杂控制流：while + if-else 嵌套 + 多种表达式 |
| 36 | `multi_function.tc` | 见附录 | 18 | 多函数交叉调用：`combine→double_it+triple_it` |
| 37 | `deep_expr.tc` | `int main(){return 1+2*3-4/2+5%3+10*(2+1)-3;}` | 34 | 深度表达式：考验表达式求值正确性 |

---

## 非法用例（15 个）— 语义错误检测

所有非法用例位于 `tests/milestone6/`，编译器应对这些程序输出错误并返回非零退出码。

| # | 用例 | 源码概要 | 期望错误信息 |
|---|------|----------|--------------|
| 1 | `error_break_outside.tc` | `main` 中直接 `break;` | `break can only appear inside a loop` |
| 2 | `error_continue_outside.tc` | `main` 中直接 `continue;` | `continue can only appear inside a loop` |
| 3 | `error_assign_const.tc` | `const int a=1; a=2;` | `cannot assign to const identifier a` |
| 4 | `error_undeclared.tc` | `return x;` 但 x 未声明 | `use of undeclared identifier x` |
| 5 | `error_call_before_definition.tc` | `main` 中调用 `f()`，但 `f` 定义在后面 | `call to function f before its definition` |
| 6 | `error_missing_main.tc` | 没有 `main` 函数 | `missing entry function main` |
| 7 | `error_bad_main_signature.tc` | `main` 有参数或返回类型非 `int` | `main must have type int main()` |
| 8 | `error_missing_return.tc` | `int f(int x)` 在 `x==0` 时无返回值 | `int function f must return a value on all paths` |
| 9 | `error_void_return_value.tc` | `void f(){ return 1; }` | `void function f cannot return a value` |
| 10 | `error_const_init_nonconst.tc` | `const int a = g + 1;` 用非常量表达式初始化常量 | `const initializer must be a compile-time constant expression` |
| 11 | `error_global_init_nonconst.tc` | 全局变量初始化引用未声明变量 | `use of undeclared identifier h` |
| 12 | `error_redefine_global.tc` | `int a=1; int a=2;` 重复定义全局名 | `redefinition of global name a` |
| 13 | `error_redefine_local.tc` | 同一作用域内 `int a=1; int a=2;` | `redefinition of local name a` |
| 14 | `error_param_count.tc` | 调用函数时参数数量不匹配 | `function f expects 2 arguments` |
| 15 | `error_redefine_function.tc` | 重复定义同名函数 | `redefinition of global name f` |

---

## 附录：较长用例源码

### relational_all.tc（第 7 号，预期 38）

```c
int main() {
    int a = 1;
    int b = 2;
    int r = 0;
    if (a > b)  r = r + 1;    // false
    if (b > a)  r = r + 2;    // true  → +2
    if (a <= b) r = r + 4;    // true  → +4
    if (a >= b) r = r + 8;    // false
    if (a == b) r = r + 16;   // false
    if (a != b) r = r + 32;   // true  → +32
    return r;                  // = 38
}
```

### scope_shadowing.tc（第 12 号，预期 1）

```c
int main() {
    int a = 1;
    {
        int a = 2;
        a = a + 3;
    }
    return a;  // 返回外层 a=1，内层不影响
}
```

### if_no_else.tc（第 15 号，预期 5）

```c
int main() {
    int a = 1;
    if (a)
        a = 5;
    return a;
}
```

### if_no_else_false.tc（第 16 号，预期 0）

```c
int main() {
    int a = 1;
    if (0)
        a = 5;
    return a;
}
```

### dangling_else.tc（第 17 号，预期 3）

```c
int main() {
    int a = 1;
    if (a)
        if (a + 1)
            a = 3;
        else
            a = 4;
    return a;
}
```

### while_loop.tc（第 18 号，预期 6）

```c
int main() {
    int i = 0;
    int s = 0;
    while (i < 3) {
        i = i + 1;
        s = s + i;
    }
    return s;  // 1+2+3=6
}
```

### nested_while.tc（第 19 号，预期 6）

```c
int main() {
    int i = 0, s = 0;
    while (i < 3) {
        int j = 0;
        while (j < 2) {
            s = s + 1;
            j = j + 1;
        }
        i = i + 1;
    }
    return s;  // 3×2=6
}
```

### break_continue.tc（第 20 号，预期 8）

```c
int main() {
    int i = 0, s = 0;
    while (i < 6) {
        i = i + 1;
        if (i == 2) continue;  // 跳过 i=2
        if (i == 5) break;     // i=5 时退出
        s = s + i;
    }
    return s;  // 1+3+4=8
}
```

### empty_stmt.tc（第 21 号，预期 3）

```c
int main() {
    int a = 1;
    if (a)
        ;        // 空语句
    else
        a = 4;
    a = a + 2;
    return a;    // 1+2=3
}
```

### global_var_const.tc（第 24 号，预期 8）

```c
const int base = 2;
int g = 5;

int addg(int x) {
    return x + g + base;
}

int main() {
    return addg(1);  // 1+5+2=8
}
```

### void_explicit_return.tc（第 26 号，预期 0）

```c
void check(int x) {
    if (x) return;   // void 函数中的无值 return
    int y = 1;
}

int main() {
    check(1);
    check(0);
    return 0;
}
```

### call_chain.tc（第 29 号，预期 10）

```c
int add2(int x) {
    return x + 2;
}

int twice(int x) {
    return add2(x) + add2(x);
}

int main() {
    return twice(3);  // (3+2)+(3+2)=10
}
```

### comments.tc（第 30 号，预期 2）

```c
// 单行注释测试
int main() {
    int a = /* 块注释测试 */ 1;
    /*
       多行注释测试
    */
    return a /* 行尾注释 */ + 1;
}
```

### short_circuit.tc（第 31 号，预期 0）

```c
int main() {
    int a = 0;
    if (a && (1 / a)) {  // a=0 时短路，不计算 1/a
        return 1;
    }
    if (1 || (1 / a)) {  // 1 为真时短路，不计算 1/a
        return 0;
    }
    return 2;
}
```

### complex_expr.tc（第 33 号，预期 1）

```c
int main() {
    int a = 10, b = 3, c = 7;
    return ((a+b)*c - a/b + c%b > a) && (a!=b || b<=c && c>=a || a==10);
}
```

### many_locals.tc（第 34 号，预期 78）

```c
int main() {
    int a=1; int b=2; int c=3; int d=4; int e=5;
    int f=6; int g=7; int h=8; int i=9; int j=10;
    int k=11; int l=12;
    return a+b+c+d+e+f+g+h+i+j+k+l;  // 1+2+…+12=78
}
```

### complex_control_flow.tc（第 35 号，预期 31）

```c
int main() {
    int i = 0, s = 0;
    while (i < 10) {
        if (i < 5) {
            s = s + i * 2;           // i=0..4: 0+2+4+6+8=20
        } else {
            if (i % 2 == 0) {
                s = s + i;           // i=6,8: 6+8=14
            } else {
                s = s - 1;           // i=5,7,9: -1-1-1=-3
            }
        }
        i = i + 1;
    }
    return s;  // 20+14-3=31
}
```

### multi_function.tc（第 36 号，预期 18）

```c
int double_it(int x) { return x + x; }
int triple_it(int x) { return x + x + x; }

int combine(int x, int y) {
    return double_it(x) + triple_it(y);
}

int main() {
    return combine(3, 4);  // 6+12=18
}
```

---

## 测试清单文件

用例列表存储在：

| 文件 | 用途 |
|------|------|
| `tests/smoke/valid_cases.txt` | 合法用例清单，格式：`路径\|预期退出码`（37 条） |
| `tests/smoke/invalid_cases.txt` | 非法用例清单，格式：`路径\|期望错误信息`（15 条） |
