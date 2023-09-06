#ifndef breeze_scanner_h
#define breeze_scanner_h

#include "common.h"

typedef enum {
  TokenEof,
} TokenType;

typedef struct {
  TokenType type;
  const char* start;
  uint32_t len;
  uint32_t line;
} Token;

void init_scanner(const char* source);
Token scan_token();

#endif // !breeze_scanner_h
