#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

#include "chunk.h"
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
  uint32_t scope_depth;
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

static void emit_word(const uint8_t byte1, const uint8_t byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}

static void emit_return() { emit_word(OpNull, OpRet); }

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
  for (uint32_t idx = 0; idx < current_chunk()->constants.len; idx += 1) {
    Value *value = &current_chunk()->constants.values[idx];
    if (IS_STRING(*value) && AS_STRING(*value)->len == name->len &&
        memcmp(name->start, AS_STRING(*value)->chars, name->len) == 0) {
      return idx;
    }
  }
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

static void var_declaration();
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

static int32_t add_upvalue(Compiler *compiler, const int32_t index,
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

  if (can_assign && match(TokenEqual)) {
    expression();
    emit_byte(set_op);
  } else {
    emit_byte(get_op);
  }
  emit_constant_idx(arg);
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
  consume(TokenIdentifier, message);

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
  emit_constant_idx(variable);
}

static uint8_t argument_list() {
  uint8_t args_len = 0;
  if (!check(TokenRightParen)) {
    while (true) {
      expression();
      if (args_len == UINT8_MAX) {
        error("Can't have more than %d arguments.", UINT8_MAX);
      }
      args_len += 1;
      if (!match(TokenComma)) {
        break;
      }
    }
  }
  consume(TokenRightParen, "Expect ')' after arguments.");
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

  compiler->function = NULL;
  compiler->function_type = function_type;

  compiler->locals_len = 0;
  compiler->scope_depth = 0;

  compiler->function = new_function();

  current_compiler = compiler;

  if (function_type != TypeScript) {
    current_compiler->function->name =
        copy_string(parser.previous.start, parser.previous.len);
  }

  current_compiler->locals_len += 1;
  Local *local = &current_compiler->locals[current_compiler->locals_len - 1];
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

  consume(TokenLeftParen, "Expect '(' after function name.");
  if (!check(TokenRightParen)) {
    while (true) {
      current_compiler->function->arity += 1;
      if (current_compiler->function->arity > UINT8_MAX) {
        error_at_current("Can't have more than %d parameters.", UINT8_MAX);
      }
      uint32_t constant = parse_variable("Expect parameter name.");
      define_variable(constant);
      if (!match(TokenComma)) {
        break;
      }
    }
  }

  consume(TokenRightParen, "Expect ')' after parameters.");
  consume(TokenLeftBrace, "Expect '{' before function body.");
  block();

  ObjFunction *func = end_compiler();

  emit_byte(OpClosure);
  emit_constant(OBJ_VAL(func));

  for (uint32_t i = 0; i < func->upvalues_len; i += 1) {
    emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
    emit_constant_idx(compiler.upvalues[i].index);
  }
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
  ObjFunction *function = end_compiler();

  return function;
}

ParseRule rules[] = {
    [TokenLeftParen] = {grouping, call, PrecCall},
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
  consume(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPrint);
}

static void return_statement() {
  if (current_compiler->function_type == TypeScript) {
    error("Can't return from top-level code.");
  }
  if (match(TokenSemiColon)) {
    emit_return();
  } else {
    expression();
    consume(TokenSemiColon, "Expect ';' after return value.");
    emit_byte(OpRet);
  }
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

static void expression_statement() {
  expression();
  consume(TokenSemiColon, "Expect ';' after value.");
  emit_byte(OpPop);
}

static void statement() {
  if (match(TokenPrint)) {
    print_statement();
  } else if (match(TokenIf)) {
    if_statement();
  } else if (match(TokenReturn)) {
    return_statement();
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

static void block() {
  while (!check(TokenRightBrace) && !check(TokenEof)) {
    declaration();
  }
  consume(TokenRightBrace, "Expect '}' after block.");
}

static void fn_declaration() {
  uint32_t variable = parse_variable("Expect function name.");
  init_variable();
  function(TypeFunction);
  define_variable(variable);
}

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

static void declaration() {
  if (match(TokenFn)) {
    fn_declaration();
  } else if (match(TokenLet)) {
    var_declaration();
  } else {
    statement();
  }

  if (parser.panic_mode) {
    synchronize();
  }
}
