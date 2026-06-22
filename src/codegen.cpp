#include "codegen.hpp"

#include <algorithm>
#include <cstdint>
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
            emitExpr(*assign->value);
            emitStore(assign->name);
            return;
        }

        if (const auto* declStmt = dynamic_cast<const DeclStmt*>(&stmt)) {
            emitDecl(*declStmt->decl);
            return;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
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
            const std::string headLabel = nextLabel("while_head");
            const std::string bodyLabel = nextLabel("while_body");
            const std::string endLabel = nextLabel("while_end");
            loopLabels_.push_back({headLabel, endLabel});
            emitLine(headLabel + ":");
            emitExpr(*whileStmt->condition);
            emitLine("    beqz a0, " + endLabel);
            emitLine(bodyLabel + ":");
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
                emitLine("    li a0, " + std::to_string(value));
                emitStoreWord("a0", offsetIt->second, "s0");
            }
            return;
        }

        emitExpr(*decl.init);
        emitStoreWord("a0", offsetIt->second, "s0");
        scopes_.back().emplace(
            decl.name, Binding{Binding::Kind::LocalVar, offsetIt->second, 0});
    }

    std::int32_t evaluateConstExpr(const Expr& expr) const {
        if (const auto* literal = dynamic_cast<const IntLiteral*>(&expr)) {
            return literal->value;
        }

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            const Binding* binding = lookupBinding(identifier->name);
            if (binding != nullptr &&
                (binding->kind == Binding::Kind::LocalConst ||
                 binding->kind == Binding::Kind::GlobalConst)) {
                return binding->constValue;
            }
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            const auto operand = evaluateConstExpr(*unary->operand);
            switch (unary->op) {
                case UnaryOp::Plus:
                    return operand;
                case UnaryOp::Minus:
                    return static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(operand));
                case UnaryOp::LogicalNot:
                    return operand == 0 ? 1 : 0;
            }
        }

        const auto* binary = dynamic_cast<const BinaryExpr*>(&expr);
        if (binary == nullptr) {
            return 0;
        }

        if (binary->op == BinaryOp::LogicalAnd) {
            const auto lhs = evaluateConstExpr(*binary->lhs);
            if (lhs == 0) {
                return 0;
            }
            return evaluateConstExpr(*binary->rhs) != 0 ? 1 : 0;
        }

        if (binary->op == BinaryOp::LogicalOr) {
            const auto lhs = evaluateConstExpr(*binary->lhs);
            if (lhs != 0) {
                return 1;
            }
            return evaluateConstExpr(*binary->rhs) != 0 ? 1 : 0;
        }

        const auto lhs = evaluateConstExpr(*binary->lhs);
        const auto rhs = evaluateConstExpr(*binary->rhs);
        switch (binary->op) {
            case BinaryOp::Add:
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                                 static_cast<std::uint32_t>(rhs));
            case BinaryOp::Sub:
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) -
                                                 static_cast<std::uint32_t>(rhs));
            case BinaryOp::Mul:
                return static_cast<std::int32_t>(lhs * rhs);
            case BinaryOp::Div:
                return static_cast<std::int32_t>(lhs / rhs);
            case BinaryOp::Mod:
                return static_cast<std::int32_t>(lhs % rhs);
            case BinaryOp::Less:
                return lhs < rhs ? 1 : 0;
            case BinaryOp::Greater:
                return lhs > rhs ? 1 : 0;
            case BinaryOp::LessEqual:
                return lhs <= rhs ? 1 : 0;
            case BinaryOp::GreaterEqual:
                return lhs >= rhs ? 1 : 0;
            case BinaryOp::Equal:
                return lhs == rhs ? 1 : 0;
            case BinaryOp::NotEqual:
                return lhs != rhs ? 1 : 0;
            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr:
                break;
        }
        return 0;
    }

    void emitExpr(const Expr& expr) {
        if (const auto* literal = dynamic_cast<const IntLiteral*>(&expr)) {
            emitLine("    li a0, " + std::to_string(literal->value));
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

            switch (binary->op) {
                case BinaryOp::Add:
                    emitLine("    add a0, t0, a0");
                    return;
                case BinaryOp::Sub:
                    emitLine("    sub a0, t0, a0");
                    return;
                case BinaryOp::Mul:
                    emitLine("    mul a0, t0, a0");
                    return;
                case BinaryOp::Div:
                    emitLine("    div a0, t0, a0");
                    return;
                case BinaryOp::Mod:
                    emitLine("    rem a0, t0, a0");
                    return;
                case BinaryOp::Less:
                    emitLine("    slt a0, t0, a0");
                    return;
                case BinaryOp::Greater:
                    emitLine("    slt a0, a0, t0");
                    return;
                case BinaryOp::LessEqual:
                    emitLine("    slt a0, a0, t0");
                    emitLine("    xori a0, a0, 1");
                    return;
                case BinaryOp::GreaterEqual:
                    emitLine("    slt a0, t0, a0");
                    emitLine("    xori a0, a0, 1");
                    return;
                case BinaryOp::Equal:
                    emitLine("    sub t1, t0, a0");
                    emitLine("    seqz a0, t1");
                    return;
                case BinaryOp::NotEqual:
                    emitLine("    sub t1, t0, a0");
                    emitLine("    snez a0, t1");
                    return;
                case BinaryOp::LogicalAnd:
                case BinaryOp::LogicalOr:
                    break;
            }
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

    void emitLoad(const std::string& name) {
        const Binding* binding = lookupBinding(name);
        if (binding == nullptr) {
            emitLine("    li a0, 0");
            return;
        }

        switch (binding->kind) {
            case Binding::Kind::LocalVar:
                emitLoadWord("a0", binding->offset, "s0");
                return;
            case Binding::Kind::LocalConst:
                emitLine("    li a0, " + std::to_string(binding->constValue));
                return;
            case Binding::Kind::GlobalVar:
                emitLine("    la t0, " + name);
                emitLine("    lw a0, 0(t0)");
                return;
            case Binding::Kind::GlobalConst:
                emitLine("    li a0, " + std::to_string(binding->constValue));
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
