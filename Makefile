CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
BUILD_DIR := build
GEN_DIR := $(BUILD_DIR)/generated

.PHONY: all clean

all: compiler

$(GEN_DIR):
	mkdir -p $(GEN_DIR)

$(GEN_DIR)/parser.cpp: src/parser.y | $(GEN_DIR)
	bison -Wall -Wcounterexamples -d -o $(GEN_DIR)/parser.cpp src/parser.y

$(GEN_DIR)/parser.hpp: $(GEN_DIR)/parser.cpp
	@test -f $@

$(GEN_DIR)/lexer.cpp: src/lexer.l $(GEN_DIR)/parser.hpp | $(GEN_DIR)
	flex -o $(GEN_DIR)/lexer.cpp src/lexer.l

compiler: src/main.cpp src/ast.cpp src/semantic.cpp src/codegen.cpp $(GEN_DIR)/parser.cpp $(GEN_DIR)/lexer.cpp
	$(CXX) $(CXXFLAGS) -Isrc -I$(GEN_DIR) $^ -o $@

clean:
	rm -rf $(BUILD_DIR) compiler
