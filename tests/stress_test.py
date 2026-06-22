# ============================================================
# ToyC 压力测试 / 随机程序生成器
# 用法: python3 tests/stress_test.py [compiler_path] [count] [seed]
#
# 原理:
#   1. 随机生成合法的 ToyC 程序
#   2. 同时用 gcc 编译 (得到"正确答案"的退出码)
#   3. 用本编译器生成 RV32 汇编, clang/lld 链接并用 qemu-riscv32 执行
#   4. 比较两者, 不一致则报告
#
# 依赖: python3, gcc, clang, ld.lld, qemu-riscv32
# ============================================================

import subprocess
import random
import sys
import os
import tempfile
import shutil

COMPILER = sys.argv[1] if len(sys.argv) > 1 else "./build/compiler"
COUNT = int(sys.argv[2]) if len(sys.argv) > 2 else 50
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else None
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__)) + "/.."
RUNTIME_DIR = os.path.join(PROJECT_DIR, "tests", "runtime")

# 工具检查
def check_bin(name):
    return shutil.which(name) is not None

HAS_RISCV = (check_bin("clang") and check_bin("ld.lld") and
             check_bin("qemu-riscv32"))
HAS_GCC = check_bin("gcc")

class ToyCGenerator:
    """随机生成合法的 ToyC 程序"""

    def __init__(self):
        self.op_pool = ["+", "-", "*"]
        self.rel_pool = ["<", ">", "<=", ">=", "==", "!="]
        self.used_funcs = {}  # name -> param_count
        self.func_count = 0

    def gen_number(self) -> str:
        return str(random.randint(-100, 100))

    def gen_expr(self, depth: int = 0) -> str:
        """生成随机表达式, depth 控制嵌套深度"""
        if depth > 5:
            return self.gen_number()

        choice = random.random()
        if choice < 0.15:
            # 字面量
            return self.gen_number()
        elif choice < 0.25:
            # 当前表达式生成器不携带作用域信息，使用字面量避免生成未声明变量
            return self.gen_number()
        elif choice < 0.35:
            # 括号
            return f"({self.gen_expr(depth + 1)})"
        elif choice < 0.55:
            # 二元运算
            op = random.choice(self.op_pool + self.rel_pool)
            return f"{self.gen_expr(depth + 1)} {op} {self.gen_expr(depth + 1)}"
        elif choice < 0.65:
            # 一元运算
            op = random.choice(["+", "-", "!"])
            # 加括号可避免 “-” 与负字面量拼成 C 的自减记号，例如 --83
            return f"{op}({self.gen_expr(depth + 1)})"
        elif choice < 0.75:
            # 逻辑运算
            op = random.choice(["&&", "||"])
            return f"{self.gen_expr(depth + 1)} {op} {self.gen_expr(depth + 1)}"
        else:
            # 函数调用
            if self.used_funcs:
                fname = random.choice(list(self.used_funcs.keys()))
                pc = self.used_funcs[fname]
                args = ", ".join([self.gen_number() for _ in range(pc)])
                return f"{fname}({args})"
            return self.gen_number()

    def _gen_main_body(self) -> str:
        """生成 main 函数体"""
        styles = [
            self._gen_compute_style,
            self._gen_loop_style,
            self._gen_if_style,
            self._gen_mixed_style,
        ]
        return random.choice(styles)()

    def _gen_compute_style(self) -> str:
        """纯计算型 main"""
        lines = []
        var_count = random.randint(2, 6)
        used_vars = []
        for i in range(var_count):
            vname = chr(ord('a') + i)
            expr = self.gen_expr(2)
            lines.append(f"    int {vname} = {expr};")
            used_vars.append(vname)
        # 最后用所有变量算一个返回值
        if used_vars:
            ret_expr = " + ".join(used_vars)
            lines.append(f"    return {ret_expr};")
        else:
            lines.append(f"    return {self.gen_number()};")
        return "\n".join(lines)

    def _gen_loop_style(self) -> str:
        """循环型 main"""
        n = random.randint(3, 10)
        return f"""    int i = 0;
    int s = 0;
    while (i < {n}) {{
        s = s + i * 2;
        i = i + 1;
    }}
    return s;"""

    def _gen_if_style(self) -> str:
        """条件分支型 main"""
        a = random.randint(-5, 10)
        b = random.randint(-5, 10)
        return f"""    int x = {a};
    int y = {b};
    int r = 0;
    if (x > 0) {{
        r = x * 2;
    }} else {{
        r = y + 5;
    }}
    if (r < 0) {{
        r = 0 - r;
    }}
    return r;"""

    def _gen_mixed_style(self) -> str:
        """混合型: 声明 + 循环 + 条件"""
        return f"""    int a = {random.randint(1, 5)};
    int b = {random.randint(1, 5)};
    int r = 0;
    int i = 0;
    while (i < a) {{
        if (i % 2 == 0) {{
            r = r + b;
        }} else {{
            r = r - 1;
        }}
        i = i + 1;
    }}
    return r;"""

    def generate(self) -> str:
        """生成一个完整程序"""
        # 每个程序都是独立编译单元，不能沿用上一轮生成的函数声明。
        self.used_funcs.clear()

        # 可选: 生成辅助函数
        helpers = []
        if random.random() < 0.3:
            helpers.append(f"""int add2(int x) {{
    return x + 2;
}}""")
            self.used_funcs["add2"] = 1

        if random.random() < 0.2:
            helpers.append(f"""int sum3(int a, int b, int c) {{
    return a + b + c;
}}""")
            self.used_funcs["sum3"] = 3

        body = self._gen_main_body()

        program = "\n\n".join(helpers + [f"int main() {{\n{body}\n}}"]) + "\n"
        return program


def get_gcc_exit_code(source: str) -> tuple[int, str]:
    """用 gcc 编译并执行, 获取正确的退出码; 返回 (code, reason)"""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
        f.write(source)
        c_path = f.name

    exe_path = c_path + ".exe"
    try:
        result = subprocess.run(["gcc", c_path, "-o", exe_path, "-w"],
                                capture_output=True, text=True, timeout=10)
        if result.returncode != 0:
            err = result.stderr.strip().split('\n')[-1] if result.stderr.strip() else "gcc 编译失败(无具体错误)"
            return (-2, f"gcc编译失败: {err[:120]}")
        result = subprocess.run([exe_path], capture_output=True, timeout=10)
        return (result.returncode & 0xFF, "")
    except subprocess.TimeoutExpired:
        return (-1, "gcc编译/执行超时")
    except subprocess.CalledProcessError:
        return (-2, "gcc编译执行异常")
    finally:
        for p in [c_path, exe_path]:
            if os.path.exists(p):
                os.remove(p)


def get_toyc_exit_code(source: str) -> tuple[int, str]:
    """用 ToyC 编译器编译, qemu-riscv32 执行, 获取退出码"""
    # Step 1: ToyC → RISC-V asm
    result = subprocess.run([COMPILER],
                            input=source, capture_output=True, text=True, timeout=10)
    if result.returncode != 0:
        return (-3, f"编译器错误: {result.stderr[:100]}")

    asm = result.stdout
    tmpdir = tempfile.mkdtemp()
    asm_path = os.path.join(tmpdir, "out.s")
    start_obj_path = os.path.join(tmpdir, "linux_start.o")
    asm_obj_path = os.path.join(tmpdir, "out.o")
    elf_path = os.path.join(tmpdir, "out.elf")
    start_path = os.path.join(RUNTIME_DIR, "linux_start.s")

    try:
        with open(asm_path, 'w') as f:
            f.write(asm)

        # Step 2: RV32 汇编 → 静态 Linux ELF
        subprocess.run(["clang", "--target=riscv32-linux-gnu", "-c",
                        start_path, "-o", start_obj_path],
                       capture_output=True, check=True, timeout=10)
        subprocess.run(["clang", "--target=riscv32-linux-gnu", "-c",
                        asm_path, "-o", asm_obj_path],
                       capture_output=True, check=True, timeout=10)
        subprocess.run(["ld.lld", "-m", "elf32lriscv", "-static",
                        "-e", "_start", start_obj_path, asm_obj_path,
                        "-o", elf_path],
                       capture_output=True, check=True, timeout=10)

        # Step 3: qemu-riscv32 执行
        result = subprocess.run(["qemu-riscv32", elf_path],
                                capture_output=True, timeout=10)
        return (result.returncode & 0xFF, "")
    except subprocess.TimeoutExpired:
        return (-4, "RISC-V 编译或执行超时")
    except subprocess.CalledProcessError as e:
        stderr = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else e.stderr
        return (-5, f"汇编或链接失败: {stderr[:100] if stderr else 'unknown'}")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def main():
    if SEED is not None:
        random.seed(SEED)

    print("=" * 50)
    print("  ToyC 压力测试 / 对拍")
    print("  compiler:", COMPILER)
    print(f"  生成 {COUNT} 个随机程序")
    if SEED is not None:
        print(f"  随机种子: {SEED}")
    print("=" * 50)
    print()

    if not HAS_GCC:
        print("[SKIP] gcc 不可用, 需要 gcc 做对拍")
        return
    if not HAS_RISCV:
        print("[SKIP] RISC-V 工具链不可用, 需要 clang + ld.lld + qemu-riscv32")
        return

    gen = ToyCGenerator()
    passed = 0
    failed = 0
    skipped = 0

    for i in range(COUNT):
        source = gen.generate()
        gcc_code, gcc_reason = get_gcc_exit_code(source)

        if gcc_code < 0:
            skipped += 1
            print(f"  [{i+1}] SKIP ({gcc_reason})")
            print(f"    源码:\n{source}")
            continue

        toyc_code, err = get_toyc_exit_code(source)
        if toyc_code < 0:
            print(f"  [{i+1}] FAIL (编译器/模拟器错误): {err}")
            print(f"    源码:\n{source}")
            failed += 1
            continue

        if toyc_code != gcc_code:
            print(f"  [{i+1}] MISMATCH: gcc={gcc_code}, toyc={toyc_code}")
            print(f"    源码:\n{source}")
            failed += 1
        else:
            passed += 1
            if (i + 1) % 10 == 0:
                print(f"  [{i+1}/{COUNT}] {passed} passed, {failed} failed...")

    print()
    print("=" * 50)
    print(f"  结果: {passed} 通过, {failed} 不匹配, {skipped} 跳过")
    print("=" * 50)

    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    main()
