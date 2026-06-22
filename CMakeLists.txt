cmake_minimum_required(VERSION 3.20)

project(toyc_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

BISON_TARGET(
    ToyCParser
    "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y"
    "${GENERATED_DIR}/parser.cpp"
    DEFINES_FILE "${GENERATED_DIR}/parser.hpp"
)

FLEX_TARGET(
    ToyCLexer
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l"
    "${GENERATED_DIR}/lexer.cpp"
)

ADD_FLEX_BISON_DEPENDENCY(ToyCLexer ToyCParser)

add_executable(
    compiler
    src/main.cpp
    src/ast.cpp
    src/codegen.cpp
    ${BISON_ToyCParser_OUTPUTS}
    ${FLEX_ToyCLexer_OUTPUTS}
)

target_include_directories(
    compiler
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(compiler PRIVATE /W4 /permissive-)
else()
    target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
