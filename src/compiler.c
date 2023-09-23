#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mode;
} Parser;

typedef enum {
  PrecNone,
  PrecAssignment, // =
  PrecOr,         // or
  PrecAnd,        // and
  PrecEquality,   // == !=
  PrecComparison, // < > <= >=
  PrecTerm,       // + -
  PrecFactor,     // * /
  PrecUnary,      // ! -
  PrecCall,       // . (
  PrecPrimary
} Precedence;

/// function pointer
typedef void (*ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Chunk *compiling_chunk;

static Chunk *current_chunk() { return compiling_chunk; }

static void error_at(const Token *token, const char *message) {
  if (parser.panic_mode == true) {
    return;
  }
  parser.panic_mode = true;
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

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

static void emit_byte(uint8_t byte) {
  write_chunk(current_chunk(), byte, parser.previous.line);
}

static void emit_word(uint8_t byte1, uint8_t byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}

static void emit_return() { emit_byte(OpRet); }

static uint32_t emit_constant(const Value value) {
  return push_constant(current_chunk(), value, parser.previous.line);
}

static void emit_name(const Token name) {
  ObjString * string= copy_string(name.start, name.len);
  emit_constant(OBJ_VAL(string));
}

static void end_compiler() {
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
static void literal();
static void string();
static void variable();

static void declaration();
static void statement();
static void print_statement();

static ParseRule *get_rule(TokenType type);

ParseRule rules[] = {
    [TokenLeftParen] = {grouping, NULL, PrecNone},
    [TokenRightParen] = {NULL, NULL, PrecNone},
    [TokenLeftBrace] = {NULL, NULL, PrecNone},
    [TokenRightBrace] = {NULL, NULL, PrecNone},
    [TokenComma] = {NULL, NULL, PrecNone},
    [TokenDot] = {NULL, NULL, PrecNone},
    [TokenMinus] = {unary, binary, PrecTerm},
    [TokenPlus] = {NULL, binary, PrecTerm},
    [TokenSemiColon] = {NULL, NULL, PrecNone},
    [TokenSlash] = {NULL, binary, PrecFactor},
    [TokenStar] = {NULL, binary, PrecFactor},
    [TokenBang] = {unary, NULL, PrecNone},
    [TokenBangEqual] = {NULL, binary, PrecEquality},
    [TokenEqual] = {NULL, NULL, PrecNone},
    [TokenEqualEqual] = {NULL, binary, PrecEquality},
    [TokenGreater] = {NULL, binary, PrecComparison},
    [TokenGreaterEqual] = {NULL, binary, PrecComparison},
    [TokenLess] = {NULL, binary, PrecComparison},
    [TokenLessEqual] = {NULL, binary, PrecComparison},
    [TokenIdentifier] = {variable, NULL, PrecNone},
    [TokenString] = {string, NULL, PrecNone},
    [TokenNumber] = {number, NULL, PrecNone},
    [TokenAnd] = {NULL, NULL, PrecNone},
    [TokenClass] = {NULL, NULL, PrecNone},
    [TokenElse] = {NULL, NULL, PrecNone},
    [TokenFalse] = {literal, NULL, PrecNone},
    [TokenFor] = {NULL, NULL, PrecNone},
    [TokenFn] = {NULL, NULL, PrecNone},
    [TokenIf] = {NULL, NULL, PrecNone},
    [TokenLet] = {NULL, NULL, PrecNone},
    [TokenNull] = {literal, NULL, PrecNone},
    [TokenOr] = {NULL, NULL, PrecNone},
    [TokenPrint] = {NULL, NULL, PrecNone},
    [TokenReturn] = {NULL, NULL, PrecNone},
    [TokenSuper] = {NULL, NULL, PrecNone},
    [TokenSelf] = {NULL, NULL, PrecNone},
    [TokenTrue] = {literal, NULL, PrecNone},
    [TokenWhile] = {NULL, NULL, PrecNone},
    [TokenError] = {NULL, NULL, PrecNone},
    [TokenEof] = {NULL, NULL, PrecNone},
};

static ParseRule *get_rule(TokenType type) { return &rules[type]; }

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

static void define_variable(const Token name) {
  emit_byte(OpDefineGlobal);
  emit_name(name);
}

static void grouping() {
  expression();
  consume(TokenRightParen, "Expect ')' after expression.");
}

static void number() {
  double value = strtod(parser.previous.start, NULL);
  emit_constant(NUMBER_VAL(value));
}

static void string() {
  emit_constant(
      OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.len - 2)));
}

static void name_variable(Token name) {
  emit_byte(OpGetGlobal);
  emit_name(name);
}
static void variable() { name_variable(parser.previous); }

static void unary() {
  TokenType operator_type = parser.previous.type;

  parse_precedence(PrecUnary);

  switch (operator_type) {
  case TokenMinus: {
    emit_byte(OpNeg);
    break;
  }
  case TokenBang: {
    emit_byte(OpNot);
    break;
  }
  default:
    return;
  }
}

static void binary() {
  TokenType operator_type = parser.previous.type;
  ParseRule *rule = get_rule(operator_type);
  parse_precedence((Precedence)(rule->precedence + 1));

  switch (operator_type) {
  case TokenEqualEqual: {
    emit_byte(OpEq);
    break;
  }
  case TokenBangEqual: {
    emit_word(OpEq, OpNot);
    break;
  }
  case TokenLess: {
    emit_byte(OpLt);
    break;
  }
  case TokenLessEqual: {
    emit_word(OpGt, OpNot);
    break;
  }
  case TokenGreater: {
    emit_byte(OpGt);
    break;
  }
  case TokenGreaterEqual: {
    emit_word(OpLt, OpNot);
    break;
  }
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
  default:
    return;
  }
}

static void literal() {
  switch (parser.previous.type) {
  case TokenNull: {
    emit_byte(OpNull);
    break;
  }
  case TokenTrue: {
    emit_byte(OpTrue);
    break;
  }
  case TokenFalse: {
    emit_byte(OpFalse);
    break;
  }
  default:
    return;
  }
}

static void expression() { parse_precedence(PrecAssignment); }

static void var_declaration() {
  consume(TokenIdentifier, "Expect variable name.");
  const Token identifier = parser.previous;


  if (match(TokenEqual)) {
    expression();
  } else {
    emit_byte(OpNull);
  }

  consume(TokenSemiColon, "Expect ';' after variable declaration.");

  define_variable(identifier);
}

static void synchronize() {
  parser.panic_mode = false;

  while (parser.current.type != TokenEof) {
    if (parser.previous.type == TokenSemiColon) {
      return;
    }
    switch (parser.current.type) {
    case TokenClass:
    case TokenFn:
    case TokenLet:
    case TokenFor:
    case TokenIf:
    case TokenWhile:
    case TokenPrint:
    case TokenReturn:
      return;

    default:;
    }
    advance();
  }
}

static void declaration() {
  if (match(TokenLet)) {
    var_declaration();
  } else {
    statement();
  }

  if (parser.panic_mode) {
    synchronize();
  }
}

static void print_statement() {
  expression();
  consume(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPrint);
}

static void expression_statement() {
  expression();
  consume(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPop);
}

static void statement() {
  if (match(TokenPrint)) {
    print_statement();
  } else {
    expression_statement();
  }
}

bool compile(const char *source, Chunk *chunk) {
  init_scanner(source);
  compiling_chunk = chunk;

  parser.had_error = false;
  parser.panic_mode = false;

  advance();
  while (!match(TokenEof)) {
    declaration();
  }
  end_compiler();
  return !parser.had_error;
}
