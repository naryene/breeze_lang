#include <stdio.h>

#include "debug.h"

void disassemble_chunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int32_t offset = 0; offset < chunk->count;) {
    offset = disassemble_inst(chunk, offset);
  }
}

int32_t simple_inst(const char* name, int32_t offset) {
  printf("%s\n", name);
  return offset+1;
}

int32_t disassemble_inst(Chunk *chunk, int32_t offset) {
  printf("%04d ", offset);

  uint8_t inst = chunk->code[offset];
  switch (inst) {
    case OpReturn:
      return simple_inst("OpReturn", offset) ;
    default:
      printf("Unknown opcode %d\n", inst);
      return offset+1;
  }
}

