#ifndef breeze_memory_h
#define breeze_memory_h

#include "common.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, ptr, old_count, new_count) \
    (type*) reallocate(ptr, sizeof(type) * (old_count), \
        sizeof(type) * (new_count))

#define FREE_ARRAY(type, ptr, old_count) \
    reallocate(ptr, sizeof(type) * (old_count), 0)



void* reallocate(void* ptr, size_t old_size, size_t new_size);
#endif // !breeze_memory_h
