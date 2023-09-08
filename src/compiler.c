#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE 
#include "debug.h"
#endif


typedef struct {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mod;
} Parser;

typedef enum {
  PrecNone,
  PrecAssignment, // =
  PrecOr, // or
  PrecAnd, // and
  PrecEquality, // == !=
  PrecComparison, // < > <= >=
  PrecTerm, // + -
  PrecFactor, // * /
  PrecUnary, // ! -
  PrecCall, // . (
  PrecPrimary 
} Precedence;

typedef void (* ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Chunk *compiling_chunk;

static Chunk* current_chunk(){
  return compiling_chunk;
}

static void error_at(const Token *token, const char *message) {
  if (parser.panic_mod == true) {
    return;
  }
  parser.panic_mod = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TokenEof) {
    fprintf(stderr, " at end");
  } else if (token->type == TokenError) {

  } else {
    fprintf(stderr, " at '%.*s'", token->len, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.had_error = true;
}
static void error(const char *message) { error_at(&parser.previous, message); }
static void error_at_current(const char *message) {
  error_at(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  while (true) {
    parser.current = scan_token();
    if (parser.current.type != TokenError) {
      break;
    }
    error(parser.current.start);
  }
}

static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  error_at_current(message);
}

// TODO:

static void emit_byte(uint8_t byte) {
  write_chunk(current_chunk(), byte, parser.previous.line);
}

static void emit_word(uint8_t byte1, uint8_t byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}

static void emit_return() {
  emit_byte(OpRet);
}

static void emit_constant(const double value) {
  push_constant(current_chunk(), (Value) value, parser.previous.line);
}

static void end_compiler(){
  emit_return();

  #ifdef DEBUG_PRINT_CODE
    if (parser.had_error == false) {
    disassemble_chunk(current_chunk(), "code");
  }
  #endif /* ifdef DEBUG_PRINT_CODE */ 
  
}

static void parse_precedence(Precedence precedence); 
static void expression(); 
static void grouping();
static void number(); 
static void unary();
static void binary();
static ParseRule* get_rule(TokenType type);

ParseRule rules[] = {
  [TokenLeftParen]    = {grouping,  NULL,   PrecNone},
  [TokenRightParen]   = {NULL,      NULL,   PrecNone},
  [TokenLeftBrace]    = {NULL,      NULL,   PrecNone},
  [TokenRightBrace]   = {NULL,      NULL,   PrecNone},
  [TokenComma]        = {NULL,      NULL,   PrecNone},
  [TokenDot]          = {NULL,      NULL,   PrecNone},
  [TokenMinus]        = {unary,     binary, PrecTerm},
  [TokenPlus]         = {NULL,      binary, PrecTerm},
  [TokenSemiColon]    = {NULL,      NULL,   PrecNone},
  [TokenSlash]        = {NULL,      binary, PrecFactor},
  [TokenStar]         = {NULL,      binary, PrecFactor},
  [TokenBang]         = {NULL,      NULL,   PrecNone},
  [TokenBangEqual]    = {NULL,      NULL,   PrecNone},
  [TokenEqual]        = {NULL,      NULL,   PrecNone},
  [TokenEqualEqual]   = {NULL,      NULL,   PrecNone},
  [TokenGreater]      = {NULL,      NULL,   PrecNone},
  [TokenGreaterEqual] = {NULL,      NULL,   PrecNone},
  [TokenLess]         = {NULL,      NULL,   PrecNone},
  [TokenLessEqual]    = {NULL,      NULL,   PrecNone},
  [TokenIdentifier]   = {NULL,      NULL,   PrecNone},
  [TokenString]       = {NULL,      NULL,   PrecNone},
  [TokenNumber]       = {number,    NULL,   PrecNone},
  [TokenAnd]          = {NULL,      NULL,   PrecNone},
  [TokenClass]        = {NULL,      NULL,   PrecNone},
  [TokenElse]         = {NULL,      NULL,   PrecNone},
  [TokenFalse]        = {NULL,      NULL,   PrecNone},
  [TokenFor]          = {NULL,      NULL,   PrecNone},
  [TokenFn]           = {NULL,      NULL,   PrecNone},
  [TokenIf]           = {NULL,      NULL,   PrecNone},
  [TokenLet]          = {NULL,      NULL,   PrecNone},
  [TokenNull]         = {NULL,      NULL,   PrecNone},
  [TokenOr]           = {NULL,      NULL,   PrecNone},
  [TokenPrint]        = {NULL,      NULL,   PrecNone},
  [TokenReturn]       = {NULL,      NULL,   PrecNone},
  [TokenSuper]        = {NULL,      NULL,   PrecNone},
  [TokenSelf]         = {NULL,      NULL,   PrecNone},
  [TokenTrue]         = {NULL,      NULL,   PrecNone},
  [TokenWhile]        = {NULL,      NULL,   PrecNone},
  [TokenError]        = {NULL,      NULL,   PrecNone},
  [TokenEof]          = {NULL,      NULL,   PrecNone},
};

static ParseRule* get_rule(TokenType type) {
  return &rules[type];
}

static void parse_precedence(Precedence precedence) {
  advance();
  ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
  if (prefix_rule == NULL) {
    error("Expect expression.");
    return;
  }

  prefix_rule();
  while (precedence <= get_rule(parser.current.type)->precedence) {
    advance();
    ParseFn infix_rule = get_rule(parser.previous.type)->infix;
    infix_rule();
  }
}


static void expression() {
  parse_precedence(PrecAssignment);
}


static void grouping() {
  expression();
  consume(TokenRightParen, "Expect ')' after expression.");
}

static void number() {
  double value = strtod(parser.previous.start, NULL);
  emit_constant(value);
}

static void unary() {
  TokenType operator_type = parser.previous.type;

  parse_precedence(PrecUnary);

  switch (operator_type) {
    case TokenMinus: {
      emit_byte(OpNeg);
      break;
    }
    default: return;
  }
}

static void binary() {
  TokenType operator_type = parser.previous.type;
  ParseRule *rule = get_rule(operator_type);
  parse_precedence((Precedence) (rule->precedence + 1));

  switch (operator_type) {
    case TokenPlus: {
      emit_byte(OpAdd);
      break;
    } 
    case TokenMinus: {
      emit_byte(OpSub);
      break;
    }
    case TokenStar: {
      emit_byte(OpMul);
      break;
    }
    case TokenSlash: {
      emit_byte(OpDiv);
      break;
    }
    default: return;
  }
}




bool compile(const char *source, Chunk *chunk) {
  init_scanner(source);
  compiling_chunk = chunk;

  parser.had_error = false;
  parser.panic_mod = false;

  advance();
  expression();
  consume(TokenEof, "Expect end of expression.");
  end_compiler();
  return !parser.had_error;
}
