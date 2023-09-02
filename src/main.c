#include "chunk.h"
#include "debug.h"
#include "common.h"
#include "value.h"
#include "virtual_machine.h"

int32_t main() {
  init_vm();

  Chunk chunk;
  init_chunk(&chunk);
  write_value_vec(&chunk.constants, 12);
  write_chunk(&chunk, OpConstantLong, 124);
  write_chunk(&chunk, (uint8_t) 0 & 0xff, 124);
  write_chunk(&chunk, (uint8_t) 0 & 0xff, 124);
  write_chunk(&chunk, (uint8_t) 0 & 0xff, 124);

  write_chunk(&chunk, OpReturn, 124);

  disassemble_chunk(&chunk, "test chunk");
  interpret(&chunk);
  free_vm();
  free_chunk(&chunk);
  return 0;
}
