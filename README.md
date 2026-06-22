# ToyC Compiler

A ToyC-to-RISC-V32 compiler implemented in C++20 with Flex and Bison.

## Build

With CMake:

```sh
cmake -S . -B build
cmake --build build
```

Or with Make:

```sh
make
```

The build requires a C++20 compiler, Flex, and Bison.

## Usage

The compiler reads ToyC source code from standard input and writes RISC-V32
assembly to standard output:

```sh
./compiler < input.tc > output.s
./compiler -opt < input.tc > output.s
```

The `-opt` flag is accepted now; optimization passes will be added after the
functional compiler pipeline is complete.

## Current milestone

Milestone 1 supports a single `int main()` function whose body returns an
integer literal with optional unary `+`, `-`, and `!` operators.
