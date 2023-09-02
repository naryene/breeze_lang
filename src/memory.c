#include <stdlib.h>

#include "memory.h"

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
