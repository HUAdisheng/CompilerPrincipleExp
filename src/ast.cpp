#include "ast.hpp"

#include <utility>

namespace toyc {

std::unique_ptr<CompUnit> parsedProgram;

IntLiteral::IntLiteral(std::int32_t literalValue) : value(literalValue) {}

IdentifierExpr::IdentifierExpr(std::string identifierName)
    : name(std::move(identifierName)) {}

UnaryExpr::UnaryExpr(UnaryOp unaryOp, Expr* subExpression)
    : op(unaryOp), operand(subExpression) {}

BinaryExpr::BinaryExpr(BinaryOp binaryOp, Expr* leftExpression, Expr* rightExpression)
    : op(binaryOp), lhs(leftExpression), rhs(rightExpression) {}

CallExpr::CallExpr(std::string calleeName, std::vector<Expr*>* rawArguments)
    : callee(std::move(calleeName)) {
    if (rawArguments != nullptr) {
        arguments.reserve(rawArguments->size());
        for (Expr* argument : *rawArguments) {
            arguments.emplace_back(argument);
        }
        delete rawArguments;
    }
}

Decl::Decl(bool constant, std::string declaredName, Expr* initExpression)
    : isConst(constant), name(std::move(declaredName)), init(initExpression) {}

BlockStmt::BlockStmt(std::vector<Stmt*>* rawStatements) {
    if (rawStatements != nullptr) {
        statements.reserve(rawStatements->size());
        for (Stmt* statement : *rawStatements) {
            statements.emplace_back(statement);
        }
        delete rawStatements;
    }
}

ExprStmt::ExprStmt(Expr* statementExpression) : expr(statementExpression) {}

AssignStmt::AssignStmt(std::string targetName, Expr* valueExpression)
    : name(std::move(targetName)), value(valueExpression) {}

DeclStmt::DeclStmt(Decl* declaredItem) : decl(declaredItem) {}

IfStmt::IfStmt(Expr* conditionExpr, Stmt* thenStatement, Stmt* elseStatement)
    : condition(conditionExpr), thenBranch(thenStatement), elseBranch(elseStatement) {}

WhileStmt::WhileStmt(Expr* conditionExpr, Stmt* bodyStatement)
    : condition(conditionExpr), body(bodyStatement) {}

ReturnStmt::ReturnStmt(Expr* returnExpression) : expr(returnExpression) {}

GlobalDecl::GlobalDecl(Decl* declaredItem) : decl(declaredItem) {}

FuncDef::FuncDef(ValueType declaredReturnType, std::string functionName,
                 std::vector<std::string>* rawParameters, BlockStmt* functionBody)
    : returnType(declaredReturnType), name(std::move(functionName)), body(functionBody) {
    if (rawParameters != nullptr) {
        params = std::move(*rawParameters);
        delete rawParameters;
    }
}

CompUnit::CompUnit(std::vector<TopLevelItem*>* rawItems) {
    if (rawItems != nullptr) {
        items.reserve(rawItems->size());
        for (TopLevelItem* item : *rawItems) {
            items.emplace_back(item);
        }
        delete rawItems;
    }
}

}  // namespace toyc
