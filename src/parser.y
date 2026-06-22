%{
#include "ast.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int yylex();
void yyerror(const char* message);
extern int yylineno;
%}

%code requires {
#include <cstdint>
#include <string>
#include <vector>

namespace toyc {
class BlockStmt;
class Decl;
class Expr;
class Stmt;
class TopLevelItem;
}
}

%define parse.error detailed
%expect 0

%union {
    std::int64_t integer;
    std::string* text;
    toyc::Expr* expr;
    toyc::Stmt* stmt;
    toyc::BlockStmt* block;
    toyc::Decl* decl;
    toyc::TopLevelItem* item;
    std::vector<toyc::Expr*>* expr_list;
    std::vector<toyc::Stmt*>* stmt_list;
    std::vector<toyc::TopLevelItem*>* item_list;
    std::vector<std::string>* string_list;
}

%token CONST INT VOID IF ELSE WHILE BREAK CONTINUE RETURN
%token OR AND EQ NE LE GE
%token <integer> NUMBER
%token <text> IDENTIFIER

%type <expr> expression logical_or_expression logical_and_expression relational_expression
%type <expr> additive_expression multiplicative_expression unary_expression primary_expression
%type <stmt> statement
%type <block> block
%type <decl> declaration const_declaration var_declaration
%type <item> top_level function_definition global_declaration
%type <item_list> top_level_list
%type <stmt_list> statement_list
%type <expr_list> argument_list argument_list_opt
%type <string_list> parameter_list parameter_list_opt

%destructor { delete $$; } <text>
%destructor { delete $$; } <expr>
%destructor { delete $$; } <stmt>
%destructor { delete $$; } <block>
%destructor { delete $$; } <decl>
%destructor { delete $$; } <item>
%destructor {
    if ($$ != nullptr) {
        for (auto* value : *$$) {
            delete value;
        }
        delete $$;
    }
} <expr_list>
%destructor {
    if ($$ != nullptr) {
        for (auto* value : *$$) {
            delete value;
        }
        delete $$;
    }
} <stmt_list>
%destructor {
    if ($$ != nullptr) {
        for (auto* value : *$$) {
            delete value;
        }
        delete $$;
    }
} <item_list>
%destructor {
    if ($$ != nullptr) {
        delete $$;
    }
} <string_list>

%start compilation_unit

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

compilation_unit
    : top_level_list
      {
          toyc::parsedProgram = std::make_unique<toyc::CompUnit>($1);
      }
    ;

top_level_list
    : top_level
      {
          $$ = new std::vector<toyc::TopLevelItem*>();
          $$->push_back($1);
      }
    | top_level_list top_level
      {
          $1->push_back($2);
          $$ = $1;
      }
    ;

top_level
    : global_declaration
      {
          $$ = $1;
      }
    | function_definition
      {
          $$ = $1;
      }
    | INT IDENTIFIER '=' expression ';'
      {
          std::string name = std::move(*$2);
          delete $2;
          $$ = new toyc::GlobalDecl(new toyc::Decl(false, std::move(name), $4));
      }
    | INT IDENTIFIER '(' parameter_list_opt ')' block
      {
          std::string name = std::move(*$2);
          delete $2;
          $$ = new toyc::FuncDef(toyc::ValueType::Int, std::move(name), $4, $6);
      }
    ;

global_declaration
    : const_declaration
      {
          $$ = new toyc::GlobalDecl($1);
      }
    ;

function_definition
    : VOID IDENTIFIER '(' parameter_list_opt ')' block
      {
          std::string name = std::move(*$2);
          delete $2;
          $$ = new toyc::FuncDef(toyc::ValueType::Void, std::move(name), $4, $6);
      }
    ;

parameter_list_opt
    : %empty
      {
          $$ = new std::vector<std::string>();
      }
    | parameter_list
      {
          $$ = $1;
      }
    ;

parameter_list
    : INT IDENTIFIER
      {
          $$ = new std::vector<std::string>();
          $$->push_back(std::move(*$2));
          delete $2;
      }
    | parameter_list ',' INT IDENTIFIER
      {
          $1->push_back(std::move(*$4));
          delete $4;
          $$ = $1;
      }
    ;

declaration
    : const_declaration
      {
          $$ = $1;
      }
    | var_declaration
      {
          $$ = $1;
      }
    ;

const_declaration
    : CONST INT IDENTIFIER '=' expression ';'
      {
          std::string name = std::move(*$3);
          delete $3;
          $$ = new toyc::Decl(true, std::move(name), $5);
      }
    ;

var_declaration
    : INT IDENTIFIER '=' expression ';'
      {
          std::string name = std::move(*$2);
          delete $2;
          $$ = new toyc::Decl(false, std::move(name), $4);
      }
    ;

block
    : '{' statement_list '}'
      {
          $$ = new toyc::BlockStmt($2);
      }
    ;

statement_list
    : %empty
      {
          $$ = new std::vector<toyc::Stmt*>();
      }
    | statement_list statement
      {
          $1->push_back($2);
          $$ = $1;
      }
    ;

statement
    : block
      {
          $$ = $1;
      }
    | ';'
      {
          $$ = new toyc::EmptyStmt();
      }
    | expression ';'
      {
          $$ = new toyc::ExprStmt($1);
      }
    | IDENTIFIER '=' expression ';'
      {
          std::string name = std::move(*$1);
          delete $1;
          $$ = new toyc::AssignStmt(std::move(name), $3);
      }
    | declaration
      {
          $$ = new toyc::DeclStmt($1);
      }
    | IF '(' expression ')' statement %prec LOWER_THAN_ELSE
      {
          $$ = new toyc::IfStmt($3, $5, nullptr);
      }
    | IF '(' expression ')' statement ELSE statement
      {
          $$ = new toyc::IfStmt($3, $5, $7);
      }
    | WHILE '(' expression ')' statement
      {
          $$ = new toyc::WhileStmt($3, $5);
      }
    | BREAK ';'
      {
          $$ = new toyc::BreakStmt();
      }
    | CONTINUE ';'
      {
          $$ = new toyc::ContinueStmt();
      }
    | RETURN ';'
      {
          $$ = new toyc::ReturnStmt(nullptr);
      }
    | RETURN expression ';'
      {
          $$ = new toyc::ReturnStmt($2);
      }
    ;

expression
    : logical_or_expression
      {
          $$ = $1;
      }
    ;

logical_or_expression
    : logical_and_expression
      {
          $$ = $1;
      }
    | logical_or_expression OR logical_and_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::LogicalOr, $1, $3);
      }
    ;

logical_and_expression
    : relational_expression
      {
          $$ = $1;
      }
    | logical_and_expression AND relational_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::LogicalAnd, $1, $3);
      }
    ;

relational_expression
    : additive_expression
      {
          $$ = $1;
      }
    | relational_expression '<' additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Less, $1, $3);
      }
    | relational_expression '>' additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Greater, $1, $3);
      }
    | relational_expression LE additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::LessEqual, $1, $3);
      }
    | relational_expression GE additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::GreaterEqual, $1, $3);
      }
    | relational_expression EQ additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Equal, $1, $3);
      }
    | relational_expression NE additive_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::NotEqual, $1, $3);
      }
    ;

additive_expression
    : multiplicative_expression
      {
          $$ = $1;
      }
    | additive_expression '+' multiplicative_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Add, $1, $3);
      }
    | additive_expression '-' multiplicative_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Sub, $1, $3);
      }
    ;

multiplicative_expression
    : unary_expression
      {
          $$ = $1;
      }
    | multiplicative_expression '*' unary_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Mul, $1, $3);
      }
    | multiplicative_expression '/' unary_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Div, $1, $3);
      }
    | multiplicative_expression '%' unary_expression
      {
          $$ = new toyc::BinaryExpr(toyc::BinaryOp::Mod, $1, $3);
      }
    ;

unary_expression
    : primary_expression
      {
          $$ = $1;
      }
    | '+' unary_expression
      {
          $$ = new toyc::UnaryExpr(toyc::UnaryOp::Plus, $2);
      }
    | '-' unary_expression
      {
          $$ = new toyc::UnaryExpr(toyc::UnaryOp::Minus, $2);
      }
    | '!' unary_expression
      {
          $$ = new toyc::UnaryExpr(toyc::UnaryOp::LogicalNot, $2);
      }
    ;

primary_expression
    : IDENTIFIER
      {
          std::string name = std::move(*$1);
          delete $1;
          $$ = new toyc::IdentifierExpr(std::move(name));
      }
    | NUMBER
      {
          const auto value = static_cast<std::uint32_t>($1);
          $$ = new toyc::IntLiteral(static_cast<std::int32_t>(value));
      }
    | '(' expression ')'
      {
          $$ = $2;
      }
    | IDENTIFIER '(' argument_list_opt ')'
      {
          std::string name = std::move(*$1);
          delete $1;
          $$ = new toyc::CallExpr(std::move(name), $3);
      }
    ;

argument_list_opt
    : %empty
      {
          $$ = new std::vector<toyc::Expr*>();
      }
    | argument_list
      {
          $$ = $1;
      }
    ;

argument_list
    : expression
      {
          $$ = new std::vector<toyc::Expr*>();
          $$->push_back($1);
      }
    | argument_list ',' expression
      {
          $1->push_back($3);
          $$ = $1;
      }
    ;

%%

void yyerror(const char* message) {
    std::cerr << "syntax error at line " << yylineno << ": " << message << '\n';
}
