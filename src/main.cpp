#include "ast.hpp"
#include "codegen.hpp"
#include "semantic.hpp"

#include <iostream>
#include <string_view>

int yyparse();

int main(int argc, char* argv[]) {
    toyc::CompileOptions options;
    if (argc == 2 && std::string_view(argv[1]) == "-opt") {
        options.enableOpt = true;
    } else if (argc != 1) {
        std::cerr << "usage: compiler [-opt]\n";
        return 2;
    }

    if (yyparse() != 0 || !toyc::parsedProgram) {
        return 1;
    }

    const auto semantic = toyc::analyzeProgram(*toyc::parsedProgram, options);
    if (!semantic.ok) {
        for (const auto& diagnostic : semantic.diagnostics) {
            std::cerr << "semantic error";
            if (diagnostic.location.line > 0) {
                std::cerr << " at line " << diagnostic.location.line;
            }
            std::cerr << ": " << diagnostic.message << '\n';
        }
        return 1;
    }

    toyc::generateRiscV(*toyc::parsedProgram, semantic.program, options, std::cout);
    return 0;
}
