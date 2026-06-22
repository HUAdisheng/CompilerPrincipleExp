#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toyc {

struct SourceLocation {
    int line = 0;
    int column = 0;
};

enum class ValueType {
    Int,
    Void,
};

class Node {
public:
    virtual ~Node() = default;

    SourceLocation location{};
};

class Expr : public Node {
public:
    ~Expr() override = default;
};

class IntLiteral final : public Expr {
public:
    explicit IntLiteral(std::int32_t literalValue);

    std::int32_t value;
};

class IdentifierExpr final : public Expr {
public:
    explicit IdentifierExpr(std::string identifierName);

    std::string name;
};

enum class UnaryOp {
    Plus,
    Minus,
    LogicalNot,
};

class UnaryExpr final : public Expr {
public:
    UnaryExpr(UnaryOp unaryOp, Expr* subExpression);

    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual,
    LogicalAnd,
    LogicalOr,
};

class BinaryExpr final : public Expr {
public:
    BinaryExpr(BinaryOp binaryOp, Expr* leftExpression, Expr* rightExpression);

    BinaryOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

class CallExpr final : public Expr {
public:
    CallExpr(std::string calleeName, std::vector<Expr*>* rawArguments);

    std::string callee;
    std::vector<std::unique_ptr<Expr>> arguments;
};

class Decl final : public Node {
public:
    Decl(bool constant, std::string declaredName, Expr* initExpression);

    bool isConst;
    std::string name;
    std::unique_ptr<Expr> init;
};

class Stmt : public Node {
public:
    ~Stmt() override = default;
};

class BlockStmt final : public Stmt {
public:
    explicit BlockStmt(std::vector<Stmt*>* rawStatements);

    std::vector<std::unique_ptr<Stmt>> statements;
};

class EmptyStmt final : public Stmt {};

class ExprStmt final : public Stmt {
public:
    explicit ExprStmt(Expr* statementExpression);

    std::unique_ptr<Expr> expr;
};

class AssignStmt final : public Stmt {
public:
    AssignStmt(std::string targetName, Expr* valueExpression);

    std::string name;
    std::unique_ptr<Expr> value;
};

class DeclStmt final : public Stmt {
public:
    explicit DeclStmt(Decl* declaredItem);

    std::unique_ptr<Decl> decl;
};

class IfStmt final : public Stmt {
public:
    IfStmt(Expr* conditionExpr, Stmt* thenStatement, Stmt* elseStatement);

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

class WhileStmt final : public Stmt {
public:
    WhileStmt(Expr* conditionExpr, Stmt* bodyStatement);

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

class BreakStmt final : public Stmt {};

class ContinueStmt final : public Stmt {};

class ReturnStmt final : public Stmt {
public:
    explicit ReturnStmt(Expr* returnExpression);

    std::unique_ptr<Expr> expr;
};

class TopLevelItem : public Node {
public:
    ~TopLevelItem() override = default;
};

class GlobalDecl final : public TopLevelItem {
public:
    explicit GlobalDecl(Decl* declaredItem);

    std::unique_ptr<Decl> decl;
};

class FuncDef final : public TopLevelItem {
public:
    FuncDef(ValueType declaredReturnType, std::string functionName,
            std::vector<std::string>* rawParameters, BlockStmt* functionBody);

    ValueType returnType;
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<BlockStmt> body;
};

class CompUnit final : public Node {
public:
    explicit CompUnit(std::vector<TopLevelItem*>* rawItems);

    std::vector<std::unique_ptr<TopLevelItem>> items;
};

extern std::unique_ptr<CompUnit> parsedProgram;

}  // namespace toyc
