## 测试说明

当前测试已经按里程碑分层放在 `clone/tests/`：

- `milestone1/`：最小闭环，一元表达式和常量返回
- `milestone2/`：二元表达式、优先级、括号、逻辑关系运算
- `milestone3/`：局部变量、赋值、作用域遮蔽
- `milestone4/`：`if/else`、`while`、`break`、`continue`
- `milestone5/`：函数调用、递归、全局变量/常量、`void` 函数、8 参数
- `milestone6/`：语义错误用例
- `milestone7/`：常量折叠、短路逻辑
- `smoke/`：自动测试清单

### 一键测试

在项目根目录执行：

```bash
clone/tests/run_tests.sh
```

脚本会自动执行：

```bash
cmake -S clone -B clone/build
cmake --build clone/build
```

然后批量运行全部测试。

### 如何看结果

脚本会输出每个用例的通过情况，例如：

```text
PASS valid  tests/milestone4/while_loop.tc
PASS invalid tests/milestone6/error_assign_const.tc
```

最后会输出汇总：

```text
Summary:
  valid   18/18 passed
  invalid 14/14 passed
```

### 查看中间输出

每个测试的标准输出和标准错误会保存到：

```text
clone/tests/.out/
```

例如：

- 合法用例的汇编输出：`clone/tests/.out/while_loop.valid.stdout`
- 合法用例的错误输出：`clone/tests/.out/while_loop.valid.stderr`
- 非法用例的错误输出：`clone/tests/.out/error_assign_const.invalid.stderr`

### 单独测试某个用例

例如：

```bash
clone/build/compiler < clone/tests/milestone5/function_call.tc
clone/build/compiler < clone/tests/milestone6/error_param_count.tc
```

如果要测 `-opt`，可以手动执行：

```bash
clone/build/compiler -opt < clone/tests/milestone7/const_folding.tc
```
