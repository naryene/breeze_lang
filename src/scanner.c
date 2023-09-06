
#include "scanner.h"
#include "common.h"

#include <string.h>

typedef struct {
  const char *start;
  const char *current;
  uint32_t line;
} Scanner;

Scanner scanner;

void init_scanner(const char *source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool is_at_end() { return *scanner.current == '\0'; }

static Token make_token(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.len = (uint32_t)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token error_token(const char *message) {
  Token token;
  token.type = TokenError;
  token.start = message;
  token.len = (uint32_t)strlen(message);
  token.line = scanner.line;
  return token;
}

static const char advance() {
  scanner.current += 1;
  return *(scanner.current - 1);
}

Token scan_token() {
  scanner.start = scanner.current;
  if (is_at_end()) {
    return make_token(TokenEof);
  }
  const char c = advance();

  switch (c) {
    case '(' : {
      return make_token(TokenLeftParen);
    }
    case ')' : {
      return make_token(TokenRightParen);
    }
    case '{' : {
      return make_token(TokenLeftBrace);
    }
    case '}' : {
      return make_token(TokenRightBrace);
    }
    case ';' : {
      return make_token(TokenSemiColon);
    }
    case ',' : {
      return make_token(TokenComma);
    }
    case '.' : {
      return make_token(TokenDot);
    }
    case '-' : {
      return make_token(TokenMinus);
    }
    case '+' : {
      return make_token(TokenPlus);
    }
    case '/' : {
      return make_token(TokenSlash);
    }
    case '*' : {
      return make_token(TokenStar);
    }
  }
  return error_token("Unexpected character.");
}
