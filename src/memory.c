#include <stdbool.h>

#include "memory.h"

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "virtual_machine.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif /* ifdef DEBUG_LOG_GC */

void *reallocate(void *ptr, size_t old_capacity, size_t new_capacity) {
  if (new_capacity > old_capacity) {
#ifdef DEBUG_STRESS_GC
    collect_garbage();
#endif /* ifdef DEBUG_STRESS_GC */
  }

  if (new_capacity == 0) {
    free(ptr);
    return NULL;
  }

  void *result = realloc(ptr, new_capacity);
  if (result == NULL) {
    exit(1);
  }
  return result;
}

static void free_object(Obj *object) {
  switch (object->type) {
  case ObjClosureType: {
    ObjClosure *closure = (ObjClosure *)object;
    FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalues_len);
    FREE(ObjClosure, object);
    break;
  }
  case ObjNativeType: {
    FREE(ObjNative, object);
    break;
  }
  case ObjFunctionType: {
    ObjFunction *function = (ObjFunction *)object;
    free_chunk(&function->chunk);
    FREE(ObjFunction, object);
    break;
  }
  case ObjStringType: {
    ObjString *string = (ObjString *)object;
    FREE_ARRAY(char, (void *)string->chars, string->len + 1);
    FREE(ObjString, object);
    break;
  }
  case ObjUpvalueType: {
    FREE(ObjUpvalue, object);
    break;
  }
  }
}

static void mark_roots() {
  for (Value *slot = vm.stack; slot < vm.stack_ptr; slot += 1) {
    // mark_value(*slot);
  }
}

void collect_garbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif /* ifdef DEBUG_LOG_GC*/

  mark_roots();

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
#endif /* ifdef DEBUG_LOG_GC*/
}

void free_objects(Obj *object) {
  while (object != NULL) {
    Obj *next = object->next;
    free_object(object);
    object = next;
  }
}
