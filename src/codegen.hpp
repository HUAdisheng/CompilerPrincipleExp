#pragma once

#include <iosfwd>

namespace toyc {

class Program;

void generateRiscV(const Program& program, std::ostream& output);

}  // namespace toyc
