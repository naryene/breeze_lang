
#include "scanner.h"
#include "common.h"

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


