#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

void compile(const char *source) {
  init_scanner(source);
  uint32_t line = 0;
  while (true) {
    Token token = scan_token();
    if (token.line != line) {
      printf("%d ", token.line);
      line = token.line;
    } else {
      printf("   | ");
    }
    printf("%2d '%.*s'\n", token.type, token.len, token.start);

    if (token.type == TokenEof) {
      break;
    }
  }
}
