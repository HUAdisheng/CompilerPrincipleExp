#include "codegen.hpp"

#include "ast.hpp"

#include <ostream>

namespace toyc {

void generateRiscV(const Program& program, std::ostream& output) {
    output << "    .text\n"
           << "    .globl " << program.functionName() << '\n'
           << "    .type " << program.functionName() << ", @function\n"
           << program.functionName() << ":\n"
           << "    li a0, " << program.returnExpr().constantValue() << '\n'
           << "    ret\n"
           << "    .size " << program.functionName() << ", .-"
           << program.functionName() << '\n';
}

}  // namespace toyc
