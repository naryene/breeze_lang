#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "virtual_machine.h"

#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif /* DEBUG_TRACE_EXECUTION */

#include "compiler.h"

VirtualMachine vm;

static Value clock_native(int32_t args_len, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void reset_stack() {
  vm.stack_ptr = vm.stack;
  vm.frames_len = 0;
}

static void runtime_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int32_t i = vm.frames_len - 1; i >= 0; i -= 1) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *function = frame->closure->function;
    size_t inst = frame->inst_ptr - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", get_line(&function->chunk.lines, inst));

    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  reset_stack();
}

static void define_native(const char *name, NativeFn function) {
  push_stack(OBJ_VAL(copy_string(name, (int32_t)strlen(name))));
  push_stack(OBJ_VAL(new_native(function)));
  table_insert(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop_stack();
  pop_stack();
}

void init_vm() {
  reset_stack();
  vm.open_upvalues = NULL;

  vm.bytes_allocated = 0;
  vm.next_gc = 1024 * 1024;
  vm.objects = NULL;

  vm.gray_stack_len = 0;
  vm.gray_stack_capacity = 0;
  vm.gray_stack = NULL;

  init_table(&vm.globals);
  init_table(&vm.strings);
  define_native("clock", clock_native);
}

void free_vm() {
  free_table(&vm.globals);
  free_table(&vm.strings);
  free_objects(vm.objects);
  init_vm();
}

void push_stack(Value value) {
  if ((vm.stack_ptr - vm.stack) < STACK_MAX) {
    *vm.stack_ptr = value;
    vm.stack_ptr += 1;
    return;
  }
  runtime_error("Stack overflow.");
  exit(1);
}

Value pop_stack() {
  vm.stack_ptr -= 1;
  return *vm.stack_ptr;
}

// Peeks at a `Value` in the VM stack.
static Value peek_stack(uint32_t distance) {
  return vm.stack_ptr[(int32_t)(-1 - distance)];
}

#ifdef DEBUG_TRACE_EXECUTION
void print_constants(const Chunk *chunk) {
  printf("Constants:\n");
  for (int i = 0; i < chunk->constants.len; i++) {
    printf("%d: ", i);
    print_value(chunk->constants.values[i]);
    printf("\n");
  }
}
#endif

static bool call(ObjClosure *closure, uint8_t args_len) {
  ObjFunction *function = closure->function;

#ifdef DEBUG_TRACE_EXECUTION
  print_constants(&function->chunk);
#endif
  if (args_len != function->arity) {
    runtime_error("Expected %d arguments but got %d.", function->arity,
                  args_len);
    return false;
  }

  if (vm.frames_len == FRAMES_MAX) {
    runtime_error("Stack overflow.");
    return false;
  }
  CallFrame *frame = &vm.frames[vm.frames_len];
  vm.frames_len += 1;
  frame->closure = closure;
  frame->inst_ptr = function->chunk.code;
  frame->frame_ptr = vm.stack_ptr - args_len - 1;
  return true;
}

static bool call_value(Value callee, uint8_t args_len) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case ObjClassType: {
      ObjClass *klass = (ObjClass *)AS_OBJ(callee);
      vm.stack_ptr[-(args_len + 1)] = OBJ_VAL(new_instance(klass));
      return true;
    }
    case ObjClosureType: {
      return call(AS_CLOSURE(callee), args_len);
    }
    case ObjNativeType: {
      NativeFn native = AS_NATIVE(callee);
      Value result = native(args_len, vm.stack_ptr - args_len);
      vm.stack_ptr -= args_len + 1;
      push_stack(result);
      return true;
    }
    default:
      break;
    }
  }
  runtime_error("Can only call functions and classes.");
  return false;
}

static ObjUpvalue *capture_upvalue(Value *local) {
  ObjUpvalue **upvalue_pptr = &vm.open_upvalues;
  while (*upvalue_pptr != NULL && (*upvalue_pptr)->location > local) {
    upvalue_pptr = &(*upvalue_pptr)->next;
  }

  if (*upvalue_pptr != NULL && (*upvalue_pptr)->location == local) {
    return *upvalue_pptr;
  }

  ObjUpvalue *created_upvalue = new_upvalue(local);
  created_upvalue->next = *upvalue_pptr;
  *upvalue_pptr = created_upvalue;

  return created_upvalue;
}

static void close_upvalues(Value *local) {
  while (vm.open_upvalues != NULL && vm.open_upvalues->location >= local) {
    ObjUpvalue *upvalue = vm.open_upvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.open_upvalues = upvalue->next;
  }
}

static void define_method(ObjString *name){
  Value method = peek_stack(0);
  ObjClass *klass = AS_CLASS(peek_stack(1));
  table_insert(&klass->methods, name, method);
  pop_stack();
}

static InterpretResult check_bool(Value value) {
  if (!IS_BOOL(value)) {
    runtime_error("Operand must be a boolean.");
    return InterpretRuntimeErr;
  }
  return InterpretOk;
}

static void concat() {
  ObjString *right = AS_STRING(peek_stack(0));
  ObjString *left = AS_STRING(peek_stack(1));

  uint32_t len = left->len + right->len;
  char *chars = ALLOCATE(char, len + 1);
  memcpy(chars, left->chars, left->len);
  memcpy(chars + left->len, right->chars, right->len);
  chars[len] = '\0';

  ObjString *result = take_string(chars, len);
  pop_stack();
  pop_stack();
  push_stack(OBJ_VAL(result));
}

static InterpretResult run() {
  /*** MACROS DEFINITION ***/
  CallFrame *frame = &vm.frames[vm.frames_len - 1];

#define READ_BYTE() (frame->inst_ptr += 1, *(frame->inst_ptr - 1))
#define READ_VALUE(idx) (frame->closure->function->chunk.constants.values[idx])

#define READ_WORD()                                                            \
  (frame->inst_ptr += 2,                                                       \
   (uint16_t)(frame->inst_ptr[-2] | (frame->inst_ptr[-1] << 8)))

#define READ_IDX(inst)                                                         \
  ({                                                                           \
    uint32_t idx;                                                              \
    if (inst == OpConst) {                                                     \
      idx = (uint32_t)READ_BYTE();                                             \
    } else {                                                                   \
      idx = (uint32_t)((READ_BYTE()) | (READ_BYTE() << 8) |                    \
                       (READ_BYTE() << 16));                                   \
    }                                                                          \
    idx;                                                                       \
  })

#define READ_CONSTANT(inst) (READ_VALUE(READ_IDX(inst)))

#define READ_STRING()                                                          \
  ({                                                                           \
    uint32_t idx = READ_IDX(READ_BYTE());                                      \
    Value constant = READ_VALUE(idx);                                          \
    AS_STRING(constant);                                                       \
  })

#define BINARY_OP(value_type, op)                                              \
  do {                                                                         \
    if (!IS_NUMBER(peek_stack(0)) || !IS_NUMBER(peek_stack(1))) {              \
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
    for (Value *stack_slot = vm.stack; stack_slot < vm.stack_ptr;
         stack_slot += 1) {
      printf("[ ");
      print_value(*stack_slot);
      printf(" ]");
    }
    printf("\n");
    disassemble_inst(
        &frame->closure->function->chunk,
        (uint32_t)(frame->inst_ptr - frame->closure->function->chunk.code));
#endif /* DEBUG_TRACE_EXECUTION */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
    case OpConst:
    case OpConstLong: {
      Value constant = READ_CONSTANT(inst);
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
    case OpDefineGlobal: {
      ObjString *name = READ_STRING();
      table_insert(&vm.globals, name, peek_stack(0));
      pop_stack();
      break;
    }
    case OpSetGlobal: {
      ObjString *name = READ_STRING();
      if (table_insert(&vm.globals, name, peek_stack(0))) {
        table_remove(&vm.globals, name);
        runtime_error("Undefined variable '%s'.", name->chars);
        return InterpretRuntimeErr;
      }
      break;
    }
    case OpGetGlobal: {
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&vm.globals, name, &value)) {
        runtime_error("Undefined variable '%s'.", name->chars);
        return InterpretRuntimeErr;
      }
      push_stack(value);
      break;
    }
    case OpSetLocal: {
      uint32_t local_stack_idx = READ_IDX(READ_BYTE());
      frame->frame_ptr[local_stack_idx] = peek_stack(0);
      break;
    }
    case OpGetLocal: {
      uint32_t local_stack_idx = READ_IDX(READ_BYTE());
      push_stack(frame->frame_ptr[local_stack_idx]);
      break;
    }
    case OpSetUpvalue: {
      uint32_t upvalue_idx = READ_IDX(READ_BYTE());
      *frame->closure->upvalues[upvalue_idx]->location = peek_stack(0);
      break;
    }
    case OpGetUpvalue: {
      uint32_t upvalue_idx = READ_IDX(READ_BYTE());
      push_stack(*frame->closure->upvalues[upvalue_idx]->location);
      break;
    }
    case OpDefineProperty: {
      ObjClass *klass = AS_CLASS(peek_stack(0));
      ObjString *name = READ_STRING();

      if (set_contains(&klass->fields, name)) {
        runtime_error("Field %s is already defined.", name->chars);
        return InterpretRuntimeErr;
      }
      set_insert(&klass->fields, name);


      break;
    }
    case OpSetProperty: {
      if (!IS_INSTANCE(peek_stack(1))) {
        runtime_error("Properties are defined for instances only.");
        return InterpretRuntimeErr;
      }

      ObjInstance *instance = AS_INSTANCE(peek_stack(1));
      ObjString *name = READ_STRING();

      if (!set_contains(&instance->klass->fields, name)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return InterpretRuntimeErr;
      }

      table_insert(&instance->fields, name, peek_stack(0));
      Value value = pop_stack();
      pop_stack();
      push_stack(value);
      break;
    }
    case OpGetProperty: {
      if (!IS_INSTANCE(peek_stack(0))) {
        runtime_error("Properties are defined for instances only.");
        return InterpretRuntimeErr;
      }

      ObjInstance *instance = AS_INSTANCE(peek_stack(0));
      ObjString *name = READ_STRING();

      Value value;
      if (table_get(&instance->fields, name, &value)) {
        pop_stack();
        push_stack(value);
        break;
      }
      runtime_error("Undefined property '%s'", name->chars);
      return InterpretRuntimeErr;
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
      if (IS_STRING(peek_stack(0)) && IS_STRING(peek_stack(1))) {
        concat();
      } else if (IS_NUMBER(peek_stack(0)) && IS_NUMBER(peek_stack(1))) {
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
      if (!IS_NUMBER(peek_stack(0))) {
        runtime_error("Operand must be a number.");
        return InterpretRuntimeErr;
      }
      push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
      break;
    }
    case OpNot: {
      InterpretResult check_result = check_bool(peek_stack(0));
      if (check_result == InterpretRuntimeErr) {
        return check_result;
      }

      push_stack(BOOL_VAL(!AS_BOOL(pop_stack())));
      break;
    }
    case OpPrint: {
      print_value(pop_stack());
      printf("\n");
      break;
    }
    case OpPop: {
      pop_stack();
      break;
    }
    case OpJmpIfFalse: {
      uint16_t offset = READ_WORD();
      InterpretResult check_result = check_bool(peek_stack(0));
      if (check_result == InterpretRuntimeErr) {
        return check_result;
      }
      if (AS_BOOL(peek_stack(0)) == false) {
        frame->inst_ptr = frame->closure->function->chunk.code + offset;
      }
      break;
    }
    case OpJmp: {
      uint16_t offset = READ_WORD();
      frame->inst_ptr = frame->closure->function->chunk.code + offset;
      break;
    }
    case OpCall: {
      uint8_t args_len = READ_BYTE();
      if (!call_value(peek_stack(args_len), args_len)) {
        return InterpretRuntimeErr;
      }
      frame = &vm.frames[vm.frames_len - 1];
      break;
    }
      case OpMethod: {
        define_method(READ_STRING());
        break;
      }
    case OpClosure: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT(READ_BYTE()));
      ObjClosure *closure = new_closure(function);
      push_stack(OBJ_VAL(closure));
      for (uint32_t i = 0; i < closure->upvalues_len; i += 1) {
        uint8_t is_local = READ_BYTE();
        uint32_t index = READ_IDX(READ_BYTE());
        if (is_local) {
          closure->upvalues[i] = capture_upvalue(frame->frame_ptr + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OpCloseUpvalue: {
      close_upvalues(vm.stack_ptr - 1);
      pop_stack();
      break;
    }
    case OpClass: {
      push_stack(OBJ_VAL(new_class(READ_STRING())));
      break;
    }
    case OpRet: {
      Value result = pop_stack();
      close_upvalues(frame->frame_ptr);
      vm.frames_len -= 1;
      if (vm.frames_len == 0) {
        pop_stack();
        return InterpretOk;
      }
      vm.stack_ptr = frame->frame_ptr;
      push_stack(result);
      frame = &vm.frames[vm.frames_len - 1];
      break;
    }
    }
  }
#undef READ_BYTE
#undef READ_WORD
#undef READ_CONSTANT
#undef READ_CONSTANT_LONG
#undef READ_IDX
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  ObjFunction *function = compile(source);
  if (function == NULL) {
    return InterpretCompileErr;
  }

  push_stack(OBJ_VAL(function));
  ObjClosure *closure = new_closure(function);
  pop_stack();
  push_stack(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
