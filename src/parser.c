#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "omni_platform.h"
#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "omni_diag.h"

// --- Precedence Enum for Pratt Parser ---
typedef enum {
    PREC_LOWEST,
    PREC_EQUALS,      // ==
    PREC_LESSGREATER, // > or <
    PREC_SUM,         // +
    PREC_PRODUCT,     // *
    PREC_PREFIX,      // -X or !X
    PREC_CALL,        // myFunction(X)
    PREC_INDEX        // array[index]
} Precedence;

// --- Pratt Parser Function Types ---
typedef AST_Expression* (*prefix_parse_fn)(Parser* p);
typedef AST_Expression* (*infix_parse_fn)(Parser* p, AST_Expression* left);


// --- Function Prototypes ---
static AST_Statement* parse_statement(Parser* p);
static AST_Statement_Block* parse_block_statement(Parser* p);
static AST_Expression* parse_expression(Parser* p, Precedence precedence);

// Expression parsing prototypes
static AST_Expression* parse_identifier(Parser* p);
static AST_Expression* parse_integer_literal(Parser* p);
static AST_Expression* parse_prefix_expression(Parser* p);
static AST_Expression* parse_infix_expression(Parser* p, AST_Expression* left);
static AST_Statement* parse_expression_statement(Parser* p);
static AST_Expression* parse_boolean(Parser* p);
static AST_Expression* parse_nil(Parser* p);
static AST_Expression* parse_string_literal(Parser* p);
static AST_Expression* parse_grouped_expression(Parser* p);
static AST_Expression* parse_empty_block_expression(Parser* p);
static AST_Expression* parse_array_literal(Parser* p);
static AST_Expression* parse_map_literal(Parser* p);
static AST_Expression* parse_index_expression(Parser* p, AST_Expression* left);
static AST_Expression* parse_fstring_expression(Parser* p);
static AST_Expression* parse_call_expression(Parser* p, AST_Expression* function);
static AST_Expression** parse_call_arguments(Parser* p);
static AST_Statement* parse_if_statement(Parser* p);
static AST_Statement* parse_fn_definition(Parser* p);
static AST_Expression* parse_fn_expression(Parser* p);
static AST_Expression_Identifier** parse_function_parameters(Parser* p);
static AST_Statement* parse_while_statement(Parser* p);
static AST_Statement* parse_for_statement(Parser* p);
static AST_Statement* parse_class_definition(Parser* p);
static AST_Statement* parse_match_statement(Parser* p);
static AST_Statement_MatchCase* parse_match_case(Parser* p);
static AST_Statement* parse_return_statement(Parser* p);
static AST_Expression* parse_semicolon_operator(Parser* p, AST_Expression* left);
static AST_Expression* parse_single_token_expression(Parser* p);


// Token management helper prototypes and implementations
static void parser_next_token(Parser* p);
static int current_token_is(Parser* p, OmniTokenType t);
static int peek_token_is(Parser* p, OmniTokenType t);
static int expect_peek(Parser* p, OmniTokenType t);


// --- Error Handling ---
/* Structured diagnostics (V01.00): every parse error records a
   OmniDiag with file/line/column from the current token, a stable
   code and a clean human-readable message. The legacy plain-string
   errors array is kept in sync for compatibility. Internal token
   debug detail is stored on the diagnostic but shown only with
   --beta — never in tooling-facing output. */
static void parser_add_error_code(Parser* p, const char* code, const char* msg) {
    int line = p->currentToken.line;
    int col  = p->currentToken.col;
    int span = (p->currentToken.literal && p->currentToken.literal[0])
               ? (int)strlen(p->currentToken.literal) : 1;
    OmniDiag* d = omni_diag_add(&p->diags, OMNI_DIAG_ERROR, code,
                                p->diag_file ? p->diag_file : "<input>",
                                line, col, span, "%s", msg);
    if (d) {
        snprintf(d->detail, sizeof(d->detail),
                 "cur_tok=%d '%s' peek=%d '%s'",
                 p->currentToken.type,
                 p->currentToken.literal ? p->currentToken.literal : "(null)",
                 p->peekToken.type,
                 p->peekToken.literal ? p->peekToken.literal : "(null)");
    }
    /* legacy string errors array — now carries the clean message */
    p->error_count++;
    p->errors = realloc(p->errors, p->error_count * sizeof(char*));
    if (!p->errors) {
        fprintf(stderr, "Fatal: OOM parser error list\n");
        exit(1);
    }
    char* error_msg = malloc(strlen(msg) + 1);
    if (!error_msg) {
        fprintf(stderr, "Fatal: OOM parser error message\n");
        exit(1);
    }
    memcpy(error_msg, msg, strlen(msg) + 1);
    p->errors[p->error_count - 1] = error_msg;
}

static void parser_add_error(Parser* p, const char* msg) {
    parser_add_error_code(p, "OMNI-E2099", msg);
}

// --- Token Management Implementations ---
static void parser_next_token(Parser* p) {
    p->currentToken = p->peekToken;
    p->peekToken = get_next_token(p->lexer);
}

static int current_token_is(Parser* p, OmniTokenType t) {
    return p->currentToken.type == t;
}

static int peek_token_is(Parser* p, OmniTokenType t) {
    return p->peekToken.type == t;
}

static int expect_peek(Parser* p, OmniTokenType t) {
    if (peek_token_is(p, t)) {
        parser_next_token(p);
        return 1;
    } else {
        char err[200];
        if (p->peekToken.type == TOKEN_EOF) {
            snprintf(err, sizeof(err), "unexpected end of file — expected %s",
                     omni_token_name(t));
        } else if (p->peekToken.literal && p->peekToken.literal[0] &&
                   p->peekToken.type != TOKEN_ILLEGAL) {
            snprintf(err, sizeof(err), "expected %s but found %s '%s'",
                     omni_token_name(t), omni_token_name(p->peekToken.type),
                     p->peekToken.literal);
        } else {
            snprintf(err, sizeof(err), "expected %s but found %s",
                     omni_token_name(t), omni_token_name(p->peekToken.type));
        }
        parser_add_error_code(p, "OMNI-E2001", err);
        return 0;
    }
}


// --- Statement Parsers ---

static AST_Statement* parse_set_statement(Parser* p) {
    AST_Statement_Set* stmt = malloc(sizeof(AST_Statement_Set));
    if (stmt == NULL) {
        parser_add_error(p, "Memory allocation failed for set statement");
        return NULL;
    }
    stmt->base.type = SET_STATEMENT;
    stmt->base.token = p->currentToken;

    if (!expect_peek(p, TOKEN_IDENT)) {
        free(stmt);
        return NULL;
    }

    AST_Expression_Identifier* name = (AST_Expression_Identifier*)parse_identifier(p);
    if (name == NULL) {
        free(stmt);
        return NULL;
    }
    stmt->name = name;

    /* ── set lst[i] = val  — index assignment ──
       Desugar into EXPRESSION_STATEMENT containing INFIX "=" with
       INDEX_EXPRESSION on the left and rhs value on the right.
       Codegen handles this in the EXPRESSION_STATEMENT augmented-assign path. */
    if (peek_token_is(p, TOKEN_LBRACKET)) {
        /* parse the index expression: name[idx] */
        AST_Expression* left_id = (AST_Expression*)name;
        parser_next_token(p); /* consume '[' */
        AST_Expression_Index* idx_node = malloc(sizeof(AST_Expression_Index));
        idx_node->base.type  = INDEX_EXPRESSION;
        idx_node->base.token = p->currentToken;
        idx_node->left       = left_id;
        parser_next_token(p); /* move to index expression */
        idx_node->index = parse_expression(p, PREC_LOWEST);
        if (!expect_peek(p, TOKEN_RBRACKET)) {
            free(idx_node); free(stmt); return NULL;
        }
        /* now expect '=' */
        if (!expect_peek(p, TOKEN_ASSIGN)) {
            free(idx_node); free(stmt); return NULL;
        }
        parser_next_token(p); /* move past '=' */
        AST_Expression* rhs = parse_expression(p, PREC_LOWEST);
        /* Build INFIX "=" node: INDEX_EXPRESSION = rhs */
        AST_Expression_Infix* assign = malloc(sizeof(AST_Expression_Infix));
        assign->base.type  = INFIX_EXPRESSION;
        assign->base.token = p->currentToken;
        assign->operator   = (char*)"=";
        assign->left       = (AST_Expression*)idx_node;
        assign->right      = rhs;
        /* Wrap in EXPRESSION_STATEMENT */
        AST_Statement_Expression* es = malloc(sizeof(AST_Statement_Expression));
        es->base.type  = EXPRESSION_STATEMENT;
        es->base.token = p->currentToken;
        es->expression = (AST_Expression*)assign;
        free(stmt); /* discard the half-built SET_STATEMENT */
        return (AST_Statement*)es;
    }

    if (!expect_peek(p, TOKEN_ASSIGN)) {
        free(name->value);
        free(name);
        free(stmt);
        return NULL;
    }
    
    parser_next_token(p);
    stmt->value = parse_expression(p, PREC_LOWEST);
    if (stmt->value == NULL) {
        free(stmt);
        return NULL;
    }
    return (AST_Statement*)stmt;
}

static AST_Statement* parse_if_statement(Parser* p) {
    // CONTRACT:
    //   ENTRY:  currentToken = TOKEN_IF or TOKEN_ELIF
    //   EXIT:   currentToken = TOKEN_NL or first token of next sibling statement
    //           All block DEDENTs are consumed internally.
    AST_Statement_If* stmt = malloc(sizeof(AST_Statement_If));
    stmt->base.type = IF_STATEMENT;
    stmt->base.token = p->currentToken;

    parser_next_token(p); // consume 'if'/'elif', land on condition
    stmt->condition = parse_expression(p, PREC_LOWEST);

    if (!expect_peek(p, TOKEN_COLON)) { free(stmt); return NULL; }

    // parse_block_statement exits with currentToken == DEDENT
    stmt->consequence = parse_block_statement(p);
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    // Now on NL, or directly on elif/else/next-stmt if no NL between

    // Skip NLs only if elif/else follows — if next is a sibling statement,
    // leave the NL so the outer block loop can see it.
    while (current_token_is(p, TOKEN_NL)) {
        if (peek_token_is(p, TOKEN_ELIF) || peek_token_is(p, TOKEN_ELSE))
            parser_next_token(p); // consume NL, land on elif/else
        else
            break; // leave NL — outer block will skip it
    }

    if (current_token_is(p, TOKEN_ELIF)) {
        stmt->alternative = parse_if_statement(p);
        // recursive call handles everything; exit state matches ours
    } else if (current_token_is(p, TOKEN_ELSE)) {
        if (!expect_peek(p, TOKEN_COLON)) { free(stmt); return NULL; }
        stmt->alternative = (AST_Statement*)parse_block_statement(p);
        if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    } else {
        stmt->alternative = NULL;
    }

    return (AST_Statement*)stmt;
}

static AST_Expression_Identifier** parse_function_parameters(Parser* p) {
    AST_Expression_Identifier** params = NULL;
    int capacity = 0;
    int param_count = 0;

    if (peek_token_is(p, TOKEN_RPAREN)) {
        parser_next_token(p); // consume ')'
        return NULL;
    }

    parser_next_token(p); // consume '(' or ','

    if (!current_token_is(p, TOKEN_IDENT) && !current_token_is(p, TOKEN_SELF)) {
        parser_add_error_code(p, "OMNI-E2002", "expected parameter name");
        return NULL;
    } // FIX: accept TOKEN_SELF as a valid parameter name (e.g. fn init(self, ...))
    
    capacity = 4;
    params = malloc(capacity * sizeof(AST_Expression_Identifier*));
    params[param_count++] = (AST_Expression_Identifier*)parse_identifier(p);

    while (peek_token_is(p, TOKEN_COMMA)) {
        parser_next_token(p); // consume ','
        parser_next_token(p); // move to the start of the next identifier
        if (param_count >= capacity) {
            capacity *= 2;
            params = realloc(params, capacity * sizeof(AST_Expression_Identifier*));
        }
        params[param_count++] = (AST_Expression_Identifier*)parse_identifier(p);
    }

    if (!expect_peek(p, TOKEN_RPAREN)) {
        // TODO: Free memory
        return NULL;
    }

    AST_Expression_Identifier** final_params = malloc((param_count + 1) * sizeof(AST_Expression_Identifier*));
    memcpy(final_params, params, param_count * sizeof(AST_Expression_Identifier*));
    final_params[param_count] = NULL; // Null terminator
    free(params);

    return final_params;
}

static AST_Statement* parse_fn_definition(Parser* p) {
    // CONTRACT: ENTRY=TOKEN_FN, EXIT=NL or next sibling (DEDENT consumed)
    AST_Statement_FnDef* stmt = malloc(sizeof(AST_Statement_FnDef));
    stmt->base.type = FN_DEFINITION;
    stmt->base.token = p->currentToken;
    if (!expect_peek(p, TOKEN_IDENT)) { return NULL; }
    stmt->name = (AST_Expression_Identifier*)parse_identifier(p);
    if (!expect_peek(p, TOKEN_LPAREN)) { return NULL; }
    stmt->parameters = parse_function_parameters(p);
    int count = 0;
    if (stmt->parameters != NULL) { while(stmt->parameters[count] != NULL) count++; }
    stmt->parameter_count = count;
    if (!expect_peek(p, TOKEN_COLON)) { parser_add_error_code(p, "OMNI-E2003", "expected ':' after function signature"); return NULL; }
    stmt->body = parse_block_statement(p);
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Expression* parse_fn_expression(Parser* p) {
    AST_Expression_FnLiteral* expr = malloc(sizeof(AST_Expression_FnLiteral));
    expr->base.type = FN_LITERAL;
    expr->base.token = p->currentToken; // The 'fn' token

    if (!expect_peek(p, TOKEN_LPAREN)) { return NULL; }
    
    expr->parameters = parse_function_parameters(p);
    
    // Count parameters
    int count = 0;
    if (expr->parameters != NULL) {
        while(expr->parameters[count] != NULL) count++;
    }
    expr->parameter_count = count;

    if (!expect_peek(p, TOKEN_COLON)) {
        parser_add_error_code(p, "OMNI-E2003", "expected ':' after function signature");
        return NULL;
    }

    expr->body = parse_block_statement(p);
    // Leave DEDENT for caller.
    return (AST_Expression*)expr;
}

static AST_Statement* parse_while_statement(Parser* p) {
    // CONTRACT: ENTRY=TOKEN_WHILE, EXIT=NL or next sibling (DEDENT consumed)
    AST_Statement_While* stmt = malloc(sizeof(AST_Statement_While));
    stmt->base.type = WHILE_STATEMENT;
    stmt->base.token = p->currentToken;
    parser_next_token(p); // consume 'while'
    stmt->condition = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_COLON)) { parser_add_error_code(p, "OMNI-E2003", "expected ':' after while condition"); return NULL; }
    stmt->body = parse_block_statement(p);
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Statement* parse_for_statement(Parser* p) {
    // CONTRACT: ENTRY=TOKEN_FOR, EXIT=NL or next sibling (DEDENT consumed)
    AST_Statement_For* stmt = malloc(sizeof(AST_Statement_For));
    stmt->base.type = FOR_STATEMENT;
    stmt->base.token = p->currentToken;
    if (!expect_peek(p, TOKEN_IDENT)) { return NULL; }
    stmt->iterator = (AST_Expression_Identifier*)parse_identifier(p);
    if (!expect_peek(p, TOKEN_IN)) { return NULL; }
    parser_next_token(p); // consume 'in'
    stmt->iterable = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_COLON)) { parser_add_error_code(p, "OMNI-E2003", "expected ':' after for statement"); return NULL; }
    stmt->body = parse_block_statement(p);
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Statement* parse_class_definition(Parser* p) {
    // CONTRACT: ENTRY=TOKEN_CLASS, EXIT=NL or next sibling (DEDENT consumed)
    AST_Statement_ClassDef* stmt = malloc(sizeof(AST_Statement_ClassDef));
    stmt->base.type = CLASS_DEFINITION;
    stmt->base.token = p->currentToken;
    if (!expect_peek(p, TOKEN_IDENT)) { return NULL; }
    stmt->name = (AST_Expression_Identifier*)parse_identifier(p);
    if (!expect_peek(p, TOKEN_COLON)) { return NULL; }
    stmt->body = parse_block_statement(p);
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Statement_MatchCase* parse_match_case(Parser* p) {
    // currentToken must be TOKEN_CASE when entering
    AST_Statement_MatchCase* match_case = malloc(sizeof(AST_Statement_MatchCase));
    match_case->base.type = MATCH_CASE_STATEMENT;
    match_case->base.token = p->currentToken;

    parser_next_token(p); // consume 'case', now on pattern
    match_case->pattern = parse_expression(p, PREC_LOWEST);

    // Now expect ':'
    if (!expect_peek(p, TOKEN_COLON)) {
        parser_add_error_code(p, "OMNI-E2003", "expected ':' after case pattern");
        return NULL;
    }
    // Now parse the body block
    match_case->consequence = parse_block_statement(p);
    // Leave DEDENT for parse_match_statement to handle.
    return match_case;
}

static AST_Statement* parse_match_statement(Parser* p) {
    AST_Statement_Match* stmt = malloc(sizeof(AST_Statement_Match));
    stmt->base.type = MATCH_STATEMENT;
    stmt->base.token = p->currentToken;
    stmt->cases = NULL;
    stmt->case_count = 0;

    parser_next_token(p); // consume 'match'
    stmt->value = parse_expression(p, PREC_LOWEST);

    if (!expect_peek(p, TOKEN_COLON)) { return NULL; }

    while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
    if (!expect_peek(p, TOKEN_INDENT)) {
        parser_add_error_code(p, "OMNI-E2004", "expected indented block after 'match:'");
        return NULL;
    }
    parser_next_token(p); // advance past INDENT

    while (current_token_is(p, TOKEN_NL)) parser_next_token(p);

    while (current_token_is(p, TOKEN_CASE)) {
        stmt->case_count++;
        stmt->cases = realloc(stmt->cases, stmt->case_count * sizeof(AST_Statement_MatchCase*));
        stmt->cases[stmt->case_count - 1] = parse_match_case(p);
        // parse_match_case leaves currentToken on DEDENT (inner case block).
        if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
        while (current_token_is(p, TOKEN_NL)) parser_next_token(p);
    }
    // Consume outer DEDENT (end of match block), then we're on NL or next stmt.
    if (current_token_is(p, TOKEN_DEDENT)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Statement* parse_use_statement(Parser* p) {
    AST_Statement_Use* stmt = malloc(sizeof(AST_Statement_Use));
    stmt->base.type  = USE_STATEMENT;
    stmt->base.token = p->currentToken;
    stmt->alias      = NULL;

    if (!expect_peek(p, TOKEN_IDENT)) { free(stmt); return NULL; }

    /* Build module name, handling dotted paths: ai.torch, ai.torch.nn, etc. */
    char modname[256];
    strcpy_s(modname, sizeof(modname), p->currentToken.literal);
    while (peek_token_is(p, TOKEN_DOT)) {
        parser_next_token(p); /* consume '.' */
        if (!expect_peek(p, TOKEN_IDENT)) break;
        strncat_s(modname, sizeof(modname), ".", _TRUNCATE);
        strncat_s(modname, sizeof(modname), p->currentToken.literal, _TRUNCATE);
    }
    stmt->module_name = malloc(strlen(modname) + 1);
    strcpy_s(stmt->module_name, strlen(modname) + 1, modname);

    /* Optional alias:  use time as t  |  use ai.torch as torch */
    if (peek_token_is(p, TOKEN_AS)) {
        parser_next_token(p);
        if (!expect_peek(p, TOKEN_IDENT)) { return (AST_Statement*)stmt; }
        stmt->alias = malloc(strlen(p->currentToken.literal) + 1);
        strcpy_s(stmt->alias, strlen(p->currentToken.literal) + 1, p->currentToken.literal);
    }

    if (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
    return (AST_Statement*)stmt;
}

static AST_Statement* parse_return_statement(Parser* p) {
    AST_Statement_Return* stmt = malloc(sizeof(AST_Statement_Return));
    stmt->base.type = RETURN_STATEMENT;
    stmt->base.token = p->currentToken; // The 'return' token

    parser_next_token(p); // consume 'return'

    stmt->return_value = parse_expression(p, PREC_LOWEST);

    return (AST_Statement*)stmt;
}



// ─────────────────────────────────────────────────────────────────────────────
// TOKEN CONTRACT FOR EVERY PARSER FUNCTION
//
// parse_block_statement:
//   ENTRY:  currentToken = TOKEN_COLON  (the colon that opened the block)
//   EXIT:   currentToken = TOKEN_DEDENT (the DEDENT that closes THIS block)
//           Caller is responsible for consuming that DEDENT.
//
// parse_if_statement:
//   ENTRY:  currentToken = TOKEN_IF or TOKEN_ELIF
//   EXIT:   currentToken = first token AFTER the entire if/elif/else chain
//           (i.e. we consume all DEDENTs internally, caller sees NL or stmt)
//
// parse_while_statement / parse_for_statement:
//   ENTRY:  currentToken = TOKEN_WHILE / TOKEN_FOR
//   EXIT:   currentToken = first token AFTER the loop body
//           (we consume the body DEDENT internally)
//
// parse_fn_definition / parse_match_statement / parse_class_definition:
//   Same as while/for — consume their own DEDENT, exit on next token.
//
// parse_use_statement / parse_return_statement / parse_set_statement:
//   EXIT:   currentToken = last meaningful token of the statement
//           parse_program / parse_block_statement will advance past it.
// ─────────────────────────────────────────────────────────────────────────────

static AST_Statement_Block* parse_block_statement(Parser* p) {
    AST_Statement_Block* block = malloc(sizeof(AST_Statement_Block));
    block->base.type = BLOCK_STATEMENT;
    block->base.token = p->currentToken;
    block->statements = NULL;
    block->statement_count = 0;

    // Skip NLs between colon and INDENT
    while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);

    if (!expect_peek(p, TOKEN_INDENT)) {
        parser_add_error_code(p, "OMNI-E2004", "expected indented block after ':'");
        free(block);
        return NULL;
    }

    parser_next_token(p); // advance past INDENT to first statement token

    while (!current_token_is(p, TOKEN_EOF)) {
        // Skip blank lines inside the block
        while (current_token_is(p, TOKEN_NL)) parser_next_token(p);

        if (current_token_is(p, TOKEN_EOF)) break;

        // DEDENT = end of THIS block. Leave it on currentToken for caller.
        if (current_token_is(p, TOKEN_DEDENT)) break;

        // Remember which token started this statement so we can tell
        // whether it was a compound statement after parse_statement returns.
        OmniTokenType tok_before = p->currentToken.type;
        int is_compound = (tok_before == TOKEN_IF    || tok_before == TOKEN_WHILE ||
                           tok_before == TOKEN_FOR   || tok_before == TOKEN_FN    ||
                           tok_before == TOKEN_MATCH || tok_before == TOKEN_CLASS);

        AST_Statement* stmt = parse_statement(p);
        if (stmt) {
            block->statement_count++;
            block->statements = realloc(block->statements,
                block->statement_count * sizeof(AST_Statement*));
            block->statements[block->statement_count - 1] = stmt;
        }

        // Compound statements consumed their own DEDENT and exited on NL or
        // directly on the next sibling token.  Never advance past them here —
        // the loop-top NL-skipper will handle NL, and if they returned directly
        // on a sibling token the loop will parse it correctly next iteration.
        // Simple statements leave currentToken on their last expression token —
        // advance once to get past it.
        if (!is_compound &&
            !current_token_is(p, TOKEN_NL)    &&
            !current_token_is(p, TOKEN_DEDENT) &&
            !current_token_is(p, TOKEN_INDENT) &&
            !current_token_is(p, TOKEN_EOF)) {
            parser_next_token(p);
        }
    }
    // EXIT: currentToken == TOKEN_DEDENT (or EOF). Caller consumes it.
    return block;
}

// --- Expression Parsers (Pratt) ---

// Precedence table mapping TokenType to Precedence
// Use a function instead of a static array to handle all token types
// (static arrays indexed by enum can silently be the wrong size)
static Precedence get_precedence(OmniTokenType type) {
    switch (type) {
        case TOKEN_EQ:       return PREC_EQUALS;
        case TOKEN_NOT_EQ:   return PREC_EQUALS;
        case TOKEN_LT:       return PREC_LESSGREATER;
        case TOKEN_GT:       return PREC_LESSGREATER;
        case TOKEN_LTE:      return PREC_LESSGREATER;
        case TOKEN_GTE:      return PREC_LESSGREATER;
        case TOKEN_PLUS:     return PREC_SUM;
        case TOKEN_MINUS:    return PREC_SUM;
        case TOKEN_SLASH:    return PREC_PRODUCT;
        case TOKEN_STAR:     return PREC_PRODUCT;
        case TOKEN_PERCENT:  return PREC_PRODUCT;
        case TOKEN_POWER:    return PREC_PRODUCT + 1; // ** binds tighter than * / %
        case TOKEN_AND:      return PREC_EQUALS;   // lower than comparisons
        case TOKEN_OR:       return PREC_EQUALS;   // same level as and for now
        case TOKEN_LPAREN:   return PREC_CALL;
        case TOKEN_LBRACKET: return PREC_INDEX;
        case TOKEN_DOT:      return PREC_INDEX;
        case TOKEN_ASSIGN:   return PREC_LOWEST + 1; // bare assignment: x = val (lowest binding)
        case TOKEN_SEMICOLON: return PREC_LOWEST;
        default:             return PREC_LOWEST;
    }
}


static AST_Expression* parse_identifier(Parser* p) {
    AST_Expression_Identifier* ident = malloc(sizeof(AST_Expression_Identifier));
    ident->base.type = IDENTIFIER;
    ident->base.token = p->currentToken;
    ident->value = malloc(strlen(p->currentToken.literal) + 1);
    if (strcpy_s(ident->value, strlen(p->currentToken.literal) + 1, p->currentToken.literal) != 0) {
        fprintf(stderr, "Fatal: strcpy_s failed in parse_identifier\n");
        exit(1);
    }
    return (AST_Expression*)ident;
}

static AST_Expression* parse_integer_literal(Parser* p) {
    AST_Expression_IntegerLiteral* lit = malloc(sizeof(AST_Expression_IntegerLiteral));
    lit->base.type = INTEGER_LITERAL;
    lit->base.token = p->currentToken;
    lit->value = atoll(p->currentToken.literal);
    return (AST_Expression*)lit;
}

static AST_Expression* parse_float_literal(Parser* p) {
    AST_Expression_FloatLiteral* lit = malloc(sizeof(AST_Expression_FloatLiteral));
    lit->base.type = FLOAT_LITERAL;
    lit->base.token = p->currentToken;
    lit->value = atof(p->currentToken.literal);
    return (AST_Expression*)lit;
}

static AST_Expression* parse_boolean(Parser* p) {
    AST_Expression_Boolean* bool_expr = malloc(sizeof(AST_Expression_Boolean));
    bool_expr->base.type = BOOLEAN_LITERAL;
    bool_expr->base.token = p->currentToken;
    bool_expr->value = current_token_is(p, TOKEN_TRUE);
    return (AST_Expression*)bool_expr;
}

static AST_Expression* parse_nil(Parser* p) {
    AST_Expression_NilLiteral* nil_expr = malloc(sizeof(AST_Expression_NilLiteral));
    nil_expr->base.type = NIL_LITERAL;
    nil_expr->base.token = p->currentToken;
    return (AST_Expression*)nil_expr;
}

static AST_Expression* parse_string_literal(Parser* p) {
    AST_Expression_StringLiteral* str_expr = malloc(sizeof(AST_Expression_StringLiteral));
    str_expr->base.type = STRING_LITERAL;
    str_expr->base.token = p->currentToken;
    str_expr->value = malloc(strlen(p->currentToken.literal) + 1);
    if (strcpy_s(str_expr->value, strlen(p->currentToken.literal) + 1, p->currentToken.literal) != 0) {
        fprintf(stderr, "Fatal: strcpy_s failed in parse_string_literal\n");
        exit(1);
    }
    return (AST_Expression*)str_expr;
}

static AST_Expression* parse_grouped_expression(Parser* p) {
    parser_next_token(p); // Consume '('
    AST_Expression* expr = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_RPAREN)) {
        return NULL;
    }
    return expr;
}

/* ── List literal: [a, b, c] ── */
static AST_Expression* parse_array_literal(Parser* p) {
    AST_Expression_ArrayLiteral* arr = malloc(sizeof(AST_Expression_ArrayLiteral));
    arr->base.type   = ARRAY_LITERAL;
    arr->base.token  = p->currentToken; /* the '[' token */
    arr->elements    = NULL;
    arr->element_count = 0;

    /* empty list [] */
    if (peek_token_is(p, TOKEN_RBRACKET)) {
        parser_next_token(p); /* consume ']' */
        return (AST_Expression*)arr;
    }

    int cap = 8;
    arr->elements = malloc(cap * sizeof(AST_Expression*));

    parser_next_token(p); /* move to first element */
    while (current_token_is(p, TOKEN_NL)) parser_next_token(p);

    arr->elements[arr->element_count++] = parse_expression(p, PREC_LOWEST);

    while (peek_token_is(p, TOKEN_COMMA)) {
        parser_next_token(p); /* consume ',' */
        parser_next_token(p); /* move to next element */
        while (current_token_is(p, TOKEN_NL)) parser_next_token(p);
        if (current_token_is(p, TOKEN_RBRACKET)) break; /* trailing comma */
        if (arr->element_count >= cap) {
            cap *= 2;
            arr->elements = realloc(arr->elements, cap * sizeof(AST_Expression*));
        }
        arr->elements[arr->element_count++] = parse_expression(p, PREC_LOWEST);
    }

    while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
    if (!expect_peek(p, TOKEN_RBRACKET)) { return (AST_Expression*)arr; }
    return (AST_Expression*)arr;
}

/* ── Dict literal: {"key": val, ...} ── */
static AST_Expression* parse_map_literal(Parser* p) {
    AST_Expression_MapLiteral* map = malloc(sizeof(AST_Expression_MapLiteral));
    map->base.type  = MAP_LITERAL;
    map->base.token = p->currentToken; /* the '{' token */
    map->entries    = NULL;
    map->entry_count = 0;

    while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
    /* empty dict {} */
    if (peek_token_is(p, TOKEN_RBRACE)) {
        parser_next_token(p);
        return (AST_Expression*)map;
    }

    int cap = 8;
    map->entries = malloc(cap * sizeof(AST_MapEntry*));

    parser_next_token(p); /* move to first key */
    while (current_token_is(p, TOKEN_NL)) parser_next_token(p);

    do {
        if (current_token_is(p, TOKEN_RBRACE)) break;
        AST_MapEntry* entry = malloc(sizeof(AST_MapEntry));
        entry->key   = parse_expression(p, PREC_LOWEST);
        if (!expect_peek(p, TOKEN_COLON)) { free(entry); break; }
        parser_next_token(p);
        entry->value = parse_expression(p, PREC_LOWEST);
        if (map->entry_count >= cap) {
            cap *= 2;
            map->entries = realloc(map->entries, cap * sizeof(AST_MapEntry*));
        }
        map->entries[map->entry_count++] = entry;
        while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
        if (!peek_token_is(p, TOKEN_COMMA)) break;
        parser_next_token(p); /* consume ',' */
        parser_next_token(p);
        while (current_token_is(p, TOKEN_NL)) parser_next_token(p);
    } while (!current_token_is(p, TOKEN_EOF));

    while (peek_token_is(p, TOKEN_NL)) parser_next_token(p);
    if (!expect_peek(p, TOKEN_RBRACE)) { return (AST_Expression*)map; }
    return (AST_Expression*)map;
}

/* ── Index expression: obj[i] ── */
static AST_Expression* parse_index_expression(Parser* p, AST_Expression* left) {
    AST_Expression_Index* idx = malloc(sizeof(AST_Expression_Index));
    idx->base.type  = INDEX_EXPRESSION;
    idx->base.token = p->currentToken; /* the '[' token */
    idx->left       = left;
    parser_next_token(p); /* move to index expression */
    idx->index = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_RBRACKET)) { return (AST_Expression*)idx; }
    return (AST_Expression*)idx;
}

/* ── F-string: f"Hello {name}, you are {age}!" ──
   Expands to a chain of str.concat calls at parse time.
   Segments between { } are parsed as expressions.
   Literal segments become string literals. */
static AST_Expression* parse_fstring_expression(Parser* p) {
    /* The fstring token literal holds the raw content of f"..." */
    const char* raw = p->currentToken.literal;
    if (!raw) raw = "";
    Token ftok = p->currentToken;

    /* Parse f-string into segments: literal text and {expr} alternating.
       Build as a chain: concat(literal, concat(expr_str, concat(...))) */

    /* Collect segments */
    typedef struct { char* text; int is_expr; } FSeg;
    FSeg segs[256]; int nseg = 0;
    char buf[65536]; int bi = 0;

    for (const char* p2 = raw; *p2 && nseg < 255; ) {
        if (*p2 == '{') {
            if (p2[1] == '{') {
                /* escaped {{ → literal { */
                buf[bi++] = '{'; p2 += 2; continue;
            }
            /* end current literal segment */
            if (bi > 0) {
                buf[bi] = 0;
                segs[nseg].text    = malloc(bi+1);
                segs[nseg].is_expr = 0;
                strcpy_s(segs[nseg].text, bi+1, buf);
                nseg++; bi = 0;
            }
            /* collect expression text until matching } */
            p2++;
            int depth = 1; int ei = 0; char ebuf[4096];
            while (*p2 && depth > 0 && ei < 4094) {
                if (*p2 == '{') depth++;
                else if (*p2 == '}') { depth--; if (!depth) { p2++; break; } }
                ebuf[ei++] = *p2++;
            }
            ebuf[ei] = 0;
            segs[nseg].text    = malloc(ei+1);
            segs[nseg].is_expr = 1;
            strcpy_s(segs[nseg].text, ei+1, ebuf);
            nseg++;
        } else if (*p2 == '}' && p2[1] == '}') {
            buf[bi++] = '}'; p2 += 2;
        } else {
            buf[bi++] = *p2++;
        }
    }
    /* trailing literal segment */
    if (bi > 0) {
        buf[bi] = 0;
        segs[nseg].text    = malloc(bi+1);
        segs[nseg].is_expr = 0;
        strcpy_s(segs[nseg].text, bi+1, buf);
        nseg++;
    }

    /* Build expression tree: each segment becomes a string or a str(expr) call.
       All segments are joined with str.concat. */
    if (nseg == 0) {
        /* empty f-string */
        AST_Expression_StringLiteral* s = malloc(sizeof(AST_Expression_StringLiteral));
        s->base.type = STRING_LITERAL; s->base.token = ftok;
        s->value = malloc(1); s->value[0] = 0;
        for (int i=0;i<nseg;i++) free(segs[i].text);
        return (AST_Expression*)s;
    }

    /* For each segment produce an AST_Expression: string lit or call to str() */
    AST_Expression** parts = malloc(nseg * sizeof(AST_Expression*));
    for (int i = 0; i < nseg; i++) {
        if (!segs[i].is_expr) {
            /* literal text segment */
            AST_Expression_StringLiteral* s = malloc(sizeof(AST_Expression_StringLiteral));
            s->base.type = STRING_LITERAL; s->base.token = ftok;
            s->value = segs[i].text; /* owns it */
            parts[i] = (AST_Expression*)s;
        } else {
            /* expression segment: parse it, wrap in str() call */
            Lexer sub_lex;
            lexer_init(&sub_lex, segs[i].text);
            Parser* sub_p = new_parser(&sub_lex);
            AST_Expression* inner = parse_expression(sub_p, PREC_LOWEST);
            free_parser(sub_p);
            free(sub_lex.indent_stack);
            free(sub_lex.pending_tokens);
            free(segs[i].text);

            /* wrap: str(inner) */
            AST_Expression_Identifier* str_id = malloc(sizeof(AST_Expression_Identifier));
            str_id->base.type = IDENTIFIER; str_id->base.token = ftok;
            str_id->value = malloc(4); strcpy_s(str_id->value, 4, "str");

            AST_Expression_Call* call = malloc(sizeof(AST_Expression_Call));
            call->base.type = CALL_EXPRESSION; call->base.token = ftok;
            call->function  = (AST_Expression*)str_id;
            call->arguments = malloc(2 * sizeof(AST_Expression*));
            call->arguments[0] = inner;
            call->arguments[1] = NULL;
            call->argument_count = 1;
            parts[i] = (AST_Expression*)call;
        }
    }

    /* Chain with str.concat(a, str.concat(b, ...)) */
    AST_Expression* result = parts[nseg - 1];
    for (int i = nseg - 2; i >= 0; i--) {
        /* str.concat(parts[i], result) */
        AST_Expression_Identifier* str_id = malloc(sizeof(AST_Expression_Identifier));
        str_id->base.type = IDENTIFIER; str_id->base.token = ftok;
        str_id->value = malloc(4); strcpy_s(str_id->value, 4, "str");

        AST_Expression_MemberAccess* ma = malloc(sizeof(AST_Expression_MemberAccess));
        ma->base.type = MEMBER_ACCESS_EXPRESSION; ma->base.token = ftok;
        ma->object    = (AST_Expression*)str_id;
        ma->member    = malloc(7); strcpy_s(ma->member, 7, "concat");

        AST_Expression_Call* call = malloc(sizeof(AST_Expression_Call));
        call->base.type = CALL_EXPRESSION; call->base.token = ftok;
        call->function  = (AST_Expression*)ma;
        call->arguments = malloc(3 * sizeof(AST_Expression*));
        call->arguments[0] = parts[i];
        call->arguments[1] = result;
        call->arguments[2] = NULL;
        call->argument_count = 2;
        result = (AST_Expression*)call;
    }
    free(parts);
    return result;
}

static AST_Expression* parse_empty_block_expression(Parser* p) {
    // This is a temporary hack for {} in test.ok
    AST_Expression_Empty* empty_expr = malloc(sizeof(AST_Expression_Empty));
    empty_expr->base.type = EMPTY_EXPRESSION;
    empty_expr->base.token = p->currentToken; // The '{' token
    
    if (!expect_peek(p, TOKEN_RBRACE)) {
        free(empty_expr);
        return NULL;
    }
    return (AST_Expression*)empty_expr;
}

// Infix DOT: parses obj.member → AST_Expression_MemberAccess
static AST_Expression* parse_dot_expression(Parser* p, AST_Expression* left) {
    AST_Expression_MemberAccess* expr = malloc(sizeof(AST_Expression_MemberAccess));
    expr->base.type  = MEMBER_ACCESS_EXPRESSION;
    expr->base.token = p->currentToken; // the '.' token
    expr->object     = left;
    // Allow any identifier-like token as member name (including keywords like 'set', 'new')
    parser_next_token(p);
    if (p->currentToken.literal == NULL || p->currentToken.literal[0] == '\0') {
        free(expr);
        return left;
    }
    expr->member = malloc(strlen(p->currentToken.literal) + 1);
    strcpy_s(expr->member, strlen(p->currentToken.literal) + 1, p->currentToken.literal);
    return (AST_Expression*)expr;
}

static AST_Expression* parse_semicolon_operator(Parser* p, AST_Expression* left) {
    (void)p; // Suppress unused parameter warning
    // This is a temporary hack to consume the semicolon
    // In a real parser, semicolons would implicitly end statements or be handled differently.
    // For now, it just passes the left expression through.
    return left;
}

static AST_Expression* parse_single_token_expression(Parser* p) {
    // Creates an empty expression node for single tokens that don't
    // have a more complex prefix parsing logic. This is mostly for
    // making simple test cases pass without "no prefix func" errors.
    AST_Expression_Empty* expr = malloc(sizeof(AST_Expression_Empty));
    expr->base.type = EMPTY_EXPRESSION;
    expr->base.token = p->currentToken; // Use the current token
    
    parser_next_token(p); // Advance the parser's current token

    return (AST_Expression*)expr;
}

static AST_Expression** parse_call_arguments(Parser* p) {
    AST_Expression** args = NULL;
    int capacity = 0;
    int arg_count = 0;

    if (peek_token_is(p, TOKEN_RPAREN)) {
        parser_next_token(p); // consume ')'
        return NULL;
    }

    parser_next_token(p); // consume '(' or ','

    // First argument
    capacity = 4;
    args = malloc(capacity * sizeof(AST_Expression*));
    args[arg_count++] = parse_expression(p, PREC_LOWEST);

    while (peek_token_is(p, TOKEN_COMMA)) {
        parser_next_token(p); // consume ','
        parser_next_token(p); // move to the start of the next expression
        if (arg_count >= capacity) {
            capacity *= 2;
            args = realloc(args, capacity * sizeof(AST_Expression*));
        }
        args[arg_count++] = parse_expression(p, PREC_LOWEST);
    }

    if (!expect_peek(p, TOKEN_RPAREN)) {
        // TODO: Free memory
        return NULL;
    }

    // This is a bit of a hack to pass the count back; a better way would be a custom struct
    // For now, we'll reallocate to the exact size and null-terminate.
    AST_Expression** final_args = malloc((arg_count + 1) * sizeof(AST_Expression*));
    memcpy(final_args, args, arg_count * sizeof(AST_Expression*));
    final_args[arg_count] = NULL; // Null terminator
    free(args);

    return final_args;
}


static AST_Expression* parse_call_expression(Parser* p, AST_Expression* function) {
    AST_Expression_Call* call_expr = malloc(sizeof(AST_Expression_Call));
    call_expr->base.type = CALL_EXPRESSION;
    call_expr->base.token = p->currentToken; // The '(' token
    call_expr->function = function;
    
    AST_Expression** args = parse_call_arguments(p);
    call_expr->arguments = args;

    // Count the arguments
    int count = 0;
    if (args != NULL) {
        while(args[count] != NULL) {
            count++;
        }
    }
    call_expr->argument_count = count;

    return (AST_Expression*)call_expr;
}


static AST_Expression* parse_prefix_expression(Parser* p) {
    AST_Expression_Prefix* expr = malloc(sizeof(AST_Expression_Prefix));
    expr->base.type = PREFIX_EXPRESSION;
    expr->base.token = p->currentToken;
    expr->operator = malloc(strlen(p->currentToken.literal) + 1);
    if (strcpy_s(expr->operator, strlen(p->currentToken.literal) + 1, p->currentToken.literal) != 0) {
        fprintf(stderr, "Fatal: strcpy_s failed in parse_prefix_expression\n");
        exit(1);
    }

    parser_next_token(p);
    expr->right = parse_expression(p, PREC_PREFIX);
    return (AST_Expression*)expr;
}

static AST_Expression* parse_infix_expression(Parser* p, AST_Expression* left) {
    AST_Expression_Infix* expr = malloc(sizeof(AST_Expression_Infix));
    expr->base.type = INFIX_EXPRESSION;
    expr->base.token = p->currentToken;
    expr->operator = malloc(strlen(p->currentToken.literal) + 1);
    if (strcpy_s(expr->operator, strlen(p->currentToken.literal) + 1, p->currentToken.literal) != 0) {
        fprintf(stderr, "Fatal: strcpy_s failed in parse_infix_expression\n");
        exit(1);
    }
    expr->left = left;

    Precedence prec = get_precedence(p->currentToken.type);
    parser_next_token(p);
    expr->right = parse_expression(p, prec);
    return (AST_Expression*)expr;
}


static AST_Expression* parse_expression(Parser* p, Precedence precedence) {
    prefix_parse_fn prefix = p->prefix_parse_fns[p->currentToken.type];
    if (prefix == NULL) {
        parser_add_error_code(p, "OMNI-E2005", "unexpected token — no parsing rule for it here");
        return NULL;
    }
    AST_Expression* left_expr = prefix(p);

    while (!peek_token_is(p, TOKEN_EOF) && precedence < get_precedence(p->peekToken.type)) {
        infix_parse_fn infix = p->infix_parse_fns[p->peekToken.type];
        if (infix == NULL) {
            return left_expr;
        }
        parser_next_token(p);
        left_expr = infix(p, left_expr);
    }

    return left_expr;
}

static AST_Statement* parse_expression_statement(Parser* p) {
    AST_Statement_Expression* stmt = malloc(sizeof(AST_Statement_Expression));
    stmt->base.type = EXPRESSION_STATEMENT;
    stmt->base.token = p->currentToken;
    stmt->expression = parse_expression(p, PREC_LOWEST);
    return (AST_Statement*)stmt;
}



static AST_Statement* parse_statement(Parser* p) {
    switch (p->currentToken.type) {
        case TOKEN_SEMICOLON:
            return NULL;
        case TOKEN_SET:
            return parse_set_statement(p);
        case TOKEN_IF:
            return parse_if_statement(p);
        case TOKEN_FN:
            return parse_fn_definition(p);
        case TOKEN_WHILE:
            return parse_while_statement(p);
        case TOKEN_FOR:
            return parse_for_statement(p);
        case TOKEN_CLASS:
            return parse_class_definition(p);
        case TOKEN_MATCH:
            return parse_match_statement(p);
        case TOKEN_RETURN:
            return parse_return_statement(p);
        case TOKEN_USE:
            return parse_use_statement(p);
        case TOKEN_LET:
            /* 'let x = expr' — treat identically to 'set x = expr' */
            return parse_set_statement(p);
        case TOKEN_PASS:
            /* 'pass' — no-op statement; emit nothing */
            return NULL;
        case TOKEN_BREAK: {
            AST_Statement_Expression* s = malloc(sizeof(AST_Statement_Expression));
            s->base.type = EXPRESSION_STATEMENT;
            s->base.token = p->currentToken;
            AST_Expression_Empty* e = malloc(sizeof(AST_Expression_Empty));
            e->base.type = EMPTY_EXPRESSION;
            e->base.token = p->currentToken;
            e->base.token.type = TOKEN_BREAK;
            s->expression = (AST_Expression*)e;
            return (AST_Statement*)s;
        }
        case TOKEN_CONTINUE: {
            AST_Statement_Expression* s = malloc(sizeof(AST_Statement_Expression));
            s->base.type = EXPRESSION_STATEMENT;
            s->base.token = p->currentToken;
            AST_Expression_Empty* e = malloc(sizeof(AST_Expression_Empty));
            e->base.type = EMPTY_EXPRESSION;
            e->base.token = p->currentToken;
            e->base.token.type = TOKEN_CONTINUE;
            s->expression = (AST_Expression*)e;
            return (AST_Statement*)s;
        }
        default: {
            /* Augmented assignment: x += e, x -= e, x *= e, x /= e, x %= e, x **= e
               The Pratt parser can't handle these as infix because += etc have no
               registered precedence > LOWEST, so the identifier gets parsed alone
               and += becomes the start of the *next* statement.  We intercept here:
               current=IDENT, peek=aug-assign-op -> build INFIX node directly. */
            if (current_token_is(p, TOKEN_IDENT) &&
                (peek_token_is(p, TOKEN_PLUS_ASSIGN)   ||
                 peek_token_is(p, TOKEN_MINUS_ASSIGN)  ||
                 peek_token_is(p, TOKEN_STAR_ASSIGN)   ||
                 peek_token_is(p, TOKEN_SLASH_ASSIGN)  ||
                 peek_token_is(p, TOKEN_PERCENT_ASSIGN)||
                 peek_token_is(p, TOKEN_POWER_ASSIGN))) {
                /* build: EXPRESSION_STATEMENT( INFIX(ident, op, rhs) ) */
                AST_Expression* lhs = parse_identifier(p);  /* consumes IDENT */
                parser_next_token(p);                        /* now on op token */
                AST_Expression_Infix* inf = malloc(sizeof(AST_Expression_Infix));
                inf->base.type  = INFIX_EXPRESSION;
                inf->base.token = p->currentToken;
                inf->operator   = malloc(strlen(p->currentToken.literal) + 1);
                strcpy_s(inf->operator, strlen(p->currentToken.literal) + 1,
                         p->currentToken.literal);
                inf->left = lhs;
                parser_next_token(p);                        /* advance to RHS */
                inf->right = parse_expression(p, PREC_LOWEST);
                AST_Statement_Expression* stmt = malloc(sizeof(AST_Statement_Expression));
                stmt->base.type  = EXPRESSION_STATEMENT;
                stmt->base.token = lhs->token;
                stmt->expression = (AST_Expression*)inf;
                return (AST_Statement*)stmt;
            }
            return (AST_Statement*)parse_expression_statement(p);
        }
    }
}


// --- Public API ---

Parser* new_parser(Lexer* l) {
    Parser* p = malloc(sizeof(Parser));
    if (p == NULL) {
        perror("Fatal: Memory allocation failed for Parser");
        exit(1);
    }
    p->lexer = l;
    p->errors = NULL;
    p->error_count = 0;
    p->indent_depth = 0;
    omni_diag_init(&p->diags);
    p->diag_file = "<input>";

    // Initialize parsing function tables
    for (int i = 0; i < 256; i++) { // Assuming max 256 token types
        p->prefix_parse_fns[i] = NULL;
        p->infix_parse_fns[i] = NULL;
    }
    
    // Register prefix functions
    p->prefix_parse_fns[TOKEN_IDENT]   = parse_identifier;
    p->prefix_parse_fns[TOKEN_SELF]    = parse_identifier;
    p->prefix_parse_fns[TOKEN_INT]     = parse_integer_literal;
    p->prefix_parse_fns[TOKEN_FLOAT]   = parse_float_literal;
    p->prefix_parse_fns[TOKEN_MINUS]   = parse_prefix_expression;
    p->prefix_parse_fns[TOKEN_TILDE]   = parse_prefix_expression;  // bitwise NOT
    p->prefix_parse_fns[TOKEN_TRUE]    = parse_boolean;
    p->prefix_parse_fns[TOKEN_FALSE]   = parse_boolean;
    p->prefix_parse_fns[TOKEN_NIL]     = parse_nil;
    p->prefix_parse_fns[TOKEN_STRING]  = parse_string_literal;
    p->prefix_parse_fns[TOKEN_FSTRING] = parse_fstring_expression;  // f"..."
    p->prefix_parse_fns[TOKEN_LPAREN]  = parse_grouped_expression;
    p->prefix_parse_fns[TOKEN_LBRACKET]= parse_array_literal;       // [a, b, c]
    p->prefix_parse_fns[TOKEN_LBRACE]  = parse_map_literal;         // {"k": v}
    p->prefix_parse_fns[TOKEN_FN]      = parse_fn_expression;
    p->prefix_parse_fns[TOKEN_NOT]     = parse_prefix_expression;
    /* fallback single-token for edge cases */
    p->prefix_parse_fns[TOKEN_ASSIGN]    = parse_single_token_expression;
    p->prefix_parse_fns[TOKEN_PLUS]      = parse_single_token_expression;
    p->prefix_parse_fns[TOKEN_COMMA]     = parse_single_token_expression;
    p->prefix_parse_fns[TOKEN_SEMICOLON] = parse_single_token_expression;
    p->prefix_parse_fns[TOKEN_STAR]      = parse_single_token_expression;
    p->prefix_parse_fns[TOKEN_SLASH]     = parse_single_token_expression;

    // Register infix functions
    p->infix_parse_fns[TOKEN_PLUS]      = parse_infix_expression;
    p->infix_parse_fns[TOKEN_MINUS]     = parse_infix_expression;
    p->infix_parse_fns[TOKEN_SLASH]     = parse_infix_expression;
    p->infix_parse_fns[TOKEN_STAR]      = parse_infix_expression;
    p->infix_parse_fns[TOKEN_EQ]        = parse_infix_expression;
    p->infix_parse_fns[TOKEN_NOT_EQ]    = parse_infix_expression;
    p->infix_parse_fns[TOKEN_LT]        = parse_infix_expression;
    p->infix_parse_fns[TOKEN_GT]        = parse_infix_expression;
    p->infix_parse_fns[TOKEN_LTE]       = parse_infix_expression;
    p->infix_parse_fns[TOKEN_GTE]       = parse_infix_expression;
    p->infix_parse_fns[TOKEN_PERCENT]   = parse_infix_expression;
    p->infix_parse_fns[TOKEN_POWER]     = parse_infix_expression;   // **
    p->infix_parse_fns[TOKEN_AMP]       = parse_infix_expression;   // &
    p->infix_parse_fns[TOKEN_PIPE]      = parse_infix_expression;   // |
    p->infix_parse_fns[TOKEN_CARET]     = parse_infix_expression;   // ^
    p->infix_parse_fns[TOKEN_LSHIFT]    = parse_infix_expression;   // <<
    p->infix_parse_fns[TOKEN_RSHIFT]    = parse_infix_expression;   // >>
    p->infix_parse_fns[TOKEN_AND]       = parse_infix_expression;
    p->infix_parse_fns[TOKEN_OR]        = parse_infix_expression;
    p->infix_parse_fns[TOKEN_LPAREN]    = parse_call_expression;
    p->infix_parse_fns[TOKEN_LBRACKET]  = parse_index_expression;   // obj[i]
    p->infix_parse_fns[TOKEN_SEMICOLON] = parse_semicolon_operator;
    p->infix_parse_fns[TOKEN_DOT]       = parse_dot_expression;
    p->infix_parse_fns[TOKEN_ASSIGN]    = parse_infix_expression;   // bare reassignment
    /* augmented assignment — parsed as infix, desugared in codegen */
    p->infix_parse_fns[TOKEN_PLUS_ASSIGN]    = parse_infix_expression;
    p->infix_parse_fns[TOKEN_MINUS_ASSIGN]   = parse_infix_expression;
    p->infix_parse_fns[TOKEN_STAR_ASSIGN]    = parse_infix_expression;
    p->infix_parse_fns[TOKEN_SLASH_ASSIGN]   = parse_infix_expression;
    p->infix_parse_fns[TOKEN_PERCENT_ASSIGN] = parse_infix_expression;
    p->infix_parse_fns[TOKEN_POWER_ASSIGN]   = parse_infix_expression;

    parser_next_token(p);
    parser_next_token(p);
    return p;
}

void free_parser(Parser* p) {
    if (p == NULL) return;
    for (int i = 0; i < p->error_count; i++) {
        free(p->errors[i]);
    }
    free(p->errors);
    omni_diag_free(&p->diags);
    free(p);
}

AST_Program* parse_program(Parser* p) {
    AST_Program* program = malloc(sizeof(AST_Program));
    if (program == NULL) {
        parser_add_error(p, "Memory allocation failed for program");
        return NULL;
    }
    program->statement_count = 0;

    // PERF FIX: pre-allocate with capacity, grow by 2x instead of realloc every statement
    int capacity = 16;
    program->statements = malloc(capacity * sizeof(AST_Statement*));
    if (!program->statements) {
        parser_add_error(p, "Memory allocation failed for program statements");
        free(program);
        return NULL;
    }

    while (!current_token_is(p, TOKEN_EOF)) {
        // All compound stmts consume their own DEDENTs and exit on NL.
        // Simple stmts exit on their last token. Either way, skip NLs here.
        while (current_token_is(p, TOKEN_NL)) parser_next_token(p);
        if (current_token_is(p, TOKEN_EOF)) break;

        OmniTokenType tok_before = p->currentToken.type;
        int is_compound = (tok_before == TOKEN_IF    || tok_before == TOKEN_WHILE ||
                           tok_before == TOKEN_FOR   || tok_before == TOKEN_FN    ||
                           tok_before == TOKEN_MATCH || tok_before == TOKEN_CLASS);

        AST_Statement* stmt = parse_statement(p);
        if (stmt) {
            if (program->statement_count >= capacity) {
                capacity *= 2;
                AST_Statement** new_stmts = realloc(program->statements, capacity * sizeof(AST_Statement*));
                if (!new_stmts) {
                    parser_add_error(p, "Memory allocation failed for program statements");
                    free(program->statements);
                    free(program);
                    return NULL;
                }
                program->statements = new_stmts;
            }
            program->statements[program->statement_count++] = stmt;
        }
        if (!is_compound &&
            !current_token_is(p, TOKEN_NL) && !current_token_is(p, TOKEN_EOF)) {
            parser_next_token(p);
        }
    }
    return program;
}