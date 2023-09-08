#include "chunk.h"
#include "common.h"
#include "value.h"
#include "virtual_machine.h"
#include <stdio.h>
#include <stdlib.h>

static void repl();
static void run_file(const char *);
static const char *read_file(const char *);

int32_t main(int32_t argc, const char *argv[]) {
  init_vm();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    run_file(argv[1]);
  } else {
    fprintf(stderr, "Usage: breeze <path>");
    exit(64);
  }

  free_vm();
  return 0;
}

static void repl() {
  char line[1024];
  while (true) {
    printf(">> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line);
  }
}

static void run_file(const char *path) {
  const char *source = read_file(path);
  InterpretResult result = interpret(source);
  free((void*) source);

  if (result == InterpretCompileErr) {
    exit(65);
  }
  if (result == InterpretRuntimeErr) {
    exit(70);
  }
}

static const char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(file_size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memero to read \"%s\"", path);
    exit(74);
  }

  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read < file_size) {
    fprintf(stderr, "Could not read file \"%s\"", path);
    exit(74);
  }
  buffer[bytes_read] = '\0';

  fclose(file);
  return buffer;
}
