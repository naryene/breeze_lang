#ifndef breeze_chunk_h
#define breeze_chunk_h

#include <stdint.h>

#include "value.h"
#include "common.h"


typedef enum {
  OpRet,
  OpConst,
  OpConstLong,
  OpNull,
  OpTrue,
  OpFalse,
  OpNot,
  OpNeg,
  OpEq,
  OpGt,
  OpLt,
  OpAdd,
  OpSub,
  OpMul,
  OpDiv,
  OpPrint,
  OpPop,
  OpCloseUpvalue,
  OpDefineGlobal,
  OpSetGlobal,
  OpGetGlobal,
  OpGetUpvalue,
  OpSetUpvalue,
  OpSetLocal,
  OpGetLocal,
  OpJmpIfFalse,
  OpJmp,
  OpClosure,
  OpCall,
} OpCode;

/***
  line[0]: line number;
  line[1]: index of the last code that is in that line
  ***/
typedef uint32_t Line[2];

typedef struct {
  uint32_t len;
  uint32_t capacity;
  Line *lines;
} LineVec;

typedef struct {
  uint32_t len;
  uint32_t capacity;
  uint8_t *code;
  LineVec lines;
  ValueVec constants;
} Chunk;

uint32_t get_line(const LineVec *lines, uint32_t offset);

void init_chunk(Chunk *chunk);
void free_chunk(Chunk *chunk);
void write_chunk(Chunk *chunk, uint8_t byte, uint32_t line);
uint32_t add_constant(Chunk *chunk, Value value);
uint32_t push_constant(Chunk *chunk, Value value, uint32_t line);
void write_constant_chunk(Chunk *chunk, uint32_t constant, uint32_t line);

#endif // !breeze_chunk_h
