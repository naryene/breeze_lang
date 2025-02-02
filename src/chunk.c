#include <stdbool.h>
#include <stdlib.h>

#include "chunk.h"

#include "memory.h"
#include "value.h"
#include "virtual_machine.h"

static void init_line_vec(LineVec *line_vec) {
  line_vec->len = 0;
  line_vec->capacity = 0;
  line_vec->lines = NULL;
}

static void free_line_vec(LineVec *line_vec) {
  FREE_ARRAY(uint32_t[2], line_vec->lines, line_vec->capacity);
  init_line_vec(line_vec);
}

static void write_line_vec(LineVec *line_vec, uint32_t line, uint32_t offset) {
  if (line_vec->capacity < line_vec->len + 1) {
    uint32_t old_capacity = line_vec->capacity;
    line_vec->capacity = GROW_CAPACITY(old_capacity);
    line_vec->lines =
        GROW_ARRAY(Line, line_vec->lines, old_capacity, line_vec->capacity);
  }

  // Check if we are inserting a line equals to the last inserted line
  if (line_vec->len > 0 && line == line_vec->lines[line_vec->len - 1][0]) {
    line_vec->lines[line_vec->len - 1][1] = offset;
  } else {
    line_vec->lines[line_vec->len][0] = line;
    line_vec->lines[line_vec->len][1] = offset;
    line_vec->len += 1;
  }
}

uint32_t get_line(const LineVec *line_vec, uint32_t inst) {
  if (line_vec->len == 0) {
    return 0;
  }
  int32_t start = 0;
  int32_t end = line_vec->len - 1;
  while (start < end) {
    int32_t mid = start + (end - start) / 2;
    Line *line = &line_vec->lines[mid];

    if (inst < (*line)[1]) {
      end = mid - 1;
    } else if (inst < line_vec->lines[mid + 1][1]) {
      return line_vec->lines[mid + 1][0];
    } else {
      start = mid + 1;
    }
  }
  return line_vec->lines[start][0];
}
void init_chunk(Chunk *chunk) {
  chunk->len = 0;
  chunk->capacity = 0;
  chunk->code = 0;
  init_line_vec(&chunk->lines);
  init_value_vec(&chunk->constants);
}

void free_chunk(Chunk *chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  free_line_vec(&chunk->lines);
  free_value_vec(&chunk->constants);
  init_chunk(chunk);
}

void write_chunk(Chunk *chunk, uint8_t byte, uint32_t line) {
  if (chunk->capacity < chunk->len + 1) {
    uint32_t old_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_capacity);
    chunk->code =
        GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
  }
  write_line_vec(&chunk->lines, line, chunk->len);

  chunk->code[chunk->len] = byte;
  chunk->len += 1;
}

uint32_t add_constant(Chunk *chunk, Value value) {
  push_stack(value);
  write_value_vec(&chunk->constants, value);
  pop_stack();
  return chunk->constants.len - 1;
}

uint32_t push_constant(Chunk *chunk, Value value, uint32_t line) {
  uint32_t idx = add_constant(chunk, value);

  if (idx > UINT16_MAX) {
    return UINT32_MAX;
  }

  if (idx < UINT8_MAX) {
    write_chunk(chunk, OpConst, line);
    write_chunk(chunk, (uint8_t)idx, line);
    return idx;
  }
  write_chunk(chunk, OpConstLong, line);
  write_chunk(chunk, (uint8_t)(idx & 0xff), line);
  write_chunk(chunk, (uint8_t)((idx >> 8) & 0xff), line);
  write_chunk(chunk, (uint8_t)((idx >> 16) & 0xff), line);
  return idx;
}

void write_constant_chunk(Chunk *chunk, uint32_t constant, uint32_t line) {
  if (constant < UINT8_MAX) {
    write_chunk(chunk, OpConst, line);
    write_chunk(chunk, (uint8_t)constant, line);
    return;
  }
  write_chunk(chunk, OpConstLong, line);
  write_chunk(chunk, (uint8_t)(constant & 0xff), line);
  write_chunk(chunk, (uint8_t)((constant >> 8) & 0xff), line);
  write_chunk(chunk, (uint8_t)((constant >> 16) & 0xff), line);
  return;
}
