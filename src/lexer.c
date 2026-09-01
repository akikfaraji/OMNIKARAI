#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "omni_platform.h"
#include "lexer.h"

#define INDENT_STACK_SIZE 100
#define PENDING_TOKEN_SIZE 512

static void read_char(Lexer* l);
static char peek_char(Lexer* l);
static Token new_token(OmniTokenType type, const char* literal);
static char* read_identifier(Lexer* l);
static char* read_number(Lexer* l);
static char* read_string(Lexer* l);
static OmniTokenType lookup_ident(const char* ident);
static void handle_leading_whitespace_and_comments(Lexer* l);

void lexer_init(Lexer* l, const char* source_code) {
    l->input        = source_code;
    l->input_len    = strlen(source_code);
    l->position     = 0;
    l->readPosition = 0;
    l->ch           = 0;
    l->at_bol       = 1;
    l->line_num     = 1;
    l->col_num      = 0;
    l->bracket_depth = 0;  // NEW: tracks ( [ { nesting depth

    l->indent_stack = malloc(sizeof(int) * INDENT_STACK_SIZE);
    if (!l->indent_stack) { fprintf(stderr,"Fatal: OOM indent_stack\n"); exit(1); }
    l->indent_level = 0;
    l->indent_stack[0] = 0;

    l->pending_tokens = malloc(sizeof(Token) * PENDING_TOKEN_SIZE);
    if (!l->pending_tokens) { fprintf(stderr,"Fatal: OOM pending_tokens\n"); exit(1); }
    l->pending_count = 0;

    read_char(l);
}

static void read_char(Lexer* l) {
    l->ch = (l->readPosition >= l->input_len) ? 0 : l->input[l->readPosition];
    l->position = l->readPosition++;
    if (l->ch == '\n') l->col_num = 0; else l->col_num++;
}

static char peek_char(Lexer* l) {
    return (l->readPosition >= l->input_len) ? 0 : l->input[l->readPosition];
}

static Token new_token(OmniTokenType type, const char* literal) {
    Token tok;
    tok.type    = type;
    tok.literal = (char*)literal;
    tok.line    = 0;
    tok.col     = 0;
    return tok;
}

/* Human-readable token names for diagnostics (docs/DIAGNOSTICS.md).
   Punctuation returns its glyph so messages read naturally:
   "expected ':' but found identifier 'x'". Used by parser + CLI. */
const char* omni_token_name(OmniTokenType t) {
    switch (t) {
        case TOKEN_ILLEGAL:   return "illegal character";
        case TOKEN_EOF:       return "end of file";
        case TOKEN_INDENT:    return "indent";
        case TOKEN_DEDENT:    return "dedent";
        case TOKEN_NL:        return "end of line";
        case TOKEN_IDENT:     return "identifier";
        case TOKEN_INT:       return "integer";
        case TOKEN_FLOAT:     return "float";
        case TOKEN_STRING:    return "string";
        case TOKEN_FSTRING:   return "f-string";
        case TOKEN_ASSIGN:    return "'='";
        case TOKEN_PLUS:      return "'+'";
        case TOKEN_MINUS:     return "'-'";
        case TOKEN_STAR:      return "'*'";
        case TOKEN_SLASH:     return "'/'";
        case TOKEN_PERCENT:   return "'%'";
        case TOKEN_POWER:     return "'**'";
        case TOKEN_EQ:        return "'=='";
        case TOKEN_NOT_EQ:    return "'!='";
        case TOKEN_LT:        return "'<'";
        case TOKEN_GT:        return "'>'";
        case TOKEN_LTE:       return "'<='";
        case TOKEN_GTE:       return "'>='";
        case TOKEN_PLUS_ASSIGN:    return "'+='";
        case TOKEN_MINUS_ASSIGN:   return "'-='";
        case TOKEN_STAR_ASSIGN:    return "'*='";
        case TOKEN_SLASH_ASSIGN:   return "'/='";
        case TOKEN_PERCENT_ASSIGN: return "'%='";
        case TOKEN_POWER_ASSIGN:   return "'**='";
        case TOKEN_AMP_ASSIGN:     return "'&='";
        case TOKEN_PIPE_ASSIGN:    return "'|='";
        case TOKEN_CARET_ASSIGN:   return "'^='";
        case TOKEN_AMP:      return "'&'";
        case TOKEN_PIPE:     return "'|'";
        case TOKEN_CARET:    return "'^'";
        case TOKEN_TILDE:    return "'~'";
        case TOKEN_LSHIFT:   return "'<<'";
        case TOKEN_RSHIFT:   return "'>>'";
        case TOKEN_ARROW:    return "'->'";
        case TOKEN_AT:       return "'@'";
        case TOKEN_COMMA:     return "','";
        case TOKEN_COLON:     return "':'";
        case TOKEN_DCOLON:    return "'::'";
        case TOKEN_LPAREN:    return "'('";
        case TOKEN_RPAREN:    return "')'";
        case TOKEN_LBRACKET:  return "'['";
        case TOKEN_RBRACKET:  return "']'";
        case TOKEN_LBRACE:    return "'{'";
        case TOKEN_RBRACE:    return "'}'";
        case TOKEN_SEMICOLON: return "';'";
        case TOKEN_DOT:       return "'.'";
        case TOKEN_ELLIPSIS:  return "'...'";
        case TOKEN_IF:      return "'if'";
        case TOKEN_ELIF:    return "'elif'";
        case TOKEN_ELSE:    return "'else'";
        case TOKEN_WHILE:   return "'while'";
        case TOKEN_FOR:     return "'for'";
        case TOKEN_IN:      return "'in'";
        case TOKEN_BREAK:   return "'break'";
        case TOKEN_CONTINUE:return "'continue'";
        case TOKEN_PASS:    return "'pass'";
        case TOKEN_RETURN:  return "'return'";
        case TOKEN_YIELD:   return "'yield'";
        case TOKEN_MATCH:   return "'match'";
        case TOKEN_CASE:    return "'case'";
        case TOKEN_WITH:    return "'with'";
        case TOKEN_FN:      return "'fn'";
        case TOKEN_CLASS:   return "'class'";
        case TOKEN_EXTENDS: return "'extends'";
        case TOKEN_SELF:    return "'self'";
        case TOKEN_SET:     return "'set'";
        case TOKEN_LET:     return "'let'";
        case TOKEN_CONST:   return "'const'";
        case TOKEN_USE:     return "'use'";
        case TOKEN_IMPORT:  return "'import'";
        case TOKEN_FROM:    return "'from'";
        case TOKEN_AS:      return "'as'";
        case TOKEN_TRY:     return "'try'";
        case TOKEN_EXCEPT:  return "'except'";
        case TOKEN_RAISE:   return "'raise'";
        case TOKEN_DEL:     return "'del'";
        case TOKEN_AND:     return "'and'";
        case TOKEN_OR:      return "'or'";
        case TOKEN_NOT:     return "'not'";
        case TOKEN_TRUE:    return "'true'";
        case TOKEN_FALSE:   return "'false'";
        case TOKEN_NIL:     return "'nil'";
        default:            return "token";
    }
}


static int is_letter(char ch) { return isalpha((unsigned char)ch) || ch == '_'; }

static void skip_inline_whitespace(Lexer* l) {
    while (l->ch == ' ' || l->ch == '\t') read_char(l);
}

static OmniTokenType lookup_ident(const char* ident) {
    switch (ident[0]) {
        case 'a': if (!strcmp(ident,"and"))  return TOKEN_AND;
                  if (!strcmp(ident,"as"))   return TOKEN_AS;   break;
        case 'b': if (!strcmp(ident,"break")) return TOKEN_BREAK; break;
        case 'c': if (!strcmp(ident,"class"))    return TOKEN_CLASS;
                  if (!strcmp(ident,"case"))     return TOKEN_CASE;
                  if (!strcmp(ident,"continue")) return TOKEN_CONTINUE;
                  if (!strcmp(ident,"const"))    return TOKEN_CONST;  break;
        case 'd': if (!strcmp(ident,"del"))      return TOKEN_DEL;    break;
        case 'e': if (!strcmp(ident,"elif"))     return TOKEN_ELIF;
                  if (!strcmp(ident,"else"))     return TOKEN_ELSE;
                  if (!strcmp(ident,"extends"))  return TOKEN_EXTENDS;
                  if (!strcmp(ident,"except"))   return TOKEN_EXCEPT; break;
        case 'f': if (!strcmp(ident,"fn"))    return TOKEN_FN;
                  if (!strcmp(ident,"for"))   return TOKEN_FOR;
                  if (!strcmp(ident,"false")) return TOKEN_FALSE;
                  if (!strcmp(ident,"from"))  return TOKEN_FROM;  break;
        case 'i': if (!strcmp(ident,"if"))     return TOKEN_IF;
                  if (!strcmp(ident,"in"))     return TOKEN_IN;
                  if (!strcmp(ident,"import")) return TOKEN_IMPORT;  break;
        case 'l': if (!strcmp(ident,"let"))   return TOKEN_LET;      break;
        case 'm': if (!strcmp(ident,"match")) return TOKEN_MATCH;    break;
        case 'n': if (!strcmp(ident,"not"))   return TOKEN_NOT;
                  if (!strcmp(ident,"nil"))   return TOKEN_NIL;      break;
        case 'o': if (!strcmp(ident,"or"))    return TOKEN_OR;       break;
        case 'p': if (!strcmp(ident,"pass"))  return TOKEN_PASS;     break;
        case 'r': if (!strcmp(ident,"return")) return TOKEN_RETURN;
                  if (!strcmp(ident,"raise"))  return TOKEN_RAISE;   break;
        case 's': if (!strcmp(ident,"set"))    return TOKEN_SET;
                  if (!strcmp(ident,"self"))   return TOKEN_SELF;    break;
        case 't': if (!strcmp(ident,"true"))   return TOKEN_TRUE;
                  if (!strcmp(ident,"try"))    return TOKEN_TRY;     break;
        case 'u': if (!strcmp(ident,"use"))    return TOKEN_USE;     break;
        case 'w': if (!strcmp(ident,"while"))  return TOKEN_WHILE;
                  if (!strcmp(ident,"with"))   return TOKEN_WITH;    break;
        case 'y': if (!strcmp(ident,"yield"))  return TOKEN_YIELD;   break;
    }
    return TOKEN_IDENT;
}

static char* read_identifier(Lexer* l) {
    size_t start = l->position;
    while (is_letter(l->ch) || isdigit((unsigned char)l->ch)) read_char(l);
    size_t len = l->position - start;
    char* s = malloc(len + 1);
    if (!s) { fprintf(stderr,"Fatal: OOM\n"); exit(1); }
    strncpy_s(s, len+1, &l->input[start], len);
    return s;
}

/* read_number: decimal, hex (0x), binary (0b), float, scientific */
static char* read_number(Lexer* l) {
    size_t start = l->position;
    if (l->ch == '0' && (peek_char(l)=='x'||peek_char(l)=='X')) {
        read_char(l); read_char(l);
        while (isxdigit((unsigned char)l->ch)) read_char(l);
    } else if (l->ch == '0' && (peek_char(l)=='b'||peek_char(l)=='B')) {
        read_char(l); read_char(l);
        while (l->ch=='0'||l->ch=='1') read_char(l);
    } else {
        while (isdigit((unsigned char)l->ch)) read_char(l);
        if (l->ch=='.' && isdigit((unsigned char)peek_char(l))) {
            read_char(l);
            while (isdigit((unsigned char)l->ch)) read_char(l);
        }
        if (l->ch=='e'||l->ch=='E') {
            read_char(l);
            if (l->ch=='+'||l->ch=='-') read_char(l);
            while (isdigit((unsigned char)l->ch)) read_char(l);
        }
    }
    size_t len = l->position - start;
    char* s = malloc(len+1);
    if (!s) { fprintf(stderr,"Fatal: OOM\n"); exit(1); }
    strncpy_s(s, len+1, &l->input[start], len);
    return s;
}

/* read_string: handles escape sequences, both ' and " quotes,
   and triple-quoted strings """...""" / '''...''' */
static char* read_string(Lexer* l) {
    char quote = l->ch;
    read_char(l); // consume opening quote

    /* triple-quoted string: """...""" or '''...''' */
    int triple = 0;
    if (l->ch == quote && peek_char(l) == quote) {
        read_char(l); read_char(l); // consume 2nd and 3rd opening quotes
        triple = 1;
    }

    char* buf = malloc(l->input_len + 4);
    if (!buf) { fprintf(stderr,"Fatal: OOM\n"); exit(1); }
    size_t out = 0;

    if (triple) {
        /* triple-quoted: read until matching triple quote, preserve newlines */
        while (l->ch != 0) {
            if (l->ch == quote && peek_char(l) == quote) {
                /* peek two ahead for third */
                size_t save_pos = l->position;
                char save_ch = l->ch;
                (void)save_pos; (void)save_ch;
                read_char(l); /* second quote */
                if (l->ch == quote) {
                    read_char(l); /* third quote */
                    break; /* end of triple string */
                }
                /* only two quotes — emit them and continue */
                buf[out++] = quote;
                buf[out++] = quote;
                continue;
            }
            if (l->ch == '\n') l->line_num++;
            buf[out++] = l->ch;
            read_char(l);
        }
    } else {
        /* normal string */
        while (l->ch != quote && l->ch != 0 && l->ch != '\n') {
            if (l->ch == '\\') {
                read_char(l);
                switch (l->ch) {
                    case 'n':  buf[out++] = '\n'; break;
                    case 't':  buf[out++] = '\t'; break;
                    case 'r':  buf[out++] = '\r'; break;
                    case '\\': buf[out++] = '\\'; break;
                    case '"':  buf[out++] = '"';  break;
                    case '\'': buf[out++] = '\''; break;
                    case '0':  buf[out++] = '\0'; break;
                    case 'x': {
                        read_char(l); char h1 = l->ch;
                        read_char(l); char h2 = l->ch;
                        char hex[3] = {h1, h2, 0};
                        buf[out++] = (char)strtol(hex, NULL, 16);
                        break;
                    }
                    default: buf[out++] = '\\'; buf[out++] = l->ch; break;
                }
            } else {
                buf[out++] = l->ch;
            }
            read_char(l);
        }
        if (l->ch == quote) read_char(l); /* consume closing quote */
    }

    buf[out] = '\0';
    return buf;
}

/* read_fstring_raw: reads the raw content of f"..." for the parser to expand */
static char* read_fstring_raw(Lexer* l) {
    return read_string(l);
}

/* ── INDENTATION HANDLER ───────────────────────────────────────────────────
   Called at the beginning of every line (at_bol==1).
   Measures the new indentation level and pushes INDENT / DEDENT tokens.
   CRITICAL: When bracket_depth > 0 (inside ( [ { ), newlines and indentation
   are suppressed — this is Python's implicit line continuation rule.
   Without this, a multi-line list like:
       set x = [
           1, 2, 3
       ]
   would generate spurious INDENT/DEDENT tokens and break parsing.
*/
static void handle_leading_whitespace_and_comments(Lexer* l) {
    int new_indent = 0;

    while (l->ch != 0) {
        if (l->ch == '\r') { read_char(l); continue; }

        if (l->ch == ' ' || l->ch == '\t') {
            new_indent = 0;
            while (l->ch == ' ' || l->ch == '\t') {
                new_indent += (l->ch == ' ') ? 1 : 4;
                read_char(l);
            }
            continue;
        }

        if (l->ch == '\n') {
            /* blank / whitespace-only line: if not inside brackets, emit NL */
            if (l->bracket_depth == 0) {
                if (l->pending_count == 0 ||
                    l->pending_tokens[l->pending_count-1].type != TOKEN_NL) {
                    if (l->pending_count >= PENDING_TOKEN_SIZE) {
                        fprintf(stderr,"Fatal: pending overflow\n"); exit(1);
                    }
                    Token nl = new_token(TOKEN_NL, "\\n");
                    nl.line = l->line_num; nl.col = l->col_num;
                    l->pending_tokens[l->pending_count++] = nl;
                }
            }
            l->line_num++; read_char(l); l->at_bol = 1; new_indent = 0;
            continue;
        }

        if (l->ch == '#') {
            if (peek_char(l) == '|') {
                /* multi-line comment #| ... |# */
                read_char(l); read_char(l);
                while (l->ch != 0) {
                    if (l->ch == '|' && peek_char(l) == '#') {
                        read_char(l); read_char(l); break;
                    }
                    if (l->ch == '\n') l->line_num++;
                    read_char(l);
                }
            } else {
                /* single-line comment */
                while (l->ch != '\n' && l->ch != 0) read_char(l);
            }
            l->at_bol = 1; new_indent = 0; continue;
        }

        /* line continuation: backslash at end of line */
        if (l->ch == '\\' && peek_char(l) == '\n') {
            read_char(l); /* consume \ */
            l->line_num++; read_char(l); /* consume \n */
            new_indent = 0; continue;
        }

        break; /* non-whitespace content found */
    }

    if (l->ch != 0) {
        /* Inside brackets: suppress all indentation tokens.
           The lexer still counts bracket depth; INDENT/DEDENT are only
           emitted at the top level of each logical line. */
        if (l->bracket_depth > 0) {
            l->at_bol = 0;
            return;
        }

        int current = l->indent_stack[l->indent_level];
        if (new_indent > current) {
            l->indent_level++;
            if (l->indent_level >= INDENT_STACK_SIZE) {
                fprintf(stderr,"Fatal: indent stack overflow at line %d\n", l->line_num);
                exit(1);
            }
            l->indent_stack[l->indent_level] = new_indent;
            if (l->pending_count >= PENDING_TOKEN_SIZE) {
                fprintf(stderr,"Fatal: pending overflow\n"); exit(1);
            }
            l->pending_tokens[l->pending_count++] = new_token(TOKEN_INDENT, "INDENT");
        } else if (new_indent < current) {
            while (l->indent_level > 0 && l->indent_stack[l->indent_level] > new_indent) {
                l->indent_level--;
                if (l->pending_count >= PENDING_TOKEN_SIZE) {
                    fprintf(stderr,"Fatal: pending overflow\n"); exit(1);
                }
                l->pending_tokens[l->pending_count++] = new_token(TOKEN_DEDENT, "DEDENT");
            }
            if (l->indent_stack[l->indent_level] != new_indent) {
                fprintf(stderr,
                    "IndentationError: unexpected dedent at line %d (level=%d expected=%d)\n",
                    l->line_num, new_indent, l->indent_stack[l->indent_level]);
                exit(1);
            }
        }
    } else {
        /* EOF: close all open indentation levels */
        while (l->indent_stack[l->indent_level] > 0) {
            l->indent_level--;
            if (l->pending_count >= PENDING_TOKEN_SIZE) {
                fprintf(stderr,"Fatal: pending overflow\n"); exit(1);
            }
            l->pending_tokens[l->pending_count++] = new_token(TOKEN_DEDENT, "DEDENT");
        }
    }

    l->at_bol = 0;
}

Token get_next_token(Lexer* l) {
    Token tok;

    /* drain any queued INDENT/DEDENT/NL tokens first */
    while (l->at_bol || l->ch == ' ' || l->ch == '\t' || l->ch == '\r' ||
           l->ch == '\n' || l->ch == '#') {
        if (l->pending_count > 0) {
            l->pending_count--;
            Token t = l->pending_tokens[l->pending_count];
            if (t.line == 0) { t.line = l->line_num; t.col = l->col_num; }
            return t;
        }
        if (l->at_bol) {
            handle_leading_whitespace_and_comments(l);
        } else {
            if (l->ch == ' ' || l->ch == '\t') {
                skip_inline_whitespace(l); continue;
            } else if (l->ch == '\r') {
                read_char(l); continue;
            } else if (l->ch == '#') {
                while (l->ch != '\n' && l->ch != 0) read_char(l);
                l->at_bol = 1; continue;
            } else if (l->ch == '\n') {
                /* Inside brackets: swallow newlines silently */
                if (l->bracket_depth > 0) {
                    l->line_num++; read_char(l); continue;
                }
                if (l->pending_count == 0 ||
                    l->pending_tokens[l->pending_count-1].type != TOKEN_NL) {
                    if (l->pending_count >= PENDING_TOKEN_SIZE) {
                        fprintf(stderr,"Fatal: pending overflow\n"); exit(1);
                    }
                    l->pending_tokens[l->pending_count++] = new_token(TOKEN_NL, "\\n");
                }
                l->line_num++; read_char(l); l->at_bol = 1; continue;
            }
            break;
        }
    }

    if (l->pending_count > 0) {
        l->pending_count--;
        Token t = l->pending_tokens[l->pending_count];
        if (t.line == 0) { t.line = l->line_num; t.col = l->col_num; }
        return t;
    }
    while (l->ch == '\r') read_char(l);

    int tok_line = l->line_num, tok_col = l->col_num;

    /* f-string: f"..." or f'...' */
    if ((l->ch == 'f' || l->ch == 'F') &&
        (peek_char(l) == '"' || peek_char(l) == '\'')) {
        read_char(l); /* consume 'f' */
        tok.literal = read_fstring_raw(l);
        tok.type    = TOKEN_FSTRING;
        tok.line    = tok_line; tok.col = tok_col;
        return tok;
    }

    switch (l->ch) {
        /* ── comparison / assignment ── */
        case '=':
            if (peek_char(l) == '=') { read_char(l); tok = new_token(TOKEN_EQ, "=="); }
            else                     { tok = new_token(TOKEN_ASSIGN, "="); }
            break;
        case '!':
            if (peek_char(l) == '=') { read_char(l); tok = new_token(TOKEN_NOT_EQ, "!="); }
            else                     { tok = new_token(TOKEN_ILLEGAL, "!"); }
            break;
        case '<':
            if (peek_char(l)=='=')       { read_char(l); tok = new_token(TOKEN_LTE, "<="); }
            else if (peek_char(l)=='<')  { read_char(l); tok = new_token(TOKEN_LSHIFT, "<<"); }
            else                         { tok = new_token(TOKEN_LT,  "<"); }
            break;
        case '>':
            if (peek_char(l)=='=')       { read_char(l); tok = new_token(TOKEN_GTE, ">="); }
            else if (peek_char(l)=='>')  { read_char(l); tok = new_token(TOKEN_RSHIFT, ">>"); }
            else                         { tok = new_token(TOKEN_GT,  ">"); }
            break;
        /* ── arithmetic / augmented assignment ── */
        case '+':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_PLUS_ASSIGN,  "+="); }
            else                   { tok = new_token(TOKEN_PLUS,  "+"); }
            break;
        case '-':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_MINUS_ASSIGN, "-="); }
            else if (peek_char(l)=='>') { read_char(l); tok = new_token(TOKEN_ARROW, "->"); }
            else                   { tok = new_token(TOKEN_MINUS, "-"); }
            break;
        case '*':
            if (peek_char(l)=='*') {
                read_char(l);
                if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_POWER_ASSIGN, "**="); }
                else                   { tok = new_token(TOKEN_POWER, "**"); }
            } else if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_STAR_ASSIGN, "*="); }
            else { tok = new_token(TOKEN_STAR, "*"); }
            break;
        case '/':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_SLASH_ASSIGN, "/="); }
            else                   { tok = new_token(TOKEN_SLASH, "/"); }
            break;
        case '%':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_PERCENT_ASSIGN, "%="); }
            else                   { tok = new_token(TOKEN_PERCENT, "%"); }
            break;
        case '&':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_AMP_ASSIGN, "&="); }
            else                   { tok = new_token(TOKEN_AMP, "&"); }
            break;
        case '|':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_PIPE_ASSIGN, "|="); }
            else                   { tok = new_token(TOKEN_PIPE, "|"); }
            break;
        case '^':
            if (peek_char(l)=='=') { read_char(l); tok = new_token(TOKEN_CARET_ASSIGN, "^="); }
            else                   { tok = new_token(TOKEN_CARET, "^"); }
            break;
        case '~': tok = new_token(TOKEN_TILDE,     "~"); break;
        /* ── bracket pairs — track depth ── */
        case '(':
            l->bracket_depth++;
            tok = new_token(TOKEN_LPAREN, "(");
            break;
        case ')':
            if (l->bracket_depth > 0) l->bracket_depth--;
            tok = new_token(TOKEN_RPAREN, ")");
            break;
        case '[':
            l->bracket_depth++;
            tok = new_token(TOKEN_LBRACKET, "[");
            break;
        case ']':
            if (l->bracket_depth > 0) l->bracket_depth--;
            tok = new_token(TOKEN_RBRACKET, "]");
            break;
        case '{':
            l->bracket_depth++;
            tok = new_token(TOKEN_LBRACE, "{");
            break;
        case '}':
            if (l->bracket_depth > 0) l->bracket_depth--;
            tok = new_token(TOKEN_RBRACE, "}");
            break;
        /* ── punctuation ── */
        case ',': tok = new_token(TOKEN_COMMA,     ","); break;
        case ':':
            if (peek_char(l)==':') { read_char(l); tok = new_token(TOKEN_DCOLON, "::"); }
            else                   { tok = new_token(TOKEN_COLON,  ":"); }
            break;
        case ';': tok = new_token(TOKEN_SEMICOLON, ";"); break;
        case '.':
            if (peek_char(l)=='.' && l->input[l->readPosition]=='.' ) {
                read_char(l); read_char(l);
                tok = new_token(TOKEN_ELLIPSIS, "...");
            } else {
                tok = new_token(TOKEN_DOT, ".");
            }
            break;
        case '@': tok = new_token(TOKEN_AT, "@"); break;
        /* ── strings ── */
        case '"': case '\'':
            tok.literal = read_string(l);
            tok.type    = TOKEN_STRING;
            tok.line    = tok_line; tok.col = tok_col;
            return tok;
        /* ── EOF ── */
        case 0:
            tok = new_token(TOKEN_EOF, "");
            tok.line = tok_line; tok.col = tok_col;
            return tok;
        default:
            if (is_letter(l->ch)) {
                tok.literal = read_identifier(l);
                tok.type    = lookup_ident(tok.literal);
                tok.line    = tok_line; tok.col = tok_col;
                return tok;
            } else if (isdigit((unsigned char)l->ch)) {
                tok.literal = read_number(l);
                tok.type    = (tok.literal[0]=='0' && (tok.literal[1]=='x'||tok.literal[1]=='X'||
                                tok.literal[1]=='b'||tok.literal[1]=='B'))
                              ? TOKEN_INT
                              : (strchr(tok.literal,'.')||strchr(tok.literal,'e')||
                                 strchr(tok.literal,'E'))
                                ? TOKEN_FLOAT : TOKEN_INT;
                tok.line = tok_line; tok.col = tok_col;
                return tok;
            } else {
                tok = new_token(TOKEN_ILLEGAL, "");
            }
            break;
    }

    read_char(l);
    tok.line = tok_line; tok.col = tok_col;
    return tok;
}
