#include "ast.hpp"
#include "codegen.hpp"

#include <iostream>
#include <string_view>

int yyparse();

int main(int argc, char* argv[]) {
    bool optimize = false;
    if (argc == 2 && std::string_view(argv[1]) == "-opt") {
        optimize = true;
    } else if (argc != 1) {
        std::cerr << "usage: compiler [-opt]\n";
        return 2;
    }

    (void)optimize;

    if (yyparse() != 0 || !toyc::parsedProgram) {
        return 1;
    }

    if (toyc::parsedProgram->functionName() != "main") {
        std::cerr << "semantic error: the program entry function must be main\n";
        return 1;
    }

    toyc::generateRiscV(*toyc::parsedProgram, std::cout);
    return 0;
}
