#ifndef breeze_memory_h
#define breeze_memory_h

#include <stdlib.h>

#include "object.h"
#include "common.h"
#include "value.h"

#define ALLOCATE(type, new_capacity) \
  (type*)reallocate(NULL, 0, sizeof(type)*new_capacity)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, ptr, old_capacity, new_capacity) \
    (type*) reallocate(ptr, sizeof(type) * (old_capacity), \
        sizeof(type) * (new_capacity))

#define FREE_ARRAY(type, ptr, old_capacity) \
    reallocate(ptr, sizeof(type) * (old_capacity), 0)

#define FREE(type, ptr) reallocate(ptr, sizeof(type), 0)

void* reallocate(void* ptr, size_t old_capacity, size_t new_capacity);
void mark_object(Obj*);
void mark_value(Value);
void collect_garbage();
void free_objects(Obj *object);

#endif // !breeze_memory_h
