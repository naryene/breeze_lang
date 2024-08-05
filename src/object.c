#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "virtual_machine.h"

static Obj *allocate_object(uint32_t size, ObjType type) {
  Obj *object = (Obj *)reallocate(NULL, 0, size);
  object->type = type;

  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjClosure *new_closure(ObjFunction *function) {
  ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalues_len);
  for (uint32_t i = 0; i < function->upvalues_len; i += 1) {
    upvalues[i] = NULL;
  }

  ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, ObjClosureType);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalues_len = function->upvalues_len;
  return closure;
}

ObjFunction *new_function() {
  ObjFunction *function = ALLOCATE_OBJ(ObjFunction, ObjFunctionType);
  function->arity = 0;
  function->upvalues_len = 0;
  function->name = NULL;
  init_chunk(&function->chunk);
  return function;
}

ObjNative *new_native(NativeFn function) {
  ObjNative *native = ALLOCATE_OBJ(ObjNative, ObjNativeType);
  native->function = function;
  return native;
}

static ObjString *allocate_string(const char *chars, uint32_t len,
                                  uint32_t hash) {
  ObjString *string = ALLOCATE_OBJ(ObjString, ObjStringType);
  string->len = len;
  string->chars = chars;
  string->hash = hash;
  table_insert(&vm.strings, string, NULL_VAL);
  return string;
}

static uint32_t hash_string(const char *key, uint32_t len) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < len; i += 1) {
    hash ^= (uint32_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString *take_string(char *chars, uint32_t len) {
  uint32_t hash = hash_string(chars, len);
  ObjString *interned = table_find_string(&vm.strings, chars, len, hash);

  if (interned != NULL) {
    FREE_ARRAY(char, chars, len + 1);
    return interned;
  }

  return allocate_string(chars, len, hash);
}

ObjString *copy_string(const char *chars, uint32_t len) {
  uint32_t hash = hash_string(chars, len);
  ObjString *interned = table_find_string(&vm.strings, chars, len, hash);

  if (interned != NULL) {
    return interned;
  }

  char *heap_chars = ALLOCATE(char, len + 1);
  memcpy(heap_chars, chars, len);
  heap_chars[len] = '\0';
  return allocate_string(heap_chars, len, hash);
}

ObjUpvalue *new_upvalue(Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, ObjUpvalueType);
  upvalue->location = slot;
  return upvalue;
}

void print_function(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void print_object(Value value) {
  switch (OBJ_TYPE(value)) {
  case ObjClosureType: {
    print_function(AS_CLOSURE(value)->function);
    break;
  }
  case ObjFunctionType: {
    print_function(AS_FUNCTION(value));
    break;
  }
  case ObjNativeType: {
    printf("<native fn>");
    break;
  }
  case ObjStringType: {
    printf("%s", AS_CSTRING(value));
    break;
  }
  case ObjUpvalueType: {
    printf("upvalue");
    break;
  }
  }
}
