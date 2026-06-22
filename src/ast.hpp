#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace toyc {

class Expr {
public:
    virtual ~Expr() = default;
    [[nodiscard]] virtual std::int32_t constantValue() const = 0;
};

class IntLiteral final : public Expr {
public:
    explicit IntLiteral(std::int32_t value);

    [[nodiscard]] std::int32_t constantValue() const override;

private:
    std::int32_t value_;
};

enum class UnaryOp {
    Plus,
    Minus,
    LogicalNot,
};

class UnaryExpr final : public Expr {
public:
    UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand);

    [[nodiscard]] std::int32_t constantValue() const override;

private:
    UnaryOp op_;
    std::unique_ptr<Expr> operand_;
};

class Program {
public:
    Program(std::string functionName, std::unique_ptr<Expr> returnExpr);

    [[nodiscard]] const std::string& functionName() const;
    [[nodiscard]] const Expr& returnExpr() const;

private:
    std::string functionName_;
    std::unique_ptr<Expr> returnExpr_;
};

extern std::unique_ptr<Program> parsedProgram;

}  // namespace toyc
