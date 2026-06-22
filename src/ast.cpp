#include "ast.hpp"

#include <utility>

namespace toyc {

std::unique_ptr<Program> parsedProgram;

IntLiteral::IntLiteral(std::int32_t value) : value_(value) {}

std::int32_t IntLiteral::constantValue() const {
    return value_;
}

UnaryExpr::UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand)
    : op_(op), operand_(std::move(operand)) {}

std::int32_t UnaryExpr::constantValue() const {
    const auto value = operand_->constantValue();
    switch (op_) {
        case UnaryOp::Plus:
            return value;
        case UnaryOp::Minus:
            return static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(value));
        case UnaryOp::LogicalNot:
            return value == 0 ? 1 : 0;
    }
    return 0;
}

Program::Program(std::string functionName, std::unique_ptr<Expr> returnExpr)
    : functionName_(std::move(functionName)), returnExpr_(std::move(returnExpr)) {}

const std::string& Program::functionName() const {
    return functionName_;
}

const Expr& Program::returnExpr() const {
    return *returnExpr_;
}

}  // namespace toyc
