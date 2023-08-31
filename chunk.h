#ifndef breeze_chunk_h
#define breeze_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  OpReturn,
  OpConstant,
} OpCode;

typedef struct {
  int32_t count;
  int32_t capacity;
  uint8_t* code;
  ValueVec constants;
} Chunk;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte);
int32_t add_constant(Chunk* chunk, Value value);

#endif // !breeze_chunk_h
