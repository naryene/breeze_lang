#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define UINT16_COUNT (UINT16_MAX + 1)

typedef struct {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mode;
} Parser;

typedef enum {
  PrecNone,
  PrecAssignment, // =
  PrecOrOr,       // ||
  PrecAndAnd,     // &&
  PrecEquality,   // == !=
  PrecComparison, // < > <= >=
  PrecTerm,       // + -
  PrecFactor,     // * /
  PrecUnary,      // ! -
  PrecCall,       // . (
  PrecPrimary
} Precedence;

/// function pointer
typedef void (*ParseFn)(bool can_assign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int32_t depth;
} Local;

typedef enum { TypeFunction, TypeScript } FunctionType;

typedef struct {
  ObjFunction *function;
  FunctionType function_type;
  Local locals[UINT16_COUNT];
  uint32_t locals_len;
  uint32_t scope_depth;
} Compiler;

Parser parser;
Compiler *current_compiler = NULL;
Chunk *compiling_chunk;

static Chunk *current_chunk() { return &current_compiler->function->chunk; }

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

static bool max_constants_error(const uint32_t idx) {
  if (idx > UINT16_MAX) {
    error("Too many constants in one chunk.");
    return true;
  }
  return false;
}

// Emits a constant into chunk and  constants array
static uint32_t emit_constant(const Value value) {
  uint32_t idx = push_constant(current_chunk(), value, parser.previous.line);
  if (max_constants_error(idx)) {
    return 0;
  }
  return idx;
}

// Emits a constant into constant array
static uint32_t emit_constant_(const Value constant) {
  uint32_t idx = add_constant(current_chunk(), constant);
  if (max_constants_error(idx)) {
    return 0;
  }
  return idx;
}

// Emits a constant into chunk
static void emit_constant_idx(const uint32_t idx) {
  if (max_constants_error(idx)) {
    return;
  }
  write_constant_chunk(current_chunk(), idx, parser.previous.line);
}

static uint32_t emit_name(const Token *name) {
  ObjString *string = copy_string(name->start, name->len);
  return emit_constant_(OBJ_VAL(string));
}

static uint32_t emit_jmp(uint8_t inst) {
  emit_byte(inst);
  emit_word(0xff, 0xff);
  return current_chunk()->len - 2;
}

static void emit_loop(uint32_t loop_start) {
  emit_byte(OpJmp);
  uint32_t offset = current_chunk()->len - loop_start + 2;
  if (offset > UINT16_MAX) {
    error("Loop body is too large.");
  }
  emit_word(loop_start & 0xff, (loop_start >> 8) & 0xff);
}

static void patch_jmp(int32_t offset) {
  int32_t jmp = current_chunk()->len;
  if ((jmp - offset - 2) > UINT16_MAX) {
    error("Too much code to jump over.");
  }
  current_chunk()->code[offset] = (jmp)&0xff;
  current_chunk()->code[offset + 1] = (jmp >> 8) & 0xff;
}

static bool same_name(const Token *name, const Token *other) {
  if (name->len != other->len) {
    return false;
  }
  return memcmp(name->start, other->start, name->len) == 0;
}

static int32_t resolve_local(Compiler *compiler, const Token *name) {
  for (int32_t i = compiler->locals_len - 1; i >= 0; i -= 1) {
    Local *local = &compiler->locals[i];
    if (same_name(name, &local->name)) {
      if (local->depth == -1) {
        error("Cannot read local variable in its own initializer.");
      }
      return i;
    }
  }
  return -1;
}

static void add_local(const Token *name) {
  // My version is able to contain more local
  // variables, but i'm trying to do same as clox
  // for now
  if (current_compiler->locals_len == UINT16_COUNT) {
    error("Too many local variabls in function.");
    return;
  }
  Local *local = &current_compiler->locals[current_compiler->locals_len];
  current_compiler->locals_len += 1;
  local->name = *name;
  local->depth = -1;
}

static void declare_variable() {
  if (current_compiler->scope_depth == 0) {
    return;
  }

  Token *name = &parser.previous;
  for (int32_t i = current_compiler->locals_len - 1; i >= 0; i -= 1) {
    Local *local = &current_compiler->locals[i];
    if (local->depth != -1 && local->depth < current_compiler->scope_depth) {
      break;
    }
    if (same_name(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }
  add_local(name);
}

static uint32_t parse_variable(const char *message) {
  consume(TokenIdentifier, message);

  declare_variable();
  if (current_compiler->scope_depth > 0) {
    return 0;
  }
  return emit_name(&parser.previous);
}

static void parse_precedence(Precedence precedence);
static void expression();

static void grouping(bool can_assign);
static void number(bool can_assign);
static void unary(bool can_assign);
static void binary(bool can_assign);
static void literal(bool can_assign);
static void string(bool can_assign);
static void variable(bool can_assign);
static void and_and_(bool can_assign);
static void or_or_(bool can_assign);

static void declaration();
static void block();
static void statement();
static void print_statement();
static void if_statement();
static void while_statement();
static void for_statement();

static ParseRule *get_rule(TokenType type);

static void begin_scope() { current_compiler->scope_depth += 1; }

static void end_scope() {
  current_compiler->scope_depth -= 1;
  while (current_compiler->locals_len > 0 &&
         current_compiler->locals[current_compiler->locals_len - 1].depth >
             current_compiler->scope_depth) {
    emit_byte(OpPop);
    current_compiler->locals_len -= 1;
  }
}

static void scoped_block() {
  begin_scope();
  block();
  end_scope();
}

static void init_compiler(Compiler *compiler, FunctionType function_type) {
  compiler->function = NULL;
  compiler->function_type = function_type;
  compiler->locals_len = 0;
  compiler->scope_depth = 0;
  compiler->function = new_function();
  current_compiler = compiler;

  Local *local = &current_compiler->locals[current_compiler->locals_len];
  current_compiler->locals_len += 1;
  local->depth = 0;
  local->name.start = "";
  local->name.len = 0;
}

static ObjFunction *end_compiler() {
  emit_return();
  ObjFunction *function = current_compiler->function;

#ifdef DEBUG_PRINT_CODE
  if (parser.had_error == false) {
    disassemble_chunk(current_chunk(),
                      function->name != NULL ? function->name->chars : "code");
  }
#endif /* ifdef DEBUG_PRINT_CODE */

  return function;
}

ObjFunction *compile(const char *source) {
  init_scanner(source);
  Compiler compiler;
  init_compiler(&compiler, TypeScript);

  parser.had_error = false;
  parser.panic_mode = false;

  advance();
  while (!match(TokenEof)) {
    declaration();
  }
  if (parser.had_error) {
    return NULL;
  }
  ObjFunction * function = end_compiler();
  return function;
}

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
    [TokenAndAnd] = {NULL, and_and_, PrecAndAnd},
    [TokenClass] = {NULL, NULL, PrecNone},
    [TokenElse] = {NULL, NULL, PrecNone},
    [TokenFalse] = {literal, NULL, PrecNone},
    [TokenFor] = {NULL, NULL, PrecNone},
    [TokenFn] = {NULL, NULL, PrecNone},
    [TokenIf] = {NULL, NULL, PrecNone},
    [TokenLet] = {NULL, NULL, PrecNone},
    [TokenNull] = {literal, NULL, PrecNone},
    [TokenOrOr] = {NULL, or_or_, PrecOrOr},
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

  bool can_assign = precedence <= PrecAssignment;
  prefix_rule(can_assign);

  while (precedence <= get_rule(parser.current.type)->precedence) {
    advance();
    ParseFn infix_rule = get_rule(parser.previous.type)->infix;
    infix_rule(can_assign);
  }

  if (can_assign && match(TokenEqual)) {
    error("Invalid assignment target.");
  }
}

static void init_variable() {
  current_compiler->locals[current_compiler->locals_len - 1].depth =
      current_compiler->scope_depth;
}

static void define_variable(uint32_t global) {
  if (current_compiler->scope_depth > 0) {
    init_variable();
    return;
  }
  emit_byte(OpDefineGlobal);
  emit_constant_idx(global);
}

static void grouping(bool can_assign) {
  expression();
  consume(TokenRightParen, "Expect ')' after expression.");
}

static void number(bool can_assign) {
  double value = strtod(parser.previous.start, NULL);
  emit_constant(NUMBER_VAL(value));
}

static void string(bool can_assign) {
  emit_constant(
      OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.len - 2)));
}

static void named_variable(const Token *name, bool can_assign) {
  uint8_t get_op, set_op;
  int32_t arg = resolve_local(current_compiler, name);
  if (arg != -1) {
    get_op = OpGetLocal;
    set_op = OpSetLocal;
  } else {
    arg = emit_name(name);
    get_op = OpGetGlobal;
    set_op = OpSetGlobal;
  }

  if (can_assign && match(TokenEqual)) {
    expression();
    emit_byte(set_op);
  } else {
    emit_byte(get_op);
  }
  emit_constant_idx(arg);
}

static void variable(bool can_assign) {
  named_variable(&parser.previous, can_assign);
}

static void unary(bool can_assign) {
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

static void and_and_(bool can_assign) {
  int32_t end_jmp = emit_jmp(OpJmpIfFalse);

  emit_jmp(OpPop);
  parse_precedence(PrecAndAnd);

  patch_jmp(end_jmp);
}

static void or_or_(bool can_assign) {
  int32_t else_jmp = emit_jmp(OpJmpIfFalse);
  int32_t end_jmp = emit_jmp(OpJmp);

  patch_jmp(else_jmp);
  emit_byte(OpPop);

  parse_precedence(PrecOrOr);
  patch_jmp(end_jmp);
}

static void binary(bool can_assign) {
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

static void literal(bool can_assign) {
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

static void expression() { parse_precedence(PrecAssignment); }

static void var_declaration() {
  uint32_t variable = parse_variable("Expect variable name.");

  if (match(TokenEqual)) {
    expression();
  } else {
    emit_byte(OpNull);
  }

  consume(TokenSemiColon, "Expect ';' after variable declaration.");

  define_variable(variable);
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

static void block() {
  while (!check(TokenRightBrace) && !check(TokenEof)) {
    declaration();
  }
  consume(TokenRightBrace, "Expect '}' after block.");
}

static void if_statement() {
  expression();

  uint32_t then_jmp = emit_jmp(OpJmpIfFalse);
  emit_byte(OpPop);
  consume(TokenLeftBrace, "Expect '{' after 'if' statement.");
  scoped_block();

  uint32_t else_jmp = emit_jmp(OpJmp);

  patch_jmp(then_jmp);
  emit_byte(OpPop);

  if (match(TokenElse)) {
    consume(TokenLeftBrace, "Expect '{' after 'else' statement.");
    scoped_block();
  }

  patch_jmp(else_jmp);
}

static void while_statement() {
  uint32_t loop_start = current_chunk()->len;
  expression();

  uint32_t exit_jmp = emit_jmp(OpJmpIfFalse);
  emit_byte(OpPop);
  consume(TokenLeftBrace, "Expect '{' after 'while' statement.");
  scoped_block();
  emit_loop(loop_start);

  patch_jmp(exit_jmp);
  emit_byte(OpPop);
}

static void for_statement() {
  begin_scope();
  consume(TokenLeftParen, "Expect '(' after 'for'.");
  if (match(TokenSemiColon)) {
  } else if (match(TokenLet)) {
    var_declaration();
  } else {
    expression_statement();
  }

  uint32_t loop_start = current_chunk()->len;
  int32_t exit_jmp = -1;
  if (!match(TokenSemiColon)) {
    expression();
    consume(TokenSemiColon, "Expect ';' after loop condition.");

    exit_jmp = emit_jmp(OpJmpIfFalse);
    emit_byte(OpPop);
  }

  if (!match(TokenRightParen)) {
    uint32_t body_jmp = emit_jmp(OpJmp);
    uint32_t increment_start = current_chunk()->len;
    expression();
    emit_byte(OpPop);
    consume(TokenRightParen, "Expect ')' after 'for' clauses.");

    emit_loop(loop_start);
    loop_start = increment_start;
    patch_jmp(body_jmp);
  }

  consume(TokenLeftBrace, "Expect '{' after 'for' statement.");
  block();
  emit_loop(loop_start);

  if (exit_jmp != -1) {
    patch_jmp(exit_jmp);
    emit_byte(OpPop);
  }

  end_scope();
}

static void statement() {
  if (match(TokenPrint)) {
    print_statement();
  } else if (match(TokenIf)) {
    if_statement();
  } else if (match(TokenWhile)) {
    while_statement();
  } else if (match(TokenFor)) {
    for_statement();
  } else if (match(TokenLeftBrace)) {
    scoped_block();
  } else {
    expression_statement();
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
