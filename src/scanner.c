
#include "scanner.h"
#include "common.h"

#include <stdio.h>
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

static const char peek() {
  return *(scanner.current);
}

static bool is_at_end() { return peek() == '\0'; }

static const char peek_next() {
  if (is_at_end()) {
    return '\0';
  }
  return *(scanner.current + 1);
}


static bool match(const char expected) {
  if (is_at_end()) {
    return false;
  }
  if (peek()!= expected) {
    return false;
  }
  advance();
  return false;
}

static void skip_white_space() {
  while(true) {
    const char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t': {
        advance();
        break;
      } 
      case '\n': {
        scanner.line += 1;
        advance();
        break;
      }
      case '/': {
        if (peek_next() == '/') {
          while (peek() != '\n' && !is_at_end()) {
            advance(); 
          }
        } else {
          return;
        }
        break;
      }
      default:
        return;
    }    
  }
}

static Token string() {
  while(peek() != '"' && !is_at_end()) {
    if (peek() == '\n') {
      scanner.line += 1;
    }
    advance();
  }

  if (is_at_end()) {
    return error_token("Unterminated string.");
  }
  
  advance();
  return make_token(TokenString);
}

static bool is_digit(const char c) {
  return c >= '0' && c <= '9';
}

static Token number() {
  while (is_digit(peek())){
    advance();
  }

  if (peek() == '.' && is_digit(peek_next())) {
    advance();
    while (is_digit(peek())) {
      advance(); 
    }
  }

  return make_token(TokenNumber);
}

static bool is_alpha(const char c) {
  return ((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c == '_'));
}

static TokenType check_keyword(uint8_t start, uint8_t len,
                               const char * rest, TokenType type){

  if ((scanner.current - scanner.start == start + len) &&
      memcmp(scanner.start + start, rest, len) == 0){
    return type;
  }

  return TokenIdentifier;
}
static TokenType identifier_type () {
  switch (scanner.start[0]) {
    case 'a': return check_keyword(1, 2, "nd", TokenAnd);
    case 'c': return check_keyword(1, 4, "lass", TokenClass);
    case 'e': return check_keyword(1, 3, "lse", TokenElse);
    case 'f': {
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return check_keyword(2, 3, "lse", TokenFalse);
          case 'o': return check_keyword(2, 1, "r", TokenFor);
          case 'n': return check_keyword(2, 0, "", TokenFn);
        }
      }
      break;
    }
    case 'i': return check_keyword(1, 1, "f", TokenIf);
    case 'l': return check_keyword(1, 2, "et", TokenLet);
    case 'n': return check_keyword(1, 3, "ull", TokenNull);
    case 'o': return check_keyword(1, 1, "r", TokenOr);
    case 'p': return check_keyword(1, 4, "rint", TokenPrint);
    case 'r': return check_keyword(1, 5, "eturn", TokenReturn);
    case 's': {
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'u': return check_keyword(2, 2, "per", TokenSuper);
          case 'e': return check_keyword(2, 2, "lf", TokenSelf);
        }
      }
      break;
    }
    case 't': return check_keyword(1, 3, "rue", TokenTrue);
      
    case 'w': return check_keyword(1, 4, "hile", TokenWhile);
  }
  return TokenIdentifier;
}

static Token identifier() {
  while(is_alpha(peek()) || is_digit(peek())) {
    advance();
  }
  return make_token(identifier_type());
}

Token scan_token() {
  skip_white_space();
  scanner.start = scanner.current;
  if (is_at_end()) {
    return make_token(TokenEof);
  }
  const char c = advance();

  if (is_alpha(c)){
    return identifier();
  }

  if (is_digit(c)) {
    return number();
  }

  switch (c) {
    case '(': {
      return make_token(TokenLeftParen);
    }
    case ')': {
      return make_token(TokenRightParen);
    }
    case '{': {
      return make_token(TokenLeftBrace);
    }
    case '}': {
      return make_token(TokenRightBrace);
    }
    case ';': {
      return make_token(TokenSemiColon);
    }
    case ',': {
      return make_token(TokenComma);
    }
    case '.': {
      return make_token(TokenDot);
    }
    case '-': {
      return make_token(TokenMinus);
    }
    case '+': {
      return make_token(TokenPlus);
    }
    case '/': {
      return make_token(TokenSlash);
    }
    case '*': {
      return make_token(TokenStar);
    }
    case '!': {
      return make_token(match('=') ? TokenBangEqual : TokenBang);
    }
    case '=': {
      return make_token(match('=') ? TokenEqualEqual : TokenEqual);
    }
    case '<': {
      return make_token(match('=') ? TokenLessEqual : TokenLess);
    }
    case '>': {
      return make_token(match('=') ? TokenGreaterEqual : TokenGreater);
    }
    case '"': {
      return string();
    }
  }
  return error_token("Unexpected character.");
}
