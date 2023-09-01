#include "chunk.h"
#include "debug.h"
#include "common.h"

int32_t main() {
  Chunk chunk;
  init_chunk(&chunk);
  uint32_t constant = add_constant(&chunk, 1.2);
  write_chunk(&chunk, OpConstant, 123);
  write_chunk(&chunk, constant, 123);

  write_chunk(&chunk, OpReturn, 123);

  disassemble_chunk(&chunk, "test chunk");
  free_chunk(&chunk);
  return 0;
}
