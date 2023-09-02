#ifndef breeze_virtual_machine_h
#define breeze_virtual_machine_h

#include "chunk.h"

typedef struct {
  Chunk* chunk;
  uint8_t* inst_ptr;
} VirtualMachine;

typedef enum {
  InterpretOk,
  InterpretCompileErr,
  InterpretRuntimeErr,
} InterpretResult;

void init_vm();
void free_vm();
InterpretResult interpret(Chunk* chunk);

#endif // !breeze_virtual_machine_h
