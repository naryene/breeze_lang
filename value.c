#include <stdio.h>

#include "memory.h"
#include "value.h"

void init_value_vec(ValueVec* vec){
  vec->values = NULL;
  vec->capacity =0;
  vec->len = 0;
}

void write_value_vec(ValueVec* vec, Value value){
  if (vec->capacity < vec->len+1) {
    uint32_t old_capacity = vec->capacity;
    vec->capacity = GROW_CAPACITY(old_capacity);
    vec->values = GROW_ARRAY(Value, vec->values, old_capacity, vec->capacity);
  }

  vec->values[vec->len] = value;
  vec->len += 1;
}

void free_value_vec(ValueVec* vec) {
  FREE_ARRAY(Value, vec->values, vec->capacity);
  init_value_vec(vec);
}

void print_value(Value value) {
  printf("%g", value);
}
