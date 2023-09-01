#ifndef breeze_chunk_h
#define breeze_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  OpReturn,
  OpConstant,
} OpCode;

/***
  line[0]: line number;
  line[1]: index of the last code that is in that line
  ***/
typedef uint32_t Line[2];

typedef struct {
  uint32_t count;
  uint32_t capacity;
  Line* lines;
} LineVec;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  uint8_t* code;
  LineVec lines;
  ValueVec constants;
} Chunk;

void init_line_vec(LineVec* line_vec);
void free_line_vec(LineVec* line_vec);
void write_line_vec(LineVec* line_vec, uint32_t line, uint32_t offset);
uint32_t get_line(LineVec lines, uint32_t offset);


void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte, uint32_t line);
uint32_t add_constant(Chunk* chunk, Value value);

#endif // !breeze_chunk_h
