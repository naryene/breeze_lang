#ifndef breeze_debug_h
#define breeze_debug_h

#include "chunk.h"

void disassemble_chunk(Chunk* chunk, const char* name);
int32_t disassemble_inst(Chunk* chunk, int32_t offset);

#endif // !breeze_debug_h
