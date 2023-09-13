#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

Obj* allocate_object(uint32_t size, ObjType type) {
  Obj* object = (Obj*) reallocate(NULL, 0, size);
  object->type = type;
  return object;
}

ObjString* allocate_string(const char* chars, uint32_t len) {
  ObjString* string = ALLOCATE_OBJ(ObjString, ObjStringType);
  string->len = len;
  string->chars = chars;
  return string;
}

ObjString* copy_string(const char* chars, uint32_t len){
  char* heap_chars = ALLOCATE(char, len+1);
  memcpy(heap_chars, chars, len);
  heap_chars[len] = '\0';
  return allocate_string(heap_chars, len);
};
