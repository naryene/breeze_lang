#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

#include "chunk.h"
#include "memory.h"
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
  bool is_captured;
} Local;

typedef struct {
  uint32_t index;
  bool is_local;
} Upvalue;

typedef enum { TypeFunction, TypeScript } FunctionType;

typedef struct Compiler {
  struct Compiler *enclosing;
  ObjFunction *function;
  FunctionType function_type;
  Local locals[UINT8_COUNT];
  uint32_t locals_len;
  Upvalue upvalues[UINT8_COUNT];
  int32_t scope_depth;
} Compiler;

Parser parser;
Compiler *current_compiler = NULL;

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

static void error(const char *format, ...) {
  char message[256]; // Adjust size as necessary
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  error_at(&parser.previous, message);
}

static void error_at_current(const char *format, ...) {
  char message[256]; // Adjust size as necessary
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
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

static void consume_token(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  error_at_current(message);
}

static bool check_token(TokenType type) { return parser.current.type == type; }

static bool match_token(TokenType type) {
  if (!check_token(type)) {
    return false;
  }
  advance();
  return true;
}

static bool max_constants_error(const uint32_t idx) {
  if (idx > UINT16_MAX) {
    error("Too many constants in one chunk.");
    return true;
  }
  return false;
}

static void emit_byte(uint8_t byte) {
  write_chunk(current_chunk(), byte, parser.previous.line);
}

static void emit_word(const uint8_t byte1, const uint8_t byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}

static void emit_return() { emit_word(OpNull, OpRet); }

/*
 * Emits a constant into constant array
 */
static uint32_t emit_constant_array(const Value value) {
  uint32_t idx = add_constant(current_chunk(), value);
  if (max_constants_error(idx)) {
    exit(1);
  }
  return idx;
}

/*
 * Emits a constant as an index into chunk
 */
static void emit_idx(const uint32_t idx) {
  if (max_constants_error(idx)) {
    return;
  }
  write_constant_chunk(current_chunk(), idx, parser.previous.line);
}

/*
 * Emits a constant into chunk and  constants array
 */
static void emit_constant(const Value value) {
  uint32_t idx = emit_constant_array(value);
  emit_idx(idx);
  return;
}

static void emit_byte_idx(const uint8_t op, const uint32_t idx) {
  emit_byte(op);
  emit_idx(idx);
}

static uint32_t emit_name(const Token *name) {
  for (uint32_t idx = 0; idx < current_chunk()->constants.len; idx += 1) {
    Value *value = &current_chunk()->constants.values[idx];
    if (IS_STRING(*value) && AS_STRING(*value)->len == name->len &&
        memcmp(name->start, AS_STRING(*value)->chars, name->len) == 0) {
      return idx;
    }
  }
  ObjString *string = copy_string(name->start, name->len);
  return emit_constant_array(OBJ_VAL(string));
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
  current_chunk()->code[offset] = (jmp) & 0xff;
  current_chunk()->code[offset + 1] = (jmp >> 8) & 0xff;
}

static void parse_precedence(Precedence precedence);
static void expression();

static void grouping(bool can_assign);
static void number(bool can_assign);
static void unary(bool can_assign);
static void binary(bool can_assign);
static void call(bool can_assign);
static void literal(bool can_assign);
static void string(bool can_assign);
static void variable(bool can_assign);
static void and_and_(bool can_assign);
static void or_or_(bool can_assign);
static void dot(bool can_assign);

static void var_declaration();
static void class_declaration();
static void fn_declaration();
static void declaration();
static void block();

static void print_statement();
static void return_statement();
static void if_statement();
static void while_statement();
static void for_statement();
static void expression_statement();
static void statement();

static ParseRule *get_rule(TokenType type);

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

static int32_t add_upvalue(Compiler *compiler, const size_t index,
                           bool is_local) {
  uint32_t upvalues_len = compiler->function->upvalues_len;

  for (uint32_t i = 0; i < upvalues_len; i += 1) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->is_local == is_local) {
      return i;
    }
  }

  if (upvalues_len == UINT16_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalues_len].is_local = is_local;
  compiler->upvalues[upvalues_len].index = index;
  compiler->function->upvalues_len += 1;
  return compiler->function->upvalues_len - 1;
}

static int32_t resolve_upvalue(Compiler *compiler, const Token *name) {

  if (compiler->enclosing == NULL) {
    return -1;
  }

  int32_t local_idx = resolve_local(compiler->enclosing, name);
  if (local_idx != -1) {
    compiler->enclosing->locals[local_idx].is_captured = false;
    return add_upvalue(compiler, local_idx, true);
  }

  int32_t upvalue = resolve_upvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return add_upvalue(compiler, upvalue, false);
  }

  return -1;
}

static void named_variable(const Token *name, bool can_assign) {
  uint8_t get_op, set_op;
  int32_t arg = resolve_local(current_compiler, name);
  if (arg != -1) {
    get_op = OpGetLocal;
    set_op = OpSetLocal;
  } else if ((arg = resolve_upvalue(current_compiler, name)) != -1) {
    get_op = OpGetUpvalue;
    set_op = OpSetUpvalue;
  } else {
    arg = emit_name(name);
    get_op = OpGetGlobal;
    set_op = OpSetGlobal;
  }

  if (can_assign && match_token(TokenEqual)) {
    expression();
    emit_byte(set_op);
  } else {
    emit_byte(get_op);
  }
  emit_idx(arg);
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
  local->is_captured = false;
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
  consume_token(TokenIdentifier, message);

  declare_variable();
  if (current_compiler->scope_depth > 0) {
    return 0;
  }
  return emit_name(&parser.previous);
}

static void init_variable() {
  if (current_compiler->scope_depth == 0) {
    return;
  }
  current_compiler->locals[current_compiler->locals_len - 1].depth =
      current_compiler->scope_depth;
}

static void define_variable(uint32_t variable) {
  if (current_compiler->scope_depth > 0) {
    init_variable();
    return;
  }
  emit_byte(OpDefineGlobal);
  emit_idx(variable);
}

static uint8_t argument_list() {
  uint8_t args_len = 0;
  if (!check_token(TokenRightParen)) {
    while (true) {
      expression();
      if (args_len == UINT8_MAX) {
        error("Can't have more than %d arguments.", UINT8_MAX);
      }
      args_len += 1;
      if (!match_token(TokenComma)) {
        break;
      }
    }
  }
  consume_token(TokenRightParen, "Expect ')' after arguments.");
  return args_len;
}

static void begin_scope() { current_compiler->scope_depth += 1; }

static void end_scope() {
  current_compiler->scope_depth -= 1;
  while (current_compiler->locals_len > 0 &&
         current_compiler->locals[current_compiler->locals_len - 1].depth >
             current_compiler->scope_depth) {
    if (current_compiler->locals[current_compiler->locals_len - 1]
            .is_captured) {
      emit_byte(OpCloseUpvalue);
    } else {
      emit_byte(OpPop);
    }
    current_compiler->locals_len -= 1;
  }
}

static void scoped_block() {
  begin_scope();
  block();
  end_scope();
}

static void init_compiler(Compiler *compiler,
                          const FunctionType function_type) {
  compiler->enclosing = current_compiler;

  compiler->function_type = function_type;

  compiler->locals_len = 0;
  compiler->scope_depth = 0;

  compiler->function = new_function();

  current_compiler = compiler;

  if (function_type != TypeScript) {
    current_compiler->function->name =
        copy_string(parser.previous.start, parser.previous.len);
  }

  Local *local = &current_compiler->locals[current_compiler->locals_len];
  current_compiler->locals_len += 1;
  local->depth = 0;

  local->is_captured = false;

  if (function_type != TypeFunction) {
    local->name.start = "this";
    local->name.len = 4;
  } else {
    local->name.start = "";
    local->name.len = 0;
  }
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

  current_compiler = current_compiler->enclosing;
  return function;
}

static void function(const FunctionType function_type) {
  Compiler compiler;
  init_compiler(&compiler, function_type);
  begin_scope();

  consume_token(TokenLeftParen, "Expect '(' after function name.");
  if (!check_token(TokenRightParen)) {
    while (true) {
      compiler.function->arity += 1;
      if (compiler.function->arity > UINT8_MAX) {
        error_at_current("Can't have more than %d parameters.", UINT8_MAX);
      }
      uint32_t param = parse_variable("Expect parameter name.");
      define_variable(param);
      if (!match_token(TokenComma)) {
        break;
      }
    }
  }

  consume_token(TokenRightParen, "Expect ')' after parameters.");
  consume_token(TokenLeftBrace, "Expect '{' before function body.");
  block();

  ObjFunction *func = end_compiler();
  uint32_t idx = emit_constant_array(OBJ_VAL(func));
  emit_byte(OpClosure);
  emit_idx(idx);

  for (uint32_t i = 0; i < func->upvalues_len; i += 1) {
    emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
    emit_idx(compiler.upvalues[i].index);
  }
}

static void method() {
  consume_token(TokenFn, "Expect method 'fn' declaration.");
  consume_token(TokenIdentifier, "Expect method name.");
  uint32_t method_name_idx = emit_name(&parser.previous);
  FunctionType function_type = TypeFunction;
  function(function_type);
  emit_byte_idx(OpMethod, method_name_idx);
}

static void property_declaration() {
  consume_token(TokenIdentifier, "Expect property name.");
  uint32_t name_idx = emit_name(&parser.previous);
  // if (match_token(TokenEqual)) {
  //   expression();
  // } else {
  //   emit_byte(OpNull);
  // }
  consume_token(TokenSemiColon, "Expect ';' after property definition.");
  emit_byte_idx(OpDefineProperty, name_idx);
}

ParseRule rules[] = {
    [TokenLeftParen] = {grouping, call, PrecCall},
    [TokenRightParen] = {NULL, NULL, PrecNone},
    [TokenLeftBrace] = {NULL, NULL, PrecNone},
    [TokenRightBrace] = {NULL, NULL, PrecNone},
    [TokenComma] = {NULL, NULL, PrecNone},
    [TokenDot] = {NULL, dot, PrecCall},
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
  ParseFn handle_prefix = get_rule(parser.previous.type)->prefix;
  if (handle_prefix == NULL) {
    error("Expect expression.");
    return;
  }

  bool can_assign = precedence <= PrecAssignment;
  handle_prefix(can_assign);

  while (precedence <= get_rule(parser.current.type)->precedence) {
    advance();
    ParseFn handle_infix = get_rule(parser.previous.type)->infix;
    handle_infix(can_assign);
  }

  if (can_assign && match_token(TokenEqual)) {
    error("Invalid assignment target.");
  }
}

static void grouping(bool can_assign) {
  expression();
  consume_token(TokenRightParen, "Expect ')' after expression.");
}

static void number(bool can_assign) {
  double value = strtod(parser.previous.start, NULL);
  emit_constant(NUMBER_VAL(value));
}

static void string(bool can_assign) {
  emit_constant(
      OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.len - 2)));
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

static void dot(bool can_assign) {
  consume_token(TokenIdentifier, "Expect property name after '.'.");
  uint32_t name_idx = emit_name(&parser.previous);

  if (can_assign && match_token(TokenEqual)) {
    expression();
    emit_byte_idx(OpSetProperty, name_idx);
  } else {
    emit_byte_idx(OpGetProperty, name_idx);
  }
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

static void call(bool can_assign) {
  uint8_t args_len = argument_list();
  emit_word(OpCall, args_len);
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

static void print_statement() {
  expression();
  consume_token(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPrint);
}

static void return_statement() {
  if (current_compiler->function_type == TypeScript) {
    error("Can't return from top-level code.");
  }
  if (match_token(TokenSemiColon)) {
    emit_return();
  } else {
    expression();
    consume_token(TokenSemiColon, "Expect ';' after return value.");
    emit_byte(OpRet);
  }
}

static void if_statement() {
  expression();

  uint32_t then_jmp = emit_jmp(OpJmpIfFalse);
  emit_byte(OpPop);
  consume_token(TokenLeftBrace, "Expect '{' after 'if' statement.");
  scoped_block();

  uint32_t else_jmp = emit_jmp(OpJmp);

  patch_jmp(then_jmp);
  emit_byte(OpPop);

  if (match_token(TokenElse)) {
    consume_token(TokenLeftBrace, "Expect '{' after 'else' statement.");
    scoped_block();
  }

  patch_jmp(else_jmp);
}

static void while_statement() {
  uint32_t loop_start = current_chunk()->len;
  expression();

  uint32_t exit_jmp = emit_jmp(OpJmpIfFalse);
  emit_byte(OpPop);
  consume_token(TokenLeftBrace, "Expect '{' after 'while' statement.");
  scoped_block();
  emit_loop(loop_start);

  patch_jmp(exit_jmp);
  emit_byte(OpPop);
}

static void for_statement() {
  begin_scope();
  consume_token(TokenLeftParen, "Expect '(' after 'for'.");
  if (match_token(TokenSemiColon)) {
  } else if (match_token(TokenLet)) {
    var_declaration();
  } else {
    expression_statement();
  }

  uint32_t loop_start = current_chunk()->len;
  int32_t exit_jmp = -1;
  if (!match_token(TokenSemiColon)) {
    expression();
    consume_token(TokenSemiColon, "Expect ';' after loop condition.");

    exit_jmp = emit_jmp(OpJmpIfFalse);
    emit_byte(OpPop);
  }

  if (!match_token(TokenRightParen)) {
    uint32_t body_jmp = emit_jmp(OpJmp);
    uint32_t increment_start = current_chunk()->len;
    expression();
    emit_byte(OpPop);
    consume_token(TokenRightParen, "Expect ')' after 'for' clauses.");

    emit_loop(loop_start);
    loop_start = increment_start;
    patch_jmp(body_jmp);
  }

  consume_token(TokenLeftBrace, "Expect '{' after 'for' statement.");
  block();
  emit_loop(loop_start);

  if (exit_jmp != -1) {
    patch_jmp(exit_jmp);
    emit_byte(OpPop);
  }

  end_scope();
}

static void expression_statement() {
  expression();
  consume_token(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPop);
}

static void statement() {
  if (match_token(TokenPrint)) {
    print_statement();
  } else if (match_token(TokenIf)) {
    if_statement();
  } else if (match_token(TokenReturn)) {
    return_statement();
  } else if (match_token(TokenWhile)) {
    while_statement();
  } else if (match_token(TokenFor)) {
    for_statement();
  } else if (match_token(TokenLeftBrace)) {
    scoped_block();
  } else {
    expression_statement();
  }
}

static void block() {
  while (!check_token(TokenRightBrace) && !check_token(TokenEof)) {
    declaration();
  }
  consume_token(TokenRightBrace, "Expect '}' after block.");
}

static void class_declaration() {
  consume_token(TokenIdentifier, "Expect class name");
  Token class_name = parser.previous;
  uint32_t class_name_idx = emit_name(&parser.previous);
  declare_variable();

  emit_byte_idx(OpClass, class_name_idx);
  define_variable(class_name_idx);

  named_variable(&class_name, false);
  consume_token(TokenLeftBrace, "Expect '{' before class body.");
  while (!check_token(TokenRightBrace) && !check_token(TokenEof) &&
         match_token(TokenLet)) {
    property_declaration();
  }
  while (!check_token(TokenRightBrace) && !check_token(TokenEof)) {
    method();
  }
  consume_token(TokenRightBrace, "Expect '}' after class body.");
}

static void fn_declaration() {
  uint32_t variable = parse_variable("Expect function name.");
  init_variable();
  function(TypeFunction);
  define_variable(variable);
}

static void var_declaration() {
  uint32_t variable = parse_variable("Expect variable name.");

  if (match_token(TokenEqual)) {
    expression();
  } else {
    emit_byte(OpNull);
  }

  consume_token(TokenSemiColon, "Expect ';' after variable declaration.");

  define_variable(variable);
}

static void declaration() {
  if (match_token(TokenClass)) {
    class_declaration();
  } else if (match_token(TokenFn)) {
    fn_declaration();
  } else if (match_token(TokenLet)) {
    var_declaration();
  } else {
    statement();
  }

  if (parser.panic_mode) {
    synchronize();
  }
}

ObjFunction *compile(const char *source) {
  init_scanner(source);
  Compiler compiler;
  init_compiler(&compiler, TypeScript);

  parser.had_error = false;
  parser.panic_mode = false;

  advance();
  while (!match_token(TokenEof)) {
    declaration();
  }
  if (parser.had_error) {
    return NULL;
  }
  ObjFunction *function = end_compiler();

  return function;
}

void mark_compiler_roots() {
  Compiler *compiler = current_compiler;
  while (compiler != NULL) {
    mark_object((Obj *)compiler->function);
    compiler = compiler->enclosing;
  }
}
