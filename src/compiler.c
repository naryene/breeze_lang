#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "scanner.h"

typedef struct {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mod;
} Parser;

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
}

static void number() {
  double value = strtod(parser.previous.start, NULL);
  emit_constant(value);
}

// TODO:
static void expression() {}

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
