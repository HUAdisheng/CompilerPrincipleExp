%option noyywrap nounput noinput yylineno

%{
#include "parser.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
%}

WHITESPACE  [ \t\r\f\v\n]+
IDENTIFIER  [_A-Za-z][_A-Za-z0-9]*
NUMBER      0|[1-9][0-9]*

%%

{WHITESPACE}            ;
"//"[^\r\n]*            ;
"/*"([^*]|\*+[^*/])*\*+"/" ;

"const"                 return CONST;
"int"                   return INT;
"void"                  return VOID;
"if"                    return IF;
"else"                  return ELSE;
"while"                 return WHILE;
"break"                 return BREAK;
"continue"              return CONTINUE;
"return"                return RETURN;

"||"                    return OR;
"&&"                    return AND;
"=="                    return EQ;
"!="                    return NE;
"<="                    return LE;
">="                    return GE;

{NUMBER}                {
                            try {
                                yylval.integer = std::stoll(yytext);
                            } catch (const std::exception&) {
                                std::cerr << "integer literal is out of range: "
                                          << yytext << '\n';
                                return YYUNDEF;
                            }
                            return NUMBER;
                        }

{IDENTIFIER}            {
                            yylval.text = new std::string(yytext);
                            return IDENTIFIER;
                        }

"("                     return '(';
")"                     return ')';
"{"                     return '{';
"}"                     return '}';
";"                     return ';';
","                     return ',';
"="                     return '=';
"+"                     return '+';
"-"                     return '-';
"*"                     return '*';
"/"                     return '/';
"%"                     return '%';
"!"                     return '!';
"<"                     return '<';
">"                     return '>';

.                       {
                            std::cerr << "unexpected character: " << yytext << '\n';
                            return YYUNDEF;
                        }

%%
