#ifndef breeze_memory_h
#define breeze_memory_h

#include "common.h"

#define ALLOCATE(type, new_capacity) \
  (type*)reallocate(NULL, 0, sizeof(type)*new_capacity)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, ptr, old_capacity, new_capacity) \
    (type*) reallocate(ptr, sizeof(type) * (old_capacity), \
        sizeof(type) * (new_capacity))

#define FREE_ARRAY(type, ptr, old_capacity) \
    reallocate(ptr, sizeof(type) * (old_capacity), 0)




void* reallocate(void* ptr, size_t old_capacity, size_t new_capacity);
#endif // !breeze_memory_h
