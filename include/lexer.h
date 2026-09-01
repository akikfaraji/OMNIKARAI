#ifndef OMNIKARAI_LEXER_H
#define OMNIKARAI_LEXER_H

typedef enum {
    // SPECIAL
    TOKEN_ILLEGAL,
    TOKEN_EOF,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_NL,

    // LITERALS
    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_FSTRING,       // f"Hello {name}!"

    // BASIC OPERATORS
    TOKEN_ASSIGN,        // =
    TOKEN_PLUS,          // +
    TOKEN_MINUS,         // -
    TOKEN_STAR,          // *
    TOKEN_SLASH,         // /
    TOKEN_PERCENT,       // %
    TOKEN_POWER,         // **

    // COMPARISON
    TOKEN_EQ,            // ==
    TOKEN_NOT_EQ,        // !=
    TOKEN_LT,            // <
    TOKEN_GT,            // >
    TOKEN_LTE,           // <=
    TOKEN_GTE,           // >=

    // AUGMENTED ASSIGNMENT
    TOKEN_PLUS_ASSIGN,   // +=
    TOKEN_MINUS_ASSIGN,  // -=
    TOKEN_STAR_ASSIGN,   // *=
    TOKEN_SLASH_ASSIGN,  // /=
    TOKEN_PERCENT_ASSIGN,// %=
    TOKEN_POWER_ASSIGN,  // **=
    TOKEN_AMP_ASSIGN,    // &=
    TOKEN_PIPE_ASSIGN,   // |=
    TOKEN_CARET_ASSIGN,  // ^=

    // BITWISE
    TOKEN_AMP,           // &
    TOKEN_PIPE,          // |
    TOKEN_CARET,         // ^
    TOKEN_TILDE,         // ~
    TOKEN_LSHIFT,        // <<
    TOKEN_RSHIFT,        // >>

    // MISC OPERATORS
    TOKEN_ARROW,         // ->
    TOKEN_AT,            // @  (decorator / matrix mul)

    // DELIMITERS
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DCOLON,        // ::
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_SEMICOLON,
    TOKEN_DOT,
    TOKEN_ELLIPSIS,      // ...

    // KEYWORDS — control flow
    TOKEN_IF,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_PASS,
    TOKEN_RETURN,
    TOKEN_YIELD,
    TOKEN_MATCH,
    TOKEN_CASE,
    TOKEN_WITH,

    // KEYWORDS — definitions
    TOKEN_FN,
    TOKEN_CLASS,
    TOKEN_EXTENDS,
    TOKEN_SELF,
    TOKEN_SET,
    TOKEN_LET,           // alias for set
    TOKEN_CONST,

    // KEYWORDS — modules
    TOKEN_USE,
    TOKEN_IMPORT,
    TOKEN_FROM,
    TOKEN_AS,

    // KEYWORDS — exceptions
    TOKEN_TRY,
    TOKEN_EXCEPT,
    TOKEN_RAISE,
    TOKEN_DEL,

    // KEYWORDS — logic
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,

    // KEYWORDS — literals
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NIL,

    TOKEN_COUNT   // sentinel — must be last
} OmniTokenType;

typedef struct {
    OmniTokenType type;
    char*         literal;
    int           line;
    int           col;
} Token;

typedef struct {
    const char* input;
    size_t      input_len;
    size_t      position;
    size_t      readPosition;
    char        ch;
    int         at_bol;          // 1 when at the start of a new logical line
    int         line_num;
    int         col_num;
    int         bracket_depth;   // ( [ { nesting depth — suppresses INDENT/DEDENT inside
    int*        indent_stack;
    int         indent_level;
    Token*      pending_tokens;
    int         pending_count;
} Lexer;

void  lexer_init(Lexer* l, const char* source_code);
Token get_next_token(Lexer* l);
const char* omni_token_name(OmniTokenType t);  /* diagnostics: human-readable token name */

#endif /* OMNIKARAI_LEXER_H */
