#include "virtual_machine.h"
#include "chunk.h"
#include "memory.h"
#include "object.h"
#include <string.h>

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif /* ifdef DEBUG_TRACE_EXECUTION */

#include "compiler.h"
#include "value.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

VirtualMachine vm;

static void reset_stack() { vm.stack_ptr = vm.stack; }

static void runtime_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t inst = vm.inst_ptr - vm.chunk->code - 1;
  uint32_t line = get_line(vm.chunk->lines, inst);
  fprintf(stderr, "[line %d] in script\n", line);
  reset_stack();
}

void init_vm() {
  reset_stack();
  vm.objects = NULL;
  init_table(&vm.strings);
}

void free_vm() {
  free_table(&vm.strings);
  free_objects();
}

void push_stack(Value value) {
  if ((vm.stack_ptr - vm.stack) < STACK_MAX) {
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

static Value peek(uint32_t distance) {
  return vm.stack_ptr[(int32_t)(-1 - distance)];
}

static void concat() {
  ObjString *right = AS_STRING(pop_stack());
  ObjString *left = AS_STRING(pop_stack());

  uint32_t len = left->len + right->len;
  char *chars = ALLOCATE(char, len + 1);
  memcpy(chars, left->chars, left->len);
  memcpy(chars + left->len, right->chars, right->len);
  chars[len] = '\0';

  ObjString *result = take_string(chars, len);
  push_stack(OBJ_VAL(result));
}

static InterpretResult run() {
  /*** MACROS DEFINITION ***/

#define READ_BYTE() (vm.inst_ptr += 1, *(vm.inst_ptr - 1))

#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

#define READ_CONSTANT_LONG()                                                   \
  ({                                                                           \
    uint32_t idx = (READ_BYTE()) | (READ_BYTE() << 8) | (READ_BYTE() << 16);   \
    vm.chunk->constants.values[idx];                                           \
  })

#define BINARY_OP(value_type, op)                                              \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      runtime_error("Operands must be numbers.");                              \
      return InterpretRuntimeErr;                                              \
    }                                                                          \
    double right = AS_NUMBER(pop_stack());                                     \
    double left = AS_NUMBER(pop_stack());                                      \
    push_stack(value_type(left op right));                                     \
  } while (false)

  /*** MACROS DEFINITION ***/

  while (true) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        ");
    for (Value *slot = vm.stack; slot < vm.stack_ptr; slot += 1) {
      printf("[ ");
      print_value(*slot);
      printf(" ]");
    }
    printf("\n");
    disassemble_inst(vm.chunk, (uint32_t)(vm.inst_ptr - vm.chunk->code));
#endif /* ifdef DEBUG_TRACE_EXECUTION                                          \
        */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
    case OpConst: {
      Value constant = READ_CONSTANT();
      push_stack(constant);
      break;
    }
    case OpConstLong: {
      Value constant = READ_CONSTANT_LONG();
      push_stack(constant);
      break;
    }
    case OpNull: {
      push_stack(NULL_VAL);
      break;
    }
    case OpTrue: {
      push_stack(BOOL_VAL(true));
      break;
    }
    case OpFalse: {
      push_stack(BOOL_VAL(false));
      break;
    }
    case OpEq: {
      Value right = pop_stack();
      Value left = pop_stack();
      push_stack(BOOL_VAL(values_equal(left, right)));
      break;
    }
    case OpLt: {
      BINARY_OP(BOOL_VAL, <);
      break;
    }
    case OpGt: {
      BINARY_OP(BOOL_VAL, >);
      break;
    }
    case OpAdd: {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concat();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        double right = AS_NUMBER(pop_stack());
        double left = AS_NUMBER(pop_stack());
        push_stack(NUMBER_VAL(left + right));
      } else {
        runtime_error("Operands must be two numbers or two strings.");
        return InterpretRuntimeErr;
      }
      break;
    }
    case OpSub: {
      BINARY_OP(NUMBER_VAL, -);
      break;
    }
    case OpMul: {
      BINARY_OP(NUMBER_VAL, *);
      break;
    }
    case OpDiv: {
      BINARY_OP(NUMBER_VAL, /);
      break;
    }
    case OpNeg: {
      if (!IS_NUMBER(peek(0))) {
        runtime_error("Operand must be a number.");
        return InterpretRuntimeErr;
      }
      push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
      break;
    }
    case OpNot: {
      if (!IS_BOOL(peek(0))) {
        runtime_error("Operand must be a boolean.");
        return InterpretRuntimeErr;
      }
      push_stack(BOOL_VAL(!AS_BOOL(pop_stack())));
      break;
    }
    case OpRet: {
      print_value(pop_stack());
      printf("\n");
      return InterpretOk;
    }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_CONSTANT_LONG
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  Chunk chunk;
  init_chunk(&chunk);
  if (!compile(source, &chunk)) {
    free_chunk(&chunk);
    return InterpretCompileErr;
  }
  vm.chunk = &chunk;
  vm.inst_ptr = vm.chunk->code;

  InterpretResult result = run();

  free_chunk(&chunk);
  return InterpretOk;
}
