#include <stdlib.h>

#include "memory.h"
#include "virtual_machine.h"

void *reallocate(void *ptr, size_t old_capacity, size_t new_capacity) {
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
  case ObjStringType: {
    ObjString *string = (ObjString *)object;
    FREE_ARRAY(char, (void*) string->chars, string->len + 1);
    FREE(ObjString, object);
    break;
  }
  }
}
void free_objects() {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    free_object(object);
    object = next;
  }
}
