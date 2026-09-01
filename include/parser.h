#ifndef OMNIKARAI_PARSER_H
#define OMNIKARAI_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "omni_diag.h"

// Forward declare Parser for use in function pointer types
typedef struct Parser Parser;

// --- Pratt Parser Function Types ---
typedef AST_Expression* (*prefix_parse_fn)(Parser* p);
typedef AST_Expression* (*infix_parse_fn)(Parser* p, AST_Expression* left);

// Parser structure holds the state of our parser
struct Parser {
    Lexer* lexer; // Pointer to the lexer instance
    Token currentToken;
    Token peekToken;

    // For error handling
    char** errors;
    int error_count;

    // Structured diagnostics (V01.00 — docs/DIAGNOSTICS.md)
    OmniDiagList diags;
    const char*  diag_file;   // file shown in diagnostics (set by CLI)

    // Indent depth tracker: incremented on TOKEN_INDENT, decremented on TOKEN_DEDENT.
    // parse_block_statement uses this to know when its own DEDENT was consumed.
    int indent_depth;

    // Pratt parser function tables
    prefix_parse_fn prefix_parse_fns[256]; // Assuming max 256 token types
    infix_parse_fn infix_parse_fns[256];
};

// --- Parser Public API ---
Parser* new_parser(Lexer* l);
void free_parser(Parser* p); // Good practice to have a way to free memory
AST_Program* parse_program(Parser* p);

#endif //OMNIKARAI_PARSER_H

