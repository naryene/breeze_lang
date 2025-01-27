#ifndef breeze_memory_h
#define breeze_memory_h

#include <stdlib.h>

#include "common.h"
#include "object.h"
#include "value.h"

/* Factor by which array capacity grows when resizing */
#define ARRAY_GROWTH_FACTOR 2

/* Allocates memory for a new array
 * @param type: Type of elements in the array
 * @param new_capacity: Number of elements to allocate
 * @return: Pointer to the newly allocated array
 */
#define ALLOCATE(type, new_capacity)                                           \
  (type *)reallocate(NULL, 0, sizeof(type) * new_capacity)

/* Calculates the new capacity when growing an array
 * @param capacity: Current capacity
 * @return: New capacity (minimum 8, or double the current)
 */
#define GROW_CAPACITY(capacity)                                                \
  ((capacity) < 8 ? 8 : (capacity) * ARRAY_GROWTH_FACTOR)

/* Resizes an array to a new capacity
 * @param type: Type of elements in the array
 * @param ptr: Pointer to the current array
 * @param old_capacity: Current max number of elements
 * @param new_capacity: Desired max number of elements
 * @return: Pointer to the resized array
 */
#define GROW_ARRAY(type, ptr, old_capacity, new_capacity)                      \
  (type *)reallocate(ptr, sizeof(type) * (old_capacity),                       \
                     sizeof(type) * (new_capacity))

/* Frees an array from memory
 * @param type: Type of elements in the array
 * @param ptr: Pointer to the array
 * @param old_capacity: Current max number of elements
 */
#define FREE_ARRAY(type, ptr, old_capacity)                                    \
  reallocate(ptr, sizeof(type) * (old_capacity), 0)

/* Frees a single object from memory
 * @param type: Type of the object
 * @param ptr: Pointer to the object
 */
#define FREE(type, ptr) reallocate(ptr, sizeof(type), 0)

/* Reallocates memory block to a new size
 * @param ptr: Pointer to the current memory block
 * @param old_capacity: Current size in bytes
 * @param new_capacity: Desired size in bytes
 * @return: Pointer to the reallocated memory block
 */
void *reallocate(void *ptr, size_t old_capacity, size_t new_capacity);

/* Marks an object as reachable in the garbage collector
 * @param object: Pointer to the object to mark
 */
void mark_object(Obj *obj);

/* Marks a value as reachable in the garbage collector
 * @param value: Value to mark
 */
void mark_value(Value value);

/* Runs the garbage collector to free unreachable objects */
void collect_garbage();

/* Frees all objects in a linked list
 * @param object: Pointer to the first object in the list
 */
void free_objects(Obj *object);

#endif // !breeze_memory_h
