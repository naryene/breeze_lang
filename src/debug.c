#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "debug.h"

#include "object.h"

static uint32_t simple_inst(const char *name, uint32_t offset) {
  printf("%s\n", name);
  return offset + 1;
}

typedef struct {
  uint32_t offset;
  uint32_t constant_idx;
} OffsetConstantIdx;

static void read_idx(const Chunk *chunk, uint32_t *offset,
                     uint32_t *constant_idx) {

  uint8_t constant_op = chunk->code[*offset];

  if (constant_op == OpConst) {
    *constant_idx = chunk->code[*offset + 1];
    *offset += 2;
  } else {
    *constant_idx = chunk->code[*offset + 1] | (chunk->code[*offset + 2] << 8) |
                    (chunk->code[*offset + 3] << 16);
    *offset += 4;
  }
}

static void constant_inst(const char *name, const Chunk *chunk,
                          uint32_t *offset, uint32_t *constant_idx) {
  uint32_t index = 0;
  if (constant_idx == NULL) {
    constant_idx = &index;
  }
  read_idx(chunk, offset, constant_idx);
  printf("%-16s %4d '", name, *constant_idx);
  print_value(chunk->constants.values[*constant_idx]);
  printf("'\n");
}

static uint32_t byte_inst(const char *name, const Chunk *chunk,
                          uint32_t offset) {
  uint8_t byte = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, byte);
  return offset + 2;
}

static void special_inst(const char *name, const Chunk *chunk, uint32_t *offset,
                         uint32_t *constant_idx) {
  *offset += 1;
  uint32_t index = 0;
  if (constant_idx == NULL) {
    constant_idx = &index;
  }
  read_idx(chunk, offset, constant_idx);
  printf("%-16s %4d\n", name, *constant_idx);
}

static uint32_t jmp_inst(const char *name, int8_t sign, const Chunk *chunk,
                         uint32_t offset) {
  uint16_t jmp = (uint16_t)chunk->code[offset + 1];
  jmp |= chunk->code[offset + 2] << 8;
  offset += 3;
  printf("%-16s %4d -> %d\n", name, offset, jmp);
  return offset;
}

uint32_t disassemble_inst(const Chunk *chunk, uint32_t offset) {
  printf("%04d ", offset);
  uint32_t curr_line = get_line(&chunk->lines, offset);
  uint32_t prev_line = offset > 0 ? get_line(&chunk->lines, offset - 1) : 0;
  if (offset > 0 && curr_line == prev_line) {
    printf("    | ");
  } else {
    printf("%4d ", curr_line);
  }

  uint8_t inst = chunk->code[offset];
  switch (inst) {
  case OpRet:
    return simple_inst("OpRet", offset);
  case OpClosure: {
    offset += 1;
    uint32_t constant_idx;
    constant_inst("OpClosure", chunk, &offset, &constant_idx);
    ObjFunction *function = AS_FUNCTION(chunk->constants.values[constant_idx]);
    for (uint32_t i = 0; i < function->upvalues_len; i += 1) {
      bool is_local = chunk->code[offset];
      offset += 1;
      read_idx(chunk, &offset, &constant_idx);
      printf("%04d    |             %s %d\n", offset - 2,
             is_local ? "local" : "upvalue", constant_idx);
    }
    return offset;
  }
  case OpCloseUpvalue:
    return simple_inst("OpCloseUpvalue", offset);
  case OpCall:
    return byte_inst("OpCall", chunk, offset);
  case OpJmp:
    return jmp_inst("OpJmp", 1, chunk, offset);
  case OpJmpIfFalse:
    return jmp_inst("OpJmpIfFalse", 1, chunk, offset);
  case OpConst:
    constant_inst("OpConst", chunk, &offset, NULL);
    return offset;
  case OpConstLong:
    constant_inst("OpConstLong", chunk, &offset, NULL);
    return offset;
  case OpNull:
    return simple_inst("OpNull", offset);
  case OpTrue:
    return simple_inst("OpTrue", offset);
  case OpFalse:
    return simple_inst("OpFalse", offset);
  case OpNot:
    return simple_inst("OpNot", offset);
  case OpNeg:
    return simple_inst("OpNeg", offset);
  case OpGetGlobal:
    return simple_inst("OpGetGlobal", offset);
  case OpDefineGlobal:
    return simple_inst("OpDefineGlobal", offset);
  case OpSetGlobal:
    constant_inst("OpSetGlobal", chunk, &offset, NULL);
    return offset;
  case OpGetUpvalue:
    special_inst("OpGetUpvalue", chunk, &offset, NULL);
    return offset;
  case OpSetUpvalue:
    special_inst("OpSetUpvalue", chunk, &offset, NULL);
    return offset;
  case OpGetLocal:
    special_inst("OpGetLocal", chunk, &offset, NULL);
    return offset;
  case OpSetLocal:
    special_inst("OpSetLocal", chunk, &offset, NULL);
    return offset;
  case OpEq:
    return simple_inst("OpEq", offset);
  case OpGt:
    return simple_inst("OpGt", offset);
  case OpLt:
    return simple_inst("OpLt", offset);
  case OpAdd:
    return simple_inst("OpAdd", offset);
  case OpSub:
    return simple_inst("OpSub", offset);
  case OpMul:
    return simple_inst("OpMul", offset);
  case OpDiv:
    return simple_inst("OpDiv", offset);
  case OpPrint:
    return simple_inst("OpPrint", offset);
  case OpPop:
    return simple_inst("OpPop", offset);
  default: {
    printf("Unknown opcode %d\n", inst);
    return offset + 1;
  }
  }
}

void print_lines(const Chunk *chunk) {
  printf("-----------------------------\n");
  for (size_t i = 0; i < chunk->lines.len; i += 1) {
    printf("[ %u %u ]\n", chunk->lines.lines[i][0], chunk->lines.lines[i][1]);
  }
  printf("-----------------------------\n");
}
void disassemble_chunk(const Chunk *chunk, const char *name) {
  if (chunk == NULL) {
    return;
  }
  printf("== %s ==\n\n", name);

  for (uint32_t offset = 0; offset < chunk->len;) {
    offset = disassemble_inst(chunk, offset);
  }
  printf("\n===");
  for (uint8_t i = 0; i < strlen(name); i += 1) {
    printf("=");
  }
  printf("===\n");
  print_lines(chunk);
}
