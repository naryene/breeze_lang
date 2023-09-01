#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "value.h"

void init_line_vec(LineVec *line_vec){
  line_vec->count = 0;
  line_vec->capacity = 0;
  line_vec->lines = NULL;
}

void free_line_vec(LineVec *line_vec){
  FREE_ARRAY(uint32_t[2], line_vec->lines, line_vec->capacity);
  init_line_vec(line_vec);
}

void write_line_vec(LineVec *line_vec, uint32_t line, uint32_t offset){
  if (line_vec->capacity < line_vec->count + 1){
    uint32_t old_capacity = line_vec->capacity;
    line_vec->capacity = GROW_CAPACITY(old_capacity);
    line_vec->lines = GROW_ARRAY(Line, line_vec->lines, old_capacity, line_vec->capacity);
  }


  if (line_vec->count > 0 &&
    line == line_vec->lines[line_vec->count - 1][0]){
    line_vec->lines[line_vec->count - 1 ][1] = offset;
  } else {
    line_vec->lines[line_vec->count][0] = line;
    line_vec->lines[line_vec->count][1] = offset;
    line_vec->count +=1;
  }

}

uint32_t get_line(LineVec line_vec, uint32_t offset) {
  int32_t start = 0;
  int32_t end = line_vec.count;
  while (true) {
    int32_t mid = (start + end)/2;

    if (start > end) {
      return line_vec.lines[mid][0];
    }
    if (offset > line_vec.lines[mid][1]){
      start = mid + 1;
    } else {
      end = mid - 1;
    }
  }
}

void init_chunk(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = 0;
  init_line_vec(&chunk->lines);
  init_value_vec(&chunk->constants);
}

void free_chunk(Chunk *chunk){
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  free_line_vec(&chunk->lines);
  free_value_vec(&chunk->constants);
  init_chunk(chunk);
}

void write_chunk(Chunk *chunk, uint8_t byte, uint32_t line) {
  if (chunk->capacity < chunk->count + 1){
    uint32_t old_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_capacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
  }
  write_line_vec(&chunk->lines, line, chunk->count);
  chunk->code[chunk->count] = byte;
  chunk->count += 1;
}

uint32_t add_constant(Chunk *chunk, Value value){
  write_value_vec(&chunk->constants, value);
  return chunk->constants.count - 1;
}
