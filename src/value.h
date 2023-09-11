#ifndef breeze_value_h
#define breeze_value_h

#include "common.h"

typedef enum {
  ValBool,
  ValNull,
  ValNumber,
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
  } as;
} Value;

#define IS_BOOL(value) ((value).type == ValBool)
#define IS_NULL(value) ((value).type == ValNull)
#define IS_NUMBER(value) ((value).type == ValNumber)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) ((Value) {ValBool, {.boolean = value}})
#define NULL_VAL ((Value) {ValNull, {.number = 0}})
#define NUMBER_VAL(value) ((Value) {ValNumber, {.number = value}})

typedef struct {
  uint32_t capacity;
  uint32_t len;
  Value *values;
} ValueVec;

bool values_equal(Value left, Value right);
void init_value_vec(ValueVec *vec);
void write_value_vec(ValueVec *vec, Value value);
void free_value_vec(ValueVec *vec);
void print_value(Value value);

#endif // !breeze_value_h
