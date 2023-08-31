#include "chunk.h"
#include "debug.h"
#include "common.h"

int32_t main() {
  Chunk chunk;
  init_chunk(&chunk);
  write_chunk(&chunk, OpReturn);
  disassemble_chunk(&chunk, "test chunk");
  free_chunk(&chunk);
  return 0;
}
