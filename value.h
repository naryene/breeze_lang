#ifndef breeze_value_h
#define breeze_value_h

#include "common.h"

typedef double Value;

typedef struct {
  uint32_t capacity;
  uint32_t len;
  Value* values;
} ValueVec;

void init_value_vec(ValueVec* vec);
void write_value_vec(ValueVec* vec, Value value);
void free_value_vec(ValueVec* vec);
void print_value(Value value);

#endif // !breeze_value_h
