#include "virtual_machine.h"
#include "debug.h"
#include "value.h"
#include <stdint.h>
#include <stdio.h>
VirtualMachine vm;

void init_vm() {
  vm.chunk = NULL;
  vm.inst_ptr = NULL;
}

void free_vm() {
  vm.chunk = NULL;
  vm.inst_ptr = NULL;
}

static InterpretResult run() {
#define READ_BYTE() (\
  vm.inst_ptr += 1, \
  *(vm.inst_ptr - 1)\
)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG()({\
  uint32_t idx = (READ_BYTE()) | (READ_BYTE() << 8) | (READ_BYTE() <<16); \
  vm.chunk->constants.values[idx];\
})

  while (true) {
    #ifdef DEBUG_TRACE_EXECUTION
        disassemble_inst(vm.chunk, (uint32_t) (vm.inst_ptr - vm.chunk->code));
    #endif /* ifdef DEBUG_TRACE_EXECUTION
         */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
      case OpConstantLong: {
        Value constant = READ_CONSTANT_LONG();
        print_value(constant);
        printf("\n");
        break;
      }
      case OpConstant: {
        Value constant = READ_CONSTANT();
        print_value(constant);
        printf("\n");
        break;
      }
      case OpReturn: {
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
