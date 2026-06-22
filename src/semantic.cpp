#include "semantic.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace toyc {
namespace {

struct VariableInfo {
    bool isConst = false;
    bool hasConstValue = false;
    std::int32_t constValue = 0;
};

struct ConstEvalResult {
    bool ok = false;
    std::int32_t value = 0;
};

class Analyzer {
public:
    explicit Analyzer(const CompileOptions& compileOptions) : options_(compileOptions) {}

    SemanticResult run(const CompUnit& program) {
        (void)options_;
        for (const auto& item : program.items) {
            analyzeTopLevel(*item);
        }

        const auto mainIt = result_.program.functions.find("main");
        if (mainIt == result_.program.functions.end()) {
            addDiagnostic(program.location, "missing entry function main");
        } else if (mainIt->second.returnType != ValueType::Int || mainIt->second.paramCount != 0) {
            addDiagnostic(program.location, "main must have type int main()");
        }

        result_.ok = result_.diagnostics.empty();
        return result_;
    }

private:
    struct Scope {
        std::unordered_map<std::string, VariableInfo> variables;
    };

    const CompileOptions& options_;
    SemanticResult result_;
    std::vector<Scope> scopes_;
    std::unordered_set<std::string> occupiedGlobalNames_;
    std::unordered_map<std::string, FunctionSignature> visibleFunctions_;
    std::unordered_map<const Expr*, std::int32_t> constantConditions_;
    ValueType currentReturnType_ = ValueType::Void;
    std::string currentFunctionName_;
    int loopDepth_ = 0;

    void addDiagnostic(SourceLocation location, std::string message) {
        result_.diagnostics.push_back(Diagnostic{location, std::move(message)});
    }

    void analyzeTopLevel(const TopLevelItem& item) {
        if (const auto* global = dynamic_cast<const GlobalDecl*>(&item)) {
            analyzeGlobalDecl(*global);
            return;
        }
        const auto* function = dynamic_cast<const FuncDef*>(&item);
        if (function != nullptr) {
            analyzeFunction(*function);
        }
    }

    void analyzeGlobalDecl(const GlobalDecl& global) {
        const Decl& decl = *global.decl;
        if (!declareGlobalName(decl.name, global.location, "redefinition of global name " + decl.name)) {
            return;
        }

        const auto initType = analyzeExpr(*decl.init);
        if (initType != ValueType::Int) {
            addDiagnostic(decl.init->location, "global initializer must have int type");
        }

        if (!isConstExpression(*decl.init)) {
            addDiagnostic(decl.init->location,
                          "global initializer must be a compile-time constant expression");
            return;
        }
        const auto initValue = evaluateConstExpr(*decl.init);
        if (!initValue.ok) {
            addDiagnostic(decl.init->location,
                          "global initializer must be a valid constant expression");
            return;
        }

        if (decl.isConst) {
            result_.program.globalConsts.emplace(decl.name, initValue.value);
        } else {
            result_.program.globalVars.emplace(decl.name, initValue.value);
            result_.program.globalVarOrder.push_back(decl.name);
        }
    }

    void analyzeFunction(const FuncDef& function) {
        if (!declareGlobalName(function.name, function.location,
                               "redefinition of global name " + function.name)) {
            return;
        }

        FunctionSignature signature;
        signature.returnType = function.returnType;
        signature.paramCount = function.params.size();
        result_.program.functions.emplace(function.name, signature);
        visibleFunctions_.emplace(function.name, signature);
        result_.program.functionOrder.push_back(&function);

        currentReturnType_ = function.returnType;
        currentFunctionName_ = function.name;
        loopDepth_ = 0;
        scopes_.clear();
        pushScope();

        for (const auto& param : function.params) {
            if (!declareLocal(param, VariableInfo{}, function.location,
                              "duplicate parameter name " + param)) {
                continue;
            }
        }

        analyzeBlock(*function.body, false);

        if (function.returnType == ValueType::Int && !stmtAlwaysReturns(*function.body)) {
            addDiagnostic(function.location,
                          "int function " + function.name + " must return a value on all paths");
        }

        scopes_.clear();
    }

    bool declareGlobalName(const std::string& name, SourceLocation location,
                           const std::string& duplicateMessage) {
        if (occupiedGlobalNames_.contains(name)) {
            addDiagnostic(location, duplicateMessage);
            return false;
        }
        occupiedGlobalNames_.insert(name);
        return true;
    }

    void pushScope() {
        scopes_.push_back(Scope{});
    }

    void popScope() {
        scopes_.pop_back();
    }

    bool declareLocal(const std::string& name, VariableInfo info, SourceLocation location,
                      const std::string& duplicateMessage) {
        auto& current = scopes_.back().variables;
        if (current.contains(name)) {
            addDiagnostic(location, duplicateMessage);
            return false;
        }
        current.emplace(name, info);
        return true;
    }

    const VariableInfo* lookupVariable(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->variables.find(name);
            if (found != it->variables.end()) {
                return &found->second;
            }
        }

        if (result_.program.globalConsts.contains(name)) {
            static VariableInfo globalConst;
            globalConst.isConst = true;
            globalConst.hasConstValue = true;
            globalConst.constValue = result_.program.globalConsts.at(name);
            return &globalConst;
        }

        if (result_.program.globalVars.contains(name)) {
            static VariableInfo globalVar;
            globalVar.isConst = false;
            globalVar.hasConstValue = false;
            globalVar.constValue = 0;
            return &globalVar;
        }

        return nullptr;
    }

    ValueType analyzeExpr(const Expr& expr) {
        if (dynamic_cast<const IntLiteral*>(&expr) != nullptr) {
            return ValueType::Int;
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            const auto* variable = lookupVariable(identifier->name);
            if (variable == nullptr) {
                addDiagnostic(expr.location, "use of undeclared identifier " + identifier->name);
                return ValueType::Int;
            }
            return ValueType::Int;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            const auto operandType = analyzeExpr(*unary->operand);
            if (operandType != ValueType::Int) {
                addDiagnostic(expr.location, "unary operator requires int operand");
            }
            return ValueType::Int;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            const auto lhsType = analyzeExpr(*binary->lhs);
            const auto rhsType = analyzeExpr(*binary->rhs);
            if (lhsType != ValueType::Int || rhsType != ValueType::Int) {
                addDiagnostic(expr.location, "binary operator requires int operands");
            }
            return ValueType::Int;
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            const auto found = visibleFunctions_.find(call->callee);
            if (found == visibleFunctions_.end()) {
                addDiagnostic(expr.location,
                              "call to function " + call->callee +
                                  " before its definition or to an undeclared function");
                for (const auto& argument : call->arguments) {
                    analyzeExpr(*argument);
                }
                return ValueType::Int;
            }

            if (found->second.paramCount != call->arguments.size()) {
                addDiagnostic(expr.location,
                              "function " + call->callee + " expects " +
                                  std::to_string(found->second.paramCount) + " arguments");
            }
            for (const auto& argument : call->arguments) {
                if (analyzeExpr(*argument) != ValueType::Int) {
                    addDiagnostic(argument->location, "function argument must have int type");
                }
            }
            return found->second.returnType;
        }

        return ValueType::Int;
    }

    ConstEvalResult evaluateConstExpr(const Expr& expr) const {
        if (const auto* literal = dynamic_cast<const IntLiteral*>(&expr)) {
            return ConstEvalResult{true, literal->value};
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                const auto found = it->variables.find(identifier->name);
                if (found != it->variables.end()) {
                    if (found->second.isConst && found->second.hasConstValue) {
                        return ConstEvalResult{true, found->second.constValue};
                    }
                    return ConstEvalResult{};
                }
            }

            const auto globalConst = result_.program.globalConsts.find(identifier->name);
            if (globalConst != result_.program.globalConsts.end()) {
                return ConstEvalResult{true, globalConst->second};
            }
            return ConstEvalResult{};
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            const auto operand = evaluateConstExpr(*unary->operand);
            if (!operand.ok) {
                return ConstEvalResult{};
            }
            switch (unary->op) {
                case UnaryOp::Plus:
                    return operand;
                case UnaryOp::Minus:
                    return ConstEvalResult{
                        true,
                        static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(operand.value))};
                case UnaryOp::LogicalNot:
                    return ConstEvalResult{true, operand.value == 0 ? 1 : 0};
            }
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            if (binary->op == BinaryOp::LogicalAnd) {
                const auto lhs = evaluateConstExpr(*binary->lhs);
                if (!lhs.ok) {
                    return ConstEvalResult{};
                }
                if (lhs.value == 0) {
                    return ConstEvalResult{true, 0};
                }
                const auto rhs = evaluateConstExpr(*binary->rhs);
                if (!rhs.ok) {
                    return ConstEvalResult{};
                }
                return ConstEvalResult{true, rhs.value != 0 ? 1 : 0};
            }

            if (binary->op == BinaryOp::LogicalOr) {
                const auto lhs = evaluateConstExpr(*binary->lhs);
                if (!lhs.ok) {
                    return ConstEvalResult{};
                }
                if (lhs.value != 0) {
                    return ConstEvalResult{true, 1};
                }
                const auto rhs = evaluateConstExpr(*binary->rhs);
                if (!rhs.ok) {
                    return ConstEvalResult{};
                }
                return ConstEvalResult{true, rhs.value != 0 ? 1 : 0};
            }

            const auto lhs = evaluateConstExpr(*binary->lhs);
            const auto rhs = evaluateConstExpr(*binary->rhs);
            if (!lhs.ok || !rhs.ok) {
                return ConstEvalResult{};
            }

            switch (binary->op) {
                case BinaryOp::Add:
                    return ConstEvalResult{true, static_cast<std::int32_t>(
                                                     static_cast<std::uint32_t>(lhs.value) +
                                                     static_cast<std::uint32_t>(rhs.value))};
                case BinaryOp::Sub:
                    return ConstEvalResult{true, static_cast<std::int32_t>(
                                                     static_cast<std::uint32_t>(lhs.value) -
                                                     static_cast<std::uint32_t>(rhs.value))};
                case BinaryOp::Mul:
                    return ConstEvalResult{
                        true, static_cast<std::int32_t>(lhs.value * rhs.value)};
                case BinaryOp::Div:
                    if (rhs.value == 0) {
                        return ConstEvalResult{};
                    }
                    return ConstEvalResult{true, static_cast<std::int32_t>(lhs.value / rhs.value)};
                case BinaryOp::Mod:
                    if (rhs.value == 0) {
                        return ConstEvalResult{};
                    }
                    return ConstEvalResult{true, static_cast<std::int32_t>(lhs.value % rhs.value)};
                case BinaryOp::Less:
                    return ConstEvalResult{true, lhs.value < rhs.value ? 1 : 0};
                case BinaryOp::Greater:
                    return ConstEvalResult{true, lhs.value > rhs.value ? 1 : 0};
                case BinaryOp::LessEqual:
                    return ConstEvalResult{true, lhs.value <= rhs.value ? 1 : 0};
                case BinaryOp::GreaterEqual:
                    return ConstEvalResult{true, lhs.value >= rhs.value ? 1 : 0};
                case BinaryOp::Equal:
                    return ConstEvalResult{true, lhs.value == rhs.value ? 1 : 0};
                case BinaryOp::NotEqual:
                    return ConstEvalResult{true, lhs.value != rhs.value ? 1 : 0};
                case BinaryOp::LogicalAnd:
                case BinaryOp::LogicalOr:
                    break;
            }
        }

        return ConstEvalResult{};
    }

    bool isConstExpression(const Expr& expr) const {
        if (dynamic_cast<const IntLiteral*>(&expr) != nullptr) {
            return true;
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                const auto found = it->variables.find(identifier->name);
                if (found != it->variables.end()) {
                    return found->second.isConst && found->second.hasConstValue;
                }
            }
            return result_.program.globalConsts.contains(identifier->name);
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            return isConstExpression(*unary->operand);
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            return isConstExpression(*binary->lhs) &&
                   isConstExpression(*binary->rhs);
        }

        return false;
    }

    void analyzeBlock(const BlockStmt& block, bool createScope) {
        if (createScope) {
            pushScope();
        }

        for (const auto& statement : block.statements) {
            analyzeStmt(*statement);
        }

        if (createScope) {
            popScope();
        }
    }

    void analyzeStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            analyzeBlock(*block, true);
            return;
        }

        if (dynamic_cast<const EmptyStmt*>(&stmt) != nullptr) {
            return;
        }

        if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&stmt)) {
            analyzeExpr(*exprStmt->expr);
            return;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            const auto* variable = lookupVariable(assign->name);
            if (variable == nullptr) {
                addDiagnostic(stmt.location, "assignment to undeclared identifier " + assign->name);
            } else if (variable->isConst) {
                addDiagnostic(stmt.location, "cannot assign to const identifier " + assign->name);
            }

            if (analyzeExpr(*assign->value) != ValueType::Int) {
                addDiagnostic(assign->value->location, "assignment value must have int type");
            }
            return;
        }

        if (const auto* declStmt = dynamic_cast<const DeclStmt*>(&stmt)) {
            analyzeDecl(*declStmt->decl);
            return;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            if (analyzeExpr(*ifStmt->condition) != ValueType::Int) {
                addDiagnostic(ifStmt->condition->location, "if condition must have int type");
            }
            const auto conditionValue = evaluateConstExpr(*ifStmt->condition);
            if (conditionValue.ok) {
                constantConditions_.emplace(ifStmt->condition.get(), conditionValue.value);
            }
            analyzeStmt(*ifStmt->thenBranch);
            if (ifStmt->elseBranch) {
                analyzeStmt(*ifStmt->elseBranch);
            }
            return;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            if (analyzeExpr(*whileStmt->condition) != ValueType::Int) {
                addDiagnostic(whileStmt->condition->location, "while condition must have int type");
            }
            const auto conditionValue = evaluateConstExpr(*whileStmt->condition);
            if (conditionValue.ok) {
                constantConditions_.emplace(whileStmt->condition.get(), conditionValue.value);
            }
            ++loopDepth_;
            analyzeStmt(*whileStmt->body);
            --loopDepth_;
            return;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt) != nullptr) {
            if (loopDepth_ <= 0) {
                addDiagnostic(stmt.location, "break can only appear inside a loop");
            }
            return;
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt) != nullptr) {
            if (loopDepth_ <= 0) {
                addDiagnostic(stmt.location, "continue can only appear inside a loop");
            }
            return;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (currentReturnType_ == ValueType::Void) {
                if (ret->expr) {
                    addDiagnostic(stmt.location,
                                  "void function " + currentFunctionName_ +
                                      " cannot return a value");
                }
            } else {
                if (!ret->expr) {
                    addDiagnostic(stmt.location,
                                  "int function " + currentFunctionName_ +
                                      " must return an int value");
                } else if (analyzeExpr(*ret->expr) != ValueType::Int) {
                    addDiagnostic(ret->expr->location, "return expression must have int type");
                }
            }
            return;
        }
    }

    void analyzeDecl(const Decl& decl) {
        if (analyzeExpr(*decl.init) != ValueType::Int) {
            addDiagnostic(decl.init->location, "initializer must have int type");
        }

        VariableInfo info;
        info.isConst = decl.isConst;
        if (decl.isConst) {
            if (!isConstExpression(*decl.init)) {
                addDiagnostic(decl.init->location,
                              "const initializer must be a compile-time constant expression");
            } else {
                const auto initValue = evaluateConstExpr(*decl.init);
                if (!initValue.ok) {
                    addDiagnostic(decl.init->location,
                                  "const initializer must be a valid constant expression");
                } else {
                    info.hasConstValue = true;
                    info.constValue = initValue.value;
                }
            }
        }

        declareLocal(decl.name, info, decl.location, "redefinition of local name " + decl.name);
    }

    bool stmtAlwaysReturns(const Stmt& stmt) const {
        if (dynamic_cast<const ReturnStmt*>(&stmt) != nullptr) {
            return true;
        }

        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            for (const auto& child : block->statements) {
                if (stmtAlwaysReturns(*child)) {
                    return true;
                }
            }
            return false;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            const auto constant = constantConditions_.find(ifStmt->condition.get());
            if (constant != constantConditions_.end()) {
                if (constant->second != 0) {
                    return stmtAlwaysReturns(*ifStmt->thenBranch);
                }
                return ifStmt->elseBranch != nullptr &&
                       stmtAlwaysReturns(*ifStmt->elseBranch);
            }
            return ifStmt->elseBranch != nullptr && stmtAlwaysReturns(*ifStmt->thenBranch) &&
                   stmtAlwaysReturns(*ifStmt->elseBranch);
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            const auto constant = constantConditions_.find(whileStmt->condition.get());
            return constant != constantConditions_.end() && constant->second != 0 &&
                   stmtAlwaysReturns(*whileStmt->body);
        }

        return false;
    }
};

}  // namespace

SemanticResult analyzeProgram(const CompUnit& program, const CompileOptions& options) {
    Analyzer analyzer(options);
    return analyzer.run(program);
}

}  // namespace toyc
