# ToyC 功能覆盖矩阵

本文档按照实验任务定义逐项记录编译器实现与回归测试。测试均经过
`ToyC -> RISC-V32 汇编 -> clang -> ld.lld -> qemu-riscv32` 真实执行。

| 规范项目 | 实现位置 | 主要测试 |
| --- | --- | --- |
| stdin 输入、stdout 汇编、`-opt` 参数 | `src/main.cpp` | `tests/run_tests.sh`、全量 `-opt` 回归 |
| 标识符、十进制整数、空白、单双行注释 | `src/lexer.l` | `milestone1/identifier_whitespace.tc`、`block_comments.tc` |
| 全局声明与函数组成的非空编译单元 | `src/parser.y` | `milestone5/global_var_const.tc` |
| int/void 函数、零到多个 int 参数 | `src/parser.y`、`src/semantic.cpp` | `void_function.tc`、`ten_params.tc` |
| 函数先定义后调用及递归 | `src/semantic.cpp` | `function_call.tc`、`factorial_recursion.tc`、错误调用测试 |
| 局部/全局变量和常量初始化 | `src/semantic.cpp`、`src/codegen.cpp` | `local_vars.tc`、`global_const_chain.tc` |
| 常量表达式仅引用字面量和已声明常量 | `src/semantic.cpp` | `const_folding.tc`、短路常量错误测试 |
| 嵌套块作用域、声明顺序、同名屏蔽 | `src/semantic.cpp` | `nested_shadowing.tc`、use-before-declaration 测试 |
| 空语句、表达式语句、赋值和声明语句 | `src/parser.y`、`src/codegen.cpp` | `assignment.tc`、`void_function.tc` |
| if/else 与 dangling-else | `src/parser.y`、`src/codegen.cpp` | `if_else.tc`、`dangling_else.tc` |
| while、break、continue 与循环嵌套 | `src/semantic.cpp`、`src/codegen.cpp` | `nested_loops.tc`、循环外错误测试 |
| int/void return 与所有路径返回检查 | `src/semantic.cpp` | constant return 测试、missing/invalid return 测试 |
| 一元 `+ - !` | `src/parser.y`、`src/codegen.cpp` | `unary.tc`、`all_operators.tc` |
| `* / % + -` 及正确优先级、结合性 | `src/parser.y`、`src/codegen.cpp` | `binary_precedence.tc`、`all_operators.tc` |
| `< > <= >= == !=` | `src/parser.y`、`src/codegen.cpp` | `all_operators.tc` |
| `&& ||` 短路及 0/非 0 布尔规则 | `src/codegen.cpp` | `short_circuit.tc`、`short_circuit_side_effects.tc` |
| 函数调用表达式及 void 值使用约束 | `src/semantic.cpp` | nested call 与 void condition/assignment/argument 测试 |
| 全局变量静态存储与运行时读写 | `src/codegen.cpp` | `global_mutation.tc` |
| RV32 前 8 参数寄存器、额外参数栈传递 | `src/codegen.cpp` | `eight_params.tc`、`nine_params.tc`、`ten_params.tc`、临时 12 参数测试 |
| 16 字节调用栈对齐与递归调用 | `src/codegen.cpp` | `nested_call_alignment.tc`、`factorial_recursion.tc` |
| 大栈帧及超出 12 位的加载/存储偏移 | `src/codegen.cpp` | 临时 600 局部变量真实执行测试 |
| CMake 与 Make 构建 | `CMakeLists.txt`、`Makefile` | WSL 双构建验证 |

## 当前验证结果

- 普通模式合法程序：36/36
- 普通模式非法程序：27/27
- `-opt` 合法程序：36/36
- 固定随机种子 GCC 差分测试：40/40
- 大栈帧测试：600 个局部变量，退出码正确
- 多参数测试：最多验证到 12 个参数，退出码正确

数组、指针、I/O 和多文件编译不属于 ToyC 语言定义，因此不在实现范围内。
