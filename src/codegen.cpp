#include "codegen.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace toyc {
namespace {

struct Binding {
    enum class Kind {
        LocalVar,
        LocalConst,
        GlobalVar,
        GlobalConst,
    };

    Kind kind = Kind::LocalVar;
    int offset = 0;
    std::int32_t constValue = 0;
};

class CodeGenerator {
public:
    CodeGenerator(const CompUnit& program, const ProgramInfo& programInfo,
                  const CompileOptions& compileOptions, std::ostream& stream)
        : program_(program), info_(programInfo), options_(compileOptions), out_(stream) {}

    void run() {
        emitDataSection();
        emitTextSection();
    }

private:
    struct FunctionLayout {
        std::unordered_map<const Decl*, int> declOffsets;
        std::vector<int> paramOffsets;
        int frameSize = 16;
    };

    const CompUnit& program_;
    const ProgramInfo& info_;
    const CompileOptions& options_;
    std::ostream& out_;
    int labelCounter_ = 0;

    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::vector<std::pair<std::string, std::string>> loopLabels_;
    std::unordered_map<const Decl*, int> currentDeclOffsets_;
    std::vector<int> currentParamOffsets_;
    std::string currentExitLabel_;
    int dynamicStackDepth_ = 0;

    static int align16(int value) {
        return (value + 15) / 16 * 16;
    }

    static bool fitsI12(int value) {
        return value >= -2048 && value <= 2047;
    }

    std::string nextLabel(const std::string& prefix) {
        return ".L_" + prefix + "_" + std::to_string(labelCounter_++);
    }

    void emitLine(const std::string& text) {
        out_ << text << '\n';
    }

    void emitDataSection() {
        if (info_.globalVarOrder.empty()) {
            return;
        }

        emitLine("    .data");
        for (const auto& name : info_.globalVarOrder) {
            const auto it = info_.globalVars.find(name);
            if (it == info_.globalVars.end()) {
                continue;
            }
            emitLine("    .globl " + name);
            emitLine(name + ":");
            emitLine("    .word " + std::to_string(it->second));
        }
    }

    void emitTextSection() {
        emitLine("    .text");
        for (const FuncDef* function : info_.functionOrder) {
            emitFunction(*function);
        }
    }

    FunctionLayout buildFunctionLayout(const FuncDef& function) {
        FunctionLayout layout;
        layout.paramOffsets.reserve(function.params.size());

        int slotIndex = 0;
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            layout.paramOffsets.push_back(offsetForSlot(slotIndex));
            ++slotIndex;
        }

        collectDeclOffsets(*function.body, layout.declOffsets, slotIndex);
        layout.frameSize = align16(16 + slotIndex * 4);
        return layout;
    }

    static int offsetForSlot(int slotIndex) {
        return -12 - slotIndex * 4;
    }

    void collectDeclOffsets(const BlockStmt& block, std::unordered_map<const Decl*, int>& offsets,
                            int& slotIndex) {
        for (const auto& statement : block.statements) {
            collectStmtOffsets(*statement, offsets, slotIndex);
        }
    }

    void collectStmtOffsets(const Stmt& statement,
                            std::unordered_map<const Decl*, int>& offsets, int& slotIndex) {
        if (const auto* declStmt = dynamic_cast<const DeclStmt*>(&statement)) {
            offsets.emplace(declStmt->decl.get(), offsetForSlot(slotIndex));
            ++slotIndex;
            return;
        }
        if (const auto* block = dynamic_cast<const BlockStmt*>(&statement)) {
            collectDeclOffsets(*block, offsets, slotIndex);
            return;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
            collectStmtOffsets(*ifStmt->thenBranch, offsets, slotIndex);
            if (ifStmt->elseBranch != nullptr) {
                collectStmtOffsets(*ifStmt->elseBranch, offsets, slotIndex);
            }
            return;
        }
        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
            collectStmtOffsets(*whileStmt->body, offsets, slotIndex);
        }
    }

    void emitFunction(const FuncDef& function) {
        const auto layout = buildFunctionLayout(function);
        currentDeclOffsets_ = layout.declOffsets;
        currentParamOffsets_ = layout.paramOffsets;
        currentExitLabel_ = nextLabel(function.name + "_exit");
        dynamicStackDepth_ = 0;
        scopes_.clear();
        loopLabels_.clear();

        emitLine("    .globl " + function.name);
        emitLine("    .type " + function.name + ", @function");
        emitLine(function.name + ":");
        emitAdjustSp(-layout.frameSize);
        emitStoreWord("ra", layout.frameSize - 4, "sp");
        emitStoreWord("s0", layout.frameSize - 8, "sp");
        emitAddImmediate("s0", "sp", layout.frameSize);

        pushScope();
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            const int offset = currentParamOffsets_[i];
            if (i < 8) {
                emitStoreWord("a" + std::to_string(i), offset, "s0");
            } else {
                emitLoadWord("t0", static_cast<int>((i - 8) * 4), "s0");
                emitStoreWord("t0", offset, "s0");
            }
            scopes_.back().emplace(function.params[i],
                                   Binding{Binding::Kind::LocalVar, offset, 0});
        }

        emitBlock(*function.body, false);

        if (function.returnType == ValueType::Void) {
            emitLine("    li a0, 0");
        }
        emitLine(currentExitLabel_ + ":");
        emitLoadWord("ra", layout.frameSize - 4, "sp");
        emitLoadWord("s0", layout.frameSize - 8, "sp");
        emitAdjustSp(layout.frameSize);
        emitLine("    ret");
        emitLine("    .size " + function.name + ", .-" + function.name);

        scopes_.clear();
        currentDeclOffsets_.clear();
        currentParamOffsets_.clear();
        currentExitLabel_.clear();
    }

    void pushScope() {
        scopes_.push_back({});
    }

    void popScope() {
        scopes_.pop_back();
    }

    const Binding* lookupBinding(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }

        const auto globalConst = info_.globalConsts.find(name);
        if (globalConst != info_.globalConsts.end()) {
            static Binding binding;
            binding.kind = Binding::Kind::GlobalConst;
            binding.constValue = globalConst->second;
            return &binding;
        }

        const auto globalVar = info_.globalVars.find(name);
        if (globalVar != info_.globalVars.end()) {
            static Binding binding;
            binding.kind = Binding::Kind::GlobalVar;
            binding.offset = 0;
            binding.constValue = 0;
            return &binding;
        }

        return nullptr;
    }

    void emitBlock(const BlockStmt& block, bool createScope) {
        if (createScope) {
            pushScope();
        }

        for (const auto& statement : block.statements) {
            emitStmt(*statement);
        }

        if (createScope) {
            popScope();
        }
    }

    void emitStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            emitBlock(*block, true);
            return;
        }

        if (dynamic_cast<const EmptyStmt*>(&stmt) != nullptr) {
            return;
        }

        if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&stmt)) {
            emitExpr(*exprStmt->expr);
            return;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            if (options_.enableOpt) {
                std::int32_t value = 0;
                if (tryEvaluateConstExpr(*assign->value, value)) {
                    emitLoadImmediate("a0", value);
                    emitStore(assign->name);
                    return;
                }
            }
            emitExpr(*assign->value);
            emitStore(assign->name);
            return;
        }

        if (const auto* declStmt = dynamic_cast<const DeclStmt*>(&stmt)) {
            emitDecl(*declStmt->decl);
            return;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            if (options_.enableOpt) {
                std::int32_t conditionValue = 0;
                if (tryEvaluateConstExpr(*ifStmt->condition, conditionValue)) {
                    if (conditionValue != 0) {
                        emitStmt(*ifStmt->thenBranch);
                    } else if (ifStmt->elseBranch != nullptr) {
                        emitStmt(*ifStmt->elseBranch);
                    }
                    return;
                }
            }

            const std::string elseLabel = nextLabel("if_else");
            const std::string endLabel = nextLabel("if_end");
            emitExpr(*ifStmt->condition);
            emitLine("    beqz a0, " + elseLabel);
            emitStmt(*ifStmt->thenBranch);
            emitLine("    j " + endLabel);
            emitLine(elseLabel + ":");
            if (ifStmt->elseBranch != nullptr) {
                emitStmt(*ifStmt->elseBranch);
            }
            emitLine(endLabel + ":");
            return;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            if (options_.enableOpt) {
                std::int32_t conditionValue = 0;
                if (tryEvaluateConstExpr(*whileStmt->condition, conditionValue)) {
                    if (conditionValue == 0) {
                        return;
                    }

                    const std::string headLabel = nextLabel("while_head");
                    const std::string endLabel = nextLabel("while_end");
                    loopLabels_.push_back({headLabel, endLabel});
                    emitLine(headLabel + ":");
                    emitStmt(*whileStmt->body);
                    emitLine("    j " + headLabel);
                    emitLine(endLabel + ":");
                    loopLabels_.pop_back();
                    return;
                }
            }

            const std::string headLabel = nextLabel("while_head");
            const std::string endLabel = nextLabel("while_end");
            loopLabels_.push_back({headLabel, endLabel});
            emitLine(headLabel + ":");
            emitExpr(*whileStmt->condition);
            emitLine("    beqz a0, " + endLabel);
            emitStmt(*whileStmt->body);
            emitLine("    j " + headLabel);
            emitLine(endLabel + ":");
            loopLabels_.pop_back();
            return;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt) != nullptr) {
            emitLine("    j " + loopLabels_.back().second);
            return;
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt) != nullptr) {
            emitLine("    j " + loopLabels_.back().first);
            return;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (ret->expr != nullptr) {
                emitExpr(*ret->expr);
            } else {
                emitLine("    li a0, 0");
            }
            emitLine("    j " + currentExitLabel_);
        }
    }

    void emitDecl(const Decl& decl) {
        const auto offsetIt = currentDeclOffsets_.find(&decl);
        if (offsetIt == currentDeclOffsets_.end()) {
            throw std::runtime_error("missing stack slot for declaration");
        }

        if (decl.isConst) {
            const std::int32_t value = evaluateConstExpr(*decl.init);
            scopes_.back().emplace(
                decl.name, Binding{Binding::Kind::LocalConst, offsetIt->second, value});
            if (!options_.enableOpt) {
                emitLoadImmediate("a0", value);
                emitStoreWord("a0", offsetIt->second, "s0");
            }
            return;
        }

        if (options_.enableOpt) {
            std::int32_t value = 0;
            if (tryEvaluateConstExpr(*decl.init, value)) {
                emitLoadImmediate("a0", value);
                emitStoreWord("a0", offsetIt->second, "s0");
                scopes_.back().emplace(
                    decl.name, Binding{Binding::Kind::LocalVar, offsetIt->second, 0});
                return;
            }
        }

        emitExpr(*decl.init);
        emitStoreWord("a0", offsetIt->second, "s0");
        scopes_.back().emplace(
            decl.name, Binding{Binding::Kind::LocalVar, offsetIt->second, 0});
    }

    bool tryEvaluateConstExpr(const Expr& expr, std::int32_t& value) const {
        if (const auto* literal = dynamic_cast<const IntLiteral*>(&expr)) {
            value = literal->value;
            return true;
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            const Binding* binding = lookupBinding(identifier->name);
            if (binding != nullptr &&
                (binding->kind == Binding::Kind::LocalConst ||
                 binding->kind == Binding::Kind::GlobalConst)) {
                value = binding->constValue;
                return true;
            }
            return false;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            std::int32_t operand = 0;
            if (!tryEvaluateConstExpr(*unary->operand, operand)) {
                return false;
            }
            switch (unary->op) {
                case UnaryOp::Plus:
                    value = operand;
                    return true;
                case UnaryOp::Minus:
                    value =
                        static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(operand));
                    return true;
                case UnaryOp::LogicalNot:
                    value = operand == 0 ? 1 : 0;
                    return true;
            }
        }

        const auto* binary = dynamic_cast<const BinaryExpr*>(&expr);
        if (binary == nullptr) {
            return false;
        }

        if (binary->op == BinaryOp::LogicalAnd) {
            std::int32_t lhs = 0;
            if (!tryEvaluateConstExpr(*binary->lhs, lhs)) {
                return false;
            }
            if (lhs == 0) {
                value = 0;
                return true;
            }
            std::int32_t rhs = 0;
            if (!tryEvaluateConstExpr(*binary->rhs, rhs)) {
                return false;
            }
            value = rhs != 0 ? 1 : 0;
            return true;
        }

        if (binary->op == BinaryOp::LogicalOr) {
            std::int32_t lhs = 0;
            if (!tryEvaluateConstExpr(*binary->lhs, lhs)) {
                return false;
            }
            if (lhs != 0) {
                value = 1;
                return true;
            }
            std::int32_t rhs = 0;
            if (!tryEvaluateConstExpr(*binary->rhs, rhs)) {
                return false;
            }
            value = rhs != 0 ? 1 : 0;
            return true;
        }

        std::int32_t lhs = 0;
        std::int32_t rhs = 0;
        if (!tryEvaluateConstExpr(*binary->lhs, lhs) ||
            !tryEvaluateConstExpr(*binary->rhs, rhs)) {
            return false;
        }
        switch (binary->op) {
            case BinaryOp::Add:
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                                  static_cast<std::uint32_t>(rhs));
                return true;
            case BinaryOp::Sub:
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) -
                                                  static_cast<std::uint32_t>(rhs));
                return true;
            case BinaryOp::Mul:
                value = static_cast<std::int32_t>(lhs * rhs);
                return true;
            case BinaryOp::Div:
                if (rhs == 0) {
                    return false;
                }
                value = static_cast<std::int32_t>(lhs / rhs);
                return true;
            case BinaryOp::Mod:
                if (rhs == 0) {
                    return false;
                }
                value = static_cast<std::int32_t>(lhs % rhs);
                return true;
            case BinaryOp::Less:
                value = lhs < rhs ? 1 : 0;
                return true;
            case BinaryOp::Greater:
                value = lhs > rhs ? 1 : 0;
                return true;
            case BinaryOp::LessEqual:
                value = lhs <= rhs ? 1 : 0;
                return true;
            case BinaryOp::GreaterEqual:
                value = lhs >= rhs ? 1 : 0;
                return true;
            case BinaryOp::Equal:
                value = lhs == rhs ? 1 : 0;
                return true;
            case BinaryOp::NotEqual:
                value = lhs != rhs ? 1 : 0;
                return true;
            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr:
                break;
        }
        return false;
    }

    std::int32_t evaluateConstExpr(const Expr& expr) const {
        std::int32_t value = 0;
        if (!tryEvaluateConstExpr(expr, value)) {
            throw std::runtime_error("expected constant expression");
        }
        return value;
    }

    void emitExpr(const Expr& expr) {
        if (options_.enableOpt) {
            std::int32_t value = 0;
            if (tryEvaluateConstExpr(expr, value)) {
                emitLoadImmediate("a0", value);
                return;
            }
        }

        if (const auto* literal = dynamic_cast<const IntLiteral*>(&expr)) {
            emitLoadImmediate("a0", literal->value);
            return;
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            emitLoad(identifier->name);
            return;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            emitExpr(*unary->operand);
            switch (unary->op) {
                case UnaryOp::Plus:
                    break;
                case UnaryOp::Minus:
                    emitLine("    neg a0, a0");
                    break;
                case UnaryOp::LogicalNot:
                    emitLine("    seqz a0, a0");
                    break;
            }
            return;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            if (options_.enableOpt && emitOptimizedBinary(*binary)) {
                return;
            }

            if (binary->op == BinaryOp::LogicalAnd) {
                const std::string falseLabel = nextLabel("land_false");
                const std::string endLabel = nextLabel("land_end");
                emitExpr(*binary->lhs);
                emitLine("    beqz a0, " + falseLabel);
                emitExpr(*binary->rhs);
                emitLine("    snez a0, a0");
                emitLine("    j " + endLabel);
                emitLine(falseLabel + ":");
                emitLine("    li a0, 0");
                emitLine(endLabel + ":");
                return;
            }

            if (binary->op == BinaryOp::LogicalOr) {
                const std::string trueLabel = nextLabel("lor_true");
                const std::string endLabel = nextLabel("lor_end");
                emitExpr(*binary->lhs);
                emitLine("    bnez a0, " + trueLabel);
                emitExpr(*binary->rhs);
                emitLine("    snez a0, a0");
                emitLine("    j " + endLabel);
                emitLine(trueLabel + ":");
                emitLine("    li a0, 1");
                emitLine(endLabel + ":");
                return;
            }

            emitExpr(*binary->lhs);
            pushA0();
            emitExpr(*binary->rhs);
            popToT0();
            emitBinaryRegOp(binary->op, "t0", "a0");
            return;
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            const std::size_t stackArgs =
                call->arguments.size() > 8 ? call->arguments.size() - 8 : 0;
            const int stackArgBytes = static_cast<int>(stackArgs * 4);
            const int padding = (16 - (dynamicStackDepth_ + stackArgBytes) % 16) % 16;
            allocateDynamicStack(padding);

            for (std::size_t i = call->arguments.size(); i > 0; --i) {
                emitExpr(*call->arguments[i - 1]);
                pushA0();
            }

            const std::size_t registerArgs = std::min<std::size_t>(8, call->arguments.size());
            for (std::size_t i = 0; i < registerArgs; ++i) {
                emitLoadWord("a" + std::to_string(i), 0, "sp");
                releaseDynamicStack(4);
            }

            emitLine("    call " + call->callee);

            releaseDynamicStack(stackArgBytes);
            releaseDynamicStack(padding);
            return;
        }
    }

    bool emitOptimizedBinary(const BinaryExpr& binary) {
        if (binary.op == BinaryOp::LogicalAnd) {
            return emitOptimizedLogicalAnd(binary);
        }

        if (binary.op == BinaryOp::LogicalOr) {
            return emitOptimizedLogicalOr(binary);
        }

        std::int32_t rhsConst = 0;
        if (tryEvaluateConstExpr(*binary.rhs, rhsConst) &&
            emitBinaryWithConst(binary.op, *binary.lhs, rhsConst, false)) {
            return true;
        }

        std::int32_t lhsConst = 0;
        if (tryEvaluateConstExpr(*binary.lhs, lhsConst) &&
            emitBinaryWithConst(binary.op, *binary.rhs, lhsConst, true)) {
            return true;
        }

        if (isLeafLikeExpr(*binary.rhs)) {
            emitExpr(*binary.lhs);
            emitMove("t3", "a0");
            emitExpr(*binary.rhs);
            emitBinaryRegOp(binary.op, "t3", "a0");
            return true;
        }

        if (isLeafLikeExpr(*binary.lhs)) {
            emitExpr(*binary.rhs);
            emitMove("t3", "a0");
            emitExpr(*binary.lhs);
            emitBinaryRegOp(binary.op, "a0", "t3");
            return true;
        }

        return false;
    }

    bool emitOptimizedLogicalAnd(const BinaryExpr& binary) {
        std::int32_t lhsConst = 0;
        if (tryEvaluateConstExpr(*binary.lhs, lhsConst)) {
            if (lhsConst == 0) {
                emitLoadImmediate("a0", 0);
            } else {
                emitExpr(*binary.rhs);
                emitLine("    snez a0, a0");
            }
            return true;
        }

        std::int32_t rhsConst = 0;
        if (tryEvaluateConstExpr(*binary.rhs, rhsConst)) {
            emitExpr(*binary.lhs);
            if (rhsConst == 0) {
                emitLoadImmediate("a0", 0);
            } else {
                emitLine("    snez a0, a0");
            }
            return true;
        }

        return false;
    }

    bool emitOptimizedLogicalOr(const BinaryExpr& binary) {
        std::int32_t lhsConst = 0;
        if (tryEvaluateConstExpr(*binary.lhs, lhsConst)) {
            if (lhsConst != 0) {
                emitLoadImmediate("a0", 1);
            } else {
                emitExpr(*binary.rhs);
                emitLine("    snez a0, a0");
            }
            return true;
        }

        std::int32_t rhsConst = 0;
        if (tryEvaluateConstExpr(*binary.rhs, rhsConst)) {
            emitExpr(*binary.lhs);
            if (rhsConst != 0) {
                emitLoadImmediate("a0", 1);
            } else {
                emitLine("    snez a0, a0");
            }
            return true;
        }

        return false;
    }

    bool emitBinaryWithConst(BinaryOp op, const Expr& otherExpr, std::int32_t constant,
                             bool constantOnLhs) {
        emitExpr(otherExpr);

        switch (op) {
            case BinaryOp::Add:
                if (!constantOnLhs && fitsI12(constant)) {
                    emitAddImmediate("a0", "a0", constant);
                } else {
                    emitLoadImmediate("t0", constant);
                    emitLine("    add a0, a0, t0");
                }
                return true;

            case BinaryOp::Sub:
                if (!constantOnLhs && constant != std::numeric_limits<std::int32_t>::min() &&
                    fitsI12(-constant)) {
                    emitAddImmediate("a0", "a0", -constant);
                } else {
                    emitLoadImmediate("t0", constant);
                    if (constantOnLhs) {
                        emitLine("    sub a0, t0, a0");
                    } else {
                        emitLine("    sub a0, a0, t0");
                    }
                }
                return true;

            case BinaryOp::Mul:
                if (constant == 1) {
                    return true;
                }
                if (constant == -1) {
                    emitLine("    neg a0, a0");
                    return true;
                }
                if (constant == 0) {
                    emitLoadImmediate("a0", 0);
                    return true;
                }
                emitLoadImmediate("t0", constant);
                if (constantOnLhs) {
                    emitLine("    mul a0, t0, a0");
                } else {
                    emitLine("    mul a0, a0, t0");
                }
                return true;

            case BinaryOp::Div:
                if (!constantOnLhs && constant == 1) {
                    return true;
                }
                emitLoadImmediate("t0", constant);
                if (constantOnLhs) {
                    emitLine("    div a0, t0, a0");
                } else {
                    emitLine("    div a0, a0, t0");
                }
                return true;

            case BinaryOp::Mod:
                emitLoadImmediate("t0", constant);
                if (constantOnLhs) {
                    emitLine("    rem a0, t0, a0");
                } else {
                    emitLine("    rem a0, a0, t0");
                }
                return true;

            case BinaryOp::Less:
                if (!constantOnLhs && fitsI12(constant)) {
                    emitLine("    slti a0, a0, " + std::to_string(constant));
                } else {
                    emitLoadImmediate("t0", constant);
                    if (constantOnLhs) {
                        emitLine("    slt a0, t0, a0");
                    } else {
                        emitLine("    slt a0, a0, t0");
                    }
                }
                return true;

            case BinaryOp::Greater:
                emitLoadImmediate("t0", constant);
                if (constantOnLhs) {
                    emitLine("    slt a0, a0, t0");
                } else {
                    emitLine("    slt a0, t0, a0");
                }
                return true;

            case BinaryOp::LessEqual:
                emitLoadImmediate("t0", constant);
                if (constantOnLhs) {
                    emitLine("    slt a0, a0, t0");
                } else {
                    emitLine("    slt a0, t0, a0");
                }
                emitLine("    xori a0, a0, 1");
                return true;

            case BinaryOp::GreaterEqual:
                if (!constantOnLhs && fitsI12(constant)) {
                    emitLine("    slti a0, a0, " + std::to_string(constant));
                    emitLine("    xori a0, a0, 1");
                } else {
                    emitLoadImmediate("t0", constant);
                    if (constantOnLhs) {
                        emitLine("    slt a0, t0, a0");
                    } else {
                        emitLine("    slt a0, a0, t0");
                    }
                    emitLine("    xori a0, a0, 1");
                }
                return true;

            case BinaryOp::Equal:
                if (constant == 0) {
                    emitLine("    seqz a0, a0");
                } else {
                    emitLoadImmediate("t0", constant);
                    emitLine("    sub t1, a0, t0");
                    emitLine("    seqz a0, t1");
                }
                return true;

            case BinaryOp::NotEqual:
                if (constant == 0) {
                    emitLine("    snez a0, a0");
                } else {
                    emitLoadImmediate("t0", constant);
                    emitLine("    sub t1, a0, t0");
                    emitLine("    snez a0, t1");
                }
                return true;

            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr:
                return false;
        }

        return false;
    }

    void emitBinaryRegOp(BinaryOp op, const std::string& lhsReg, const std::string& rhsReg) {
        switch (op) {
            case BinaryOp::Add:
                emitLine("    add a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Sub:
                emitLine("    sub a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Mul:
                emitLine("    mul a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Div:
                emitLine("    div a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Mod:
                emitLine("    rem a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Less:
                emitLine("    slt a0, " + lhsReg + ", " + rhsReg);
                return;
            case BinaryOp::Greater:
                emitLine("    slt a0, " + rhsReg + ", " + lhsReg);
                return;
            case BinaryOp::LessEqual:
                emitLine("    slt a0, " + rhsReg + ", " + lhsReg);
                emitLine("    xori a0, a0, 1");
                return;
            case BinaryOp::GreaterEqual:
                emitLine("    slt a0, " + lhsReg + ", " + rhsReg);
                emitLine("    xori a0, a0, 1");
                return;
            case BinaryOp::Equal:
                emitLine("    sub t1, " + lhsReg + ", " + rhsReg);
                emitLine("    seqz a0, t1");
                return;
            case BinaryOp::NotEqual:
                emitLine("    sub t1, " + lhsReg + ", " + rhsReg);
                emitLine("    snez a0, t1");
                return;
            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr:
                return;
        }
    }

    bool isLeafLikeExpr(const Expr& expr) const {
        if (dynamic_cast<const IntLiteral*>(&expr) != nullptr) {
            return true;
        }

        if (dynamic_cast<const IdentifierExpr*>(&expr) != nullptr) {
            return true;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            return isLeafLikeExpr(*unary->operand);
        }

        return false;
    }

    void emitLoad(const std::string& name) {
        const Binding* binding = lookupBinding(name);
        if (binding == nullptr) {
            emitLoadImmediate("a0", 0);
            return;
        }

        switch (binding->kind) {
            case Binding::Kind::LocalVar:
                emitLoadWord("a0", binding->offset, "s0");
                return;
            case Binding::Kind::LocalConst:
                emitLoadImmediate("a0", binding->constValue);
                return;
            case Binding::Kind::GlobalVar:
                emitLine("    la t0, " + name);
                emitLine("    lw a0, 0(t0)");
                return;
            case Binding::Kind::GlobalConst:
                emitLoadImmediate("a0", binding->constValue);
                return;
        }
    }

    void emitStore(const std::string& name) {
        const Binding* binding = lookupBinding(name);
        if (binding == nullptr) {
            return;
        }

        switch (binding->kind) {
            case Binding::Kind::LocalVar:
                emitStoreWord("a0", binding->offset, "s0");
                return;
            case Binding::Kind::GlobalVar:
                emitLine("    la t0, " + name);
                emitLine("    sw a0, 0(t0)");
                return;
            case Binding::Kind::LocalConst:
            case Binding::Kind::GlobalConst:
                return;
        }
    }

    void pushA0() {
        allocateDynamicStack(4);
        emitStoreWord("a0", 0, "sp");
    }

    void popToT0() {
        emitLoadWord("t0", 0, "sp");
        releaseDynamicStack(4);
    }

    void emitLoadImmediate(const std::string& destination, std::int32_t value) {
        emitLine("    li " + destination + ", " + std::to_string(value));
    }

    void emitMove(const std::string& destination, const std::string& source) {
        if (destination == source) {
            return;
        }
        emitLine("    mv " + destination + ", " + source);
    }

    void emitAdjustSp(int amount) {
        if (amount == 0) {
            return;
        }
        if (fitsI12(amount)) {
            emitLine("    addi sp, sp, " + std::to_string(amount));
        } else {
            emitLine("    li t2, " + std::to_string(amount));
            emitLine("    add sp, sp, t2");
        }
    }

    void emitAddImmediate(const std::string& destination, const std::string& source, int amount) {
        if (fitsI12(amount)) {
            emitLine("    addi " + destination + ", " + source + ", " +
                     std::to_string(amount));
        } else {
            emitLine("    li t2, " + std::to_string(amount));
            emitLine("    add " + destination + ", " + source + ", t2");
        }
    }

    void emitLoadWord(const std::string& destination, int offset, const std::string& base) {
        if (fitsI12(offset)) {
            emitLine("    lw " + destination + ", " + std::to_string(offset) + "(" + base + ")");
        } else {
            emitLine("    li t2, " + std::to_string(offset));
            emitLine("    add t2, " + base + ", t2");
            emitLine("    lw " + destination + ", 0(t2)");
        }
    }

    void emitStoreWord(const std::string& source, int offset, const std::string& base) {
        if (fitsI12(offset)) {
            emitLine("    sw " + source + ", " + std::to_string(offset) + "(" + base + ")");
        } else {
            emitLine("    li t2, " + std::to_string(offset));
            emitLine("    add t2, " + base + ", t2");
            emitLine("    sw " + source + ", 0(t2)");
        }
    }

    void allocateDynamicStack(int bytes) {
        if (bytes <= 0) {
            return;
        }
        emitAdjustSp(-bytes);
        dynamicStackDepth_ += bytes;
    }

    void releaseDynamicStack(int bytes) {
        if (bytes <= 0) {
            return;
        }
        emitAdjustSp(bytes);
        dynamicStackDepth_ -= bytes;
    }
};

}  // namespace

void generateRiscV(const CompUnit& program, const ProgramInfo& info,
                   const CompileOptions& options, std::ostream& output) {
    CodeGenerator generator(program, info, options, output);
    generator.run();
}

}  // namespace toyc
