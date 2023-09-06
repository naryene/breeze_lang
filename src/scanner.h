#ifndef breeze_scanner_h
#define breeze_scanner_h

#include "common.h"

typedef enum {
  TokenLeftParen,
  TokenRightParen,
  TokenLeftBrace,
  TokenRightBrace,
  TokenComma,
  TokenDot,
  TokenMinus,
  TokenPlus,
  TokenSemiColon,
  TokenSlash,
  TokenStar,

  TokenBang,
  TokenBangEqual,
  TokenEqual,
  TokenEqualEqual,
  TokenGreater,
  TokenGreaterEqual,
  TokenLess,
  TokenLessEqual,

  TokenIdentifier,
  TokenString,
  TokenNumber,

  TokenAnd,
  TokenClass,
  TokenElse,
  TokenFalse,
  TokenFor,
  TokenFn,
  TokenIf,
  TokenNull,
  TokenOr,
  TokenPrint,
  TokenReturn,
  TokenSuper,
  TokenThis,
  TokenTrue,
  TokenLet,
  TokenWhile,

  TokenError,
  TokenEof,
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  uint32_t len;
  uint32_t line;
} Token;

void init_scanner(const char *source);
Token scan_token();

#endif // !breeze_scanner_h
