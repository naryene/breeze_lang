#include "chunk.h"
#include "debug.h"
#include "common.h"
#include "value.h"
#include "virtual_machine.h"
#include <stdio.h>

int32_t main() {
  init_vm();
  
  Chunk chunk;
  init_chunk(&chunk);

  push_constant(&chunk, 3.4, 123);
  push_constant(&chunk, 123, 123);
  write_chunk(&chunk, OpAdd, 124);

  push_constant(&chunk, 5.6, 124);

  write_chunk(&chunk, OpDiv, 124);
  write_chunk(&chunk, OpNeg, 124);
  write_chunk(&chunk, OpRet, 124);

  disassemble_chunk(&chunk, "chunk");
  interpret(&chunk);
  free_vm();
  free_chunk(&chunk);
  return 0;
}
