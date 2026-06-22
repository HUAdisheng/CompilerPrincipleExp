#pragma once

#include "ast.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {

struct CompileOptions {
    bool enableOpt = false;
};

struct Diagnostic {
    SourceLocation location{};
    std::string message;
};

struct FunctionSignature {
    ValueType returnType = ValueType::Void;
    std::size_t paramCount = 0;
};

struct ProgramInfo {
    std::unordered_map<std::string, std::int32_t> globalConsts;
    std::unordered_map<std::string, std::int32_t> globalVars;
    std::unordered_map<std::string, FunctionSignature> functions;
    std::vector<const FuncDef*> functionOrder;
    std::vector<std::string> globalVarOrder;
};

struct SemanticResult {
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
    ProgramInfo program;
};

SemanticResult analyzeProgram(const CompUnit& program, const CompileOptions& options);

}  // namespace toyc
