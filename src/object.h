#ifndef breeze_object_h
#define breeze_object_h

#include <stdint.h>

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

/* Allocates memory for an object and sets its type
 * @param type: The struct type to allocate (used for casting)
 * @param object_type: The enum value to set in the object's type field
 * @return: A pointer to the newly allocated object, cast to the specified type
 *
 * Example:
 *     ObjString* str = ALLOCATE_OBJ(ObjString, ObjStringType);
 */
#define ALLOCATE_OBJ(type, object_type)                                        \
  (type *)allocate_object(sizeof(type), object_type)

/* Gets the type of an object from a value
 * @param value: Value struct instance to cast to an object
 * @return: An object type of the value
 */
#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_INSTANCE(value) is_obj_type(value, ObjInstanceType)
#define IS_CLASS(value) is_obj_type(value, ObjClassType)
#define IS_CLOSURE(value) is_obj_type(value, ObjClosureType)
#define IS_FUNCTION(value) is_obj_type(value, ObjFunctionType)
#define IS_NATIVE(value) is_obj_type(value, ObjNativeType)
#define IS_STRING(value) is_obj_type(value, ObjStringType)

#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
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
  ObjUpvalueType,
  ObjClassType,
  ObjInstanceType,
} ObjType;

typedef struct Obj {
  ObjType type;
  bool is_marked;
  struct Obj *next;
} Obj;

typedef struct ObjFunction {
  Obj obj;
  int32_t arity;
  uint32_t upvalues_len;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int32_t args_len, Value *args);

typedef struct ObjNative {
  Obj obj;
  NativeFn function;
} ObjNative;

typedef struct ObjString {
  Obj obj;
  uint32_t len;
  const char *chars;
  uint32_t hash;
} ObjString;

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct ObjClosure {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  uint32_t upvalues_len;
} ObjClosure;

typedef struct ObjClass {
  Obj obj;
  ObjString *name;
  Table methods;
} ObjClass;

typedef struct ObjInstance {
  Obj obj;
  ObjClass *klass;
  Table fields;
} ObjInstance;

/* Creates a new instance object
 * @param class (klass!): A pointer to a class object
 * @return: Pointer to the newly created instance
 */
ObjInstance *new_instance(ObjClass *klass);

/* Creates a new class object
 * @param name: A pointer to a string object
 * @return: Pointer to the newly created class
 */
ObjClass *new_class(ObjString *name);

/* Creates a new closure object that wraps a function
 * @param function: The function object to wrap
 * @return: Pointer to the newly created closure
 */
ObjClosure *new_closure(ObjFunction *function);

/* Creates a new empty function object
 * @return: Pointer to the newly created function
 */
ObjFunction *new_function();

/* Creates a new native function object
 * @param function: Pointer to the C function to wrap
 * @return: Pointer to the newly created native function object
 */
ObjNative *new_native(NativeFn function);

/* Creates a string object from an existing char array
 * @param chars: Pointer to the character array (takes ownership)
 * @param len: Length of the string
 * @return: Pointer to the newly created string object
 */
ObjString *take_string(char *chars, uint32_t len);

/* Creates a string object by copying a char array
 * @param chars: Pointer to the character array to copy
 * @param len: Length of the string
 * @return: Pointer to the newly created string object
 */
ObjString *copy_string(const char *, uint32_t);

/* Creates a new upvalue object
 * @param stack_slot: Pointer to the stack location of the captured value
 * @return: Pointer to the newly created upvalue
 */
ObjUpvalue *new_upvalue(Value *stack_slot);

/* Checks if a value is an object of a specific type
 * @param value: The value to check
 * @param type: The object type to compare against
 * @return: true if the value is an object of the specified type
 */
static inline bool is_obj_type(Value value, ObjType type) {
  return IS_OBJ(value) && (AS_OBJ(value)->type == type);
}

/* Prints the string representation of an object
 * @param value: The value containing the object to print
 */
void print_object(Value value);

#endif // !breeze_object_h
