#ifndef breeze_object_h
#define breeze_object_h

#include <stdint.h>

#include "common.h"
#include "value.h"
#include "chunk.h"

#define ALLOCATE_OBJ(type, object_type)                                        \
  (type *)allocate_object(sizeof(type), object_type)

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) is_obj_type(value, ObjClosureType)
#define IS_FUNCTION(value) is_obj_type(value, ObjFunctionType)
#define IS_NATIVE(value) is_obj_type(value, ObjNativeType)
#define IS_STRING(value) is_obj_type(value, ObjStringType)

#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
  ObjNativeType,
  ObjFunctionType,
  ObjStringType,
  ObjClosureType,
  ObjUpvalueType
} ObjType;

struct Obj {
  ObjType type;
  bool is_marked;
  struct Obj *next;
};

typedef struct {
  Obj obj;
  int32_t arity;
  int32_t upvalues_len;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int32_t args_len, Value *args);

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj obj;
  uint32_t len;
  const char *chars;
  uint32_t hash;
};

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  uint32_t upvalues_len;
} ObjClosure;

ObjClosure *new_closure(ObjFunction *);
ObjFunction *new_function();
ObjNative *new_native(NativeFn);
ObjString *take_string(char *, uint32_t);
ObjString *copy_string(const char *, uint32_t);
ObjUpvalue *new_upvalue(Value *);

static inline bool is_obj_type(Value value, ObjType type) {
  return IS_OBJ(value) && (AS_OBJ(value)->type == type);
}

void print_object(Value value);

#endif // !breeze_object_h
