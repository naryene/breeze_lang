#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "memory.h"

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "virtual_machine.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif /* ifdef DEBUG_LOG_GC */

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *ptr, size_t old_capacity, size_t new_capacity) {
  vm.bytes_allocated += new_capacity - old_capacity;
  if (new_capacity > old_capacity) {
#ifdef DEBUG_STRESS_GC
    collect_garbage();
#endif /* ifdef DEBUG_STRESS_GC */
  }

  if (vm.bytes_allocated > vm.next_gc) {
    collect_garbage();
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

void mark_object(Obj *object) {
  if (object == NULL) {
    return;
  }
  if (object->is_marked == true) {
    return;
  }
#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void *)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif // ifdef DEBUG_LOG_GC

  object->is_marked = true;

  if (vm.gray_stack_capacity < vm.gray_stack_len + 1) {
    vm.gray_stack_capacity = GROW_CAPACITY(vm.gray_stack_capacity);
    vm.gray_stack =
        (Obj **)realloc(vm.gray_stack, sizeof(Obj *) * vm.gray_stack_capacity);

    if (vm.gray_stack == NULL) {
      fprintf(stderr, "Not enough memory for `gray_stack` allocation.");
      exit(1);
    }
  }

  vm.gray_stack[vm.gray_stack_len] = object;
  vm.gray_stack_len += 1;
}

void mark_value(Value value) {
  if (IS_OBJ(value)) {
    mark_object(AS_OBJ(value));
  }
}

void mark_vec(ValueVec *vector) {
  for (uint32_t i = 0; i < vector->len; i += 1) {
    mark_value(vector->values[i]);
  }
}

static void blacken_object(Obj *object) {

#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void *)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif /* ifdef DEBUG_LOG_GC */

  switch (object->type) {
  case ObjInstanceType: {
    ObjInstance *instance = (ObjInstance *)object;
    mark_object((Obj *)instance->klass);
    mark_table(&instance->fields);
    break;
  }
  case ObjClassType: {
    ObjClass *klass = (ObjClass *)object;
    mark_object((Obj *)klass->name);
    mark_table(&klass->methods);
    mark_set(&klass->fields);
    break;
  }

  case ObjClosureType: {
    ObjClosure *closure = (ObjClosure *)object;
    mark_object((Obj *)closure->function);
    for (uint32_t i = 0; i < closure->upvalues_len; i += 1) {
      mark_object((Obj *)closure->upvalues[i]);
    }
    break;
  }

  case ObjFunctionType: {
    ObjFunction *function = (ObjFunction *)object;
    mark_object((Obj *)function->name);
    mark_vec(&function->chunk.constants);
    break;
  }

  case ObjUpvalueType: {
    mark_value(((ObjUpvalue *)object)->closed);
    break;
  }

  case ObjNativeType:
  case ObjStringType:
    break;
  }
}

static void free_object(Obj *object) {
  switch (object->type) {
  case ObjInstanceType: {
    ObjInstance *instance = (ObjInstance *)object;
    free_table(&instance->fields);
    FREE(ObjInstance, object);
    break;
  }
  case ObjClassType: {
    ObjClass *klass = (ObjClass *)object;
    free_table(&klass->methods);
    free_set(&klass->fields);
    FREE(ObjClass, object);
    break;
  }
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
  for (Value *stack_slot = vm.stack; stack_slot < vm.stack_ptr;
       stack_slot += 1) {
    mark_value(*stack_slot);
  }

  for (uint32_t frame_idx = 0; frame_idx < vm.frames_len; frame_idx += 1) {
    mark_object((Obj *)vm.frames[frame_idx].closure);
  }

  for (ObjUpvalue *upvalue = vm.open_upvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    mark_object((Obj *)upvalue);
  }

  mark_table(&vm.globals);
  mark_compiler_roots();
}

static void trace_references() {
  while (vm.gray_stack_len > 0) {
    vm.gray_stack_len -= 1;
    Obj *object = vm.gray_stack[vm.gray_stack_len];
    blacken_object(object);
  }
}

static void sweep() {
  Obj *previous = NULL;
  Obj *object = vm.objects;
  while (object != NULL) {
    if (object->is_marked) {
      object->is_marked = false;
      previous = object;
      object = object->next;
    } else {
      Obj *unreachable = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      free_object(unreachable);
    }
  }
}

void collect_garbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t bytes_allocated_before = vm.bytes_allocated;
#endif /* ifdef DEBUG_LOG_GC*/

  mark_roots();
  trace_references();
  table_remove_white(&vm.strings);
  sweep();

  vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         bytes_allocated_before - vm.bytes_allocated, bytes_allocated_before,
         vm.bytes_allocated, vm.next_gc);
#endif /* ifdef DEBUG_LOG_GC*/
}

void free_objects(Obj *object) {
  while (object != NULL) {
    Obj *next = object->next;
    free_object(object);
    object = next;
  }
  free(vm.gray_stack);
}
