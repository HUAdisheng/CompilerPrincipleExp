#pragma once

#include "ast.hpp"
#include "semantic.hpp"

#include <iosfwd>

namespace toyc {

void generateRiscV(const CompUnit& program, const ProgramInfo& info,
                   const CompileOptions& options, std::ostream& output);

}  // namespace toyc
