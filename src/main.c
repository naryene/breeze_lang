#include "chunk.h"
#include "debug.h"
#include "common.h"
#include "value.h"
#include "virtual_machine.h"
#include <stdio.h>

int32_t main() {
  Chunk chunk;
  init_chunk(&chunk);
  push_constant(&chunk, 1.2, 123);
  push_constant(&chunk, 1.2, 123);
  write_chunk(&chunk, OpRet, 124);
  write_chunk(&chunk, OpRet, 125);
  disassemble_chunk(&chunk, "chunk");
  free_chunk(&chunk);
  return 0;
}
