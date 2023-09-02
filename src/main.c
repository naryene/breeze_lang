#include "chunk.h"
#include "debug.h"
#include "common.h"
#include "value.h"

int32_t main() {
  Chunk chunk;
  init_chunk(&chunk);
  push_constant(&chunk, 1.2, 123);
  write_value_vec(&chunk.constants, 12);
  write_chunk(&chunk, OpConstantLong, 124);
  write_chunk(&chunk, 1, 124);
  write_chunk(&chunk, 0, 124);
  write_chunk(&chunk, 0, 124);

  write_chunk(&chunk, OpReturn, 124);

  disassemble_chunk(&chunk, "test chunk");
  free_chunk(&chunk);
  return 0;
}
