#ifndef breeze_chunk_h
#define breeze_chunk_h

#include "common.h"

typedef enum {
  OpReturn,
} OpCode;

typedef struct {
  int32_t count;
  int32_t capacity;
  uint8_t* code;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte);

#endif // !breeze_chunk_h
