#include "virtual_machine.h"
#include "debug.h"
#include "value.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

VirtualMachine vm;

void reset_stack(){
  vm.stack_ptr = vm.stack;
}

void init_vm() {
  reset_stack();
}

void free_vm() {
  vm.chunk = NULL;
  vm.inst_ptr = NULL;
}

void push_stack(Value value){
  if ((vm.stack_ptr - vm.stack) < STACK_MAX){
    *vm.stack_ptr = value;
    vm.stack_ptr += 1;
    return;
  }
  printf("Error: STACK OVERFLOW");
  exit(1);
}

Value pop_stack() {
  vm.stack_ptr -= 1;
  return *vm.stack_ptr;
}

static InterpretResult run() {
/*** MACROS DEFINITION ***/
#define READ_BYTE() (\
  vm.inst_ptr += 1, \
  *(vm.inst_ptr - 1)\
)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG()({\
  uint32_t idx = (READ_BYTE()) | (READ_BYTE() << 8) | (READ_BYTE() <<16); \
  vm.chunk->constants.values[idx];\
})
/*** MACROS DEFINITION ***/

  while (true) {
    #ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (Value* slot = vm.stack; slot < vm.stack_ptr; slot +=1) {
          printf("[ ");
          print_value(*slot);
          printf(" ]");
        }
        printf("\n");
        disassemble_inst(vm.chunk, (uint32_t) (vm.inst_ptr - vm.chunk->code));
    #endif /* ifdef DEBUG_TRACE_EXECUTION
         */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
      case OpConstantLong: {
        Value constant = READ_CONSTANT_LONG();
        push_stack(constant);
        break;
      }
      case OpConstant: {
        Value constant = READ_CONSTANT();
        push_stack(constant);
        break;
      }
      case OpNegate: {
        push_stack(- pop_stack());
        break;
      }
      case OpReturn: {
        print_value(pop_stack());
        printf("\n");
        return InterpretOk;
      }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_CONSTANT_LONG
}

InterpretResult interpret(Chunk* chunk) {
  vm.chunk = chunk;
  vm.inst_ptr = vm.chunk->code;
  return run();
}
