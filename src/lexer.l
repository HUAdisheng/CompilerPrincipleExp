%option noyywrap nounput noinput

%{
#include "parser.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
%}

WHITESPACE  [ \t\r\n\f\v]+
IDENTIFIER  [_A-Za-z][_A-Za-z0-9]*
NUMBER      0|[1-9][0-9]*

%%

{WHITESPACE}            ;
"//"[^\r\n]*            ;
"/*"([^*]|\*+[^*/])*\*+"/" ;

"int"                   return INT;
"return"                return RETURN;

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
"+"                     return '+';
"-"                     return '-';
"!"                     return '!';

.                       {
                            std::cerr << "unexpected character: " << yytext << '\n';
                            return YYUNDEF;
                        }

%%
