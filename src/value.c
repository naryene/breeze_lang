#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "object.h"

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
  switch (value.type) {
    case ValBool: {
      printf(AS_BOOL(value) ? "true" : "false");
      break;
    }
    case ValNull: {
      printf("null");
      break;
    }
    case ValNumber: {
      printf("%g", AS_NUMBER(value));
      break;
    }
    case ValObj: {
      print_object(value);
      break;
    }
  }
}

bool values_equal(Value left, Value right) {
  if (left.type != right.type) {
    return false;
  }
  switch (left.type) {
    case ValBool: return AS_BOOL(left) == AS_BOOL(right);
    case ValNull: return true;
    case ValNumber: return AS_NUMBER(left)== AS_NUMBER(right);
    case ValObj: return AS_OBJ(left) == AS_OBJ(right);
    default: return false;
  }
}
