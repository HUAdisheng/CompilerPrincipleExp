%{
#include "ast.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

int yylex();
void yyerror(const char* message);
%}

%code requires {
#include <cstdint>
#include <string>

namespace toyc {
class Expr;
}
}

%define parse.error detailed

%union {
    std::int64_t integer;
    std::string* text;
    toyc::Expr* expr;
}

%token INT RETURN
%token <integer> NUMBER
%token <text> IDENTIFIER

%type <expr> expression unary_expression primary_expression

%destructor { delete $$; } <text>
%destructor { delete $$; } <expr>

%start compilation_unit

%%

compilation_unit
    : INT IDENTIFIER '(' ')' '{' RETURN expression ';' '}'
      {
          std::string name = std::move(*$2);
          delete $2;
          toyc::parsedProgram = std::make_unique<toyc::Program>(
              std::move(name), std::unique_ptr<toyc::Expr>($7));
      }
    ;

expression
    : unary_expression
      {
          $$ = $1;
      }
    ;

unary_expression
    : primary_expression
      {
          $$ = $1;
      }
    | '+' unary_expression
      {
          $$ = new toyc::UnaryExpr(
              toyc::UnaryOp::Plus, std::unique_ptr<toyc::Expr>($2));
      }
    | '-' unary_expression
      {
          $$ = new toyc::UnaryExpr(
              toyc::UnaryOp::Minus, std::unique_ptr<toyc::Expr>($2));
      }
    | '!' unary_expression
      {
          $$ = new toyc::UnaryExpr(
              toyc::UnaryOp::LogicalNot, std::unique_ptr<toyc::Expr>($2));
      }
    ;

primary_expression
    : NUMBER
      {
          const auto value = static_cast<std::uint32_t>($1);
          $$ = new toyc::IntLiteral(static_cast<std::int32_t>(value));
      }
    | '(' expression ')'
      {
          $$ = $2;
      }
    ;

%%

void yyerror(const char* message) {
    std::cerr << "syntax error: " << message << '\n';
}
