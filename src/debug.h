#ifndef breeze_debug_h
#define breeze_debug_h

#include <stdint.h>

#include "chunk.h"

void disassemble_chunk(const Chunk *chunk, const char *name);
uint32_t disassemble_inst(const Chunk *chunk, uint32_t offset);

#endif // !breeze_debug_h
