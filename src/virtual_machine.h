#ifndef breeze_virtual_machine_h
#define breeze_virtual_machine_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk *chunk;
  uint8_t *inst_ptr;
  Value stack[STACK_MAX];
  Value *stack_ptr;
  Table globals;
  Table strings;
  Obj *objects;
} VirtualMachine;

typedef enum {
  InterpretOk,
  InterpretCompileErr,
  InterpretRuntimeErr,
} InterpretResult;

extern VirtualMachine vm;

void init_vm();
void free_vm();
InterpretResult interpret(const char *source);
void push_stack(Value value);
Value pop_stack();

#endif // !breeze_virtual_machine_h
