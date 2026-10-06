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

static Value clock_native([[maybe_unused]] int32_t args_len,
                          [[maybe_unused]] Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void close_upvalues(Value *local);

static void reset_stack() {
  vm.stack_ptr = vm.stack;
  vm.frames_len = 0;
  vm.open_upvalues = NULL;
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

  // Closures that escaped (e.g. into a global) may still reference stack
  // slots that are about to be discarded; give them their current values.
  close_upvalues(vm.stack);
  reset_stack();
}

static void define_native(const char *name, NativeFn function) {
  push_stack(OBJ_VAL(copy_string(name, (int32_t)strlen(name))));
  push_stack(OBJ_VAL(new_native(function)));
  table_insert(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop_stack();
  pop_stack();
}

// Puts every VM field into its empty state without allocating, so it is safe
// to call both before setup and after teardown.
static void reset_vm_state() {
  reset_stack();

  vm.bytes_allocated = 0;
  vm.next_gc = 1024 * 1024;
  vm.objects = NULL;

  vm.gray_stack_len = 0;
  vm.gray_stack_capacity = 0;
  vm.gray_stack = NULL;

  init_table(&vm.globals);
  init_table(&vm.strings);
}

void init_vm() {
  reset_vm_state();
  define_native("clock", clock_native);
}

void free_vm() {
  free_table(&vm.globals);
  free_table(&vm.strings);
  free_objects(vm.objects);
  reset_vm_state();
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
  for (uint32_t i = 0; i < chunk->constants.len; i += 1) {
    printf("%u: ", i);
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
      // No initializers yet, so a class takes no arguments. Rejecting them
      // also keeps the stack balanced: the instance replaces the callee slot.
      if (args_len != 0) {
        runtime_error("Expected 0 arguments but got %d.", args_len);
        return false;
      }
      ObjClass *klass = (ObjClass *)AS_OBJ(callee);
      vm.stack_ptr[-1] = OBJ_VAL(new_instance(klass));
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
  CallFrame *frame;
  uint8_t *ip;

// The instruction pointer lives in a local so gcc can keep it in a register.
// frame->inst_ptr is only current after SAVE_IP(), so call SAVE_IP() before
// anything that reads it: call_value() (the new frame's caller resumes from
// it) and runtime_error() (the trace walks every frame) -- RUNTIME_ERROR()
// does the latter for you.
#define LOAD_FRAME()                                                           \
  (frame = &vm.frames[vm.frames_len - 1], ip = frame->inst_ptr)
#define SAVE_IP() (frame->inst_ptr = ip)
#define CODE() (frame->closure->function->chunk.code)

#define READ_BYTE() (ip += 1, ip[-1])
#define READ_WORD() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
// Index operand whose width is selected by the prefix opcode just read:
// OpConst -> 1 byte, OpConstLong -> 3 bytes little-endian. The long branch
// advances ip first and then reads fixed offsets, so no read is unsequenced
// with the increment.
#define READ_IDX(width_op)                                                     \
  ((width_op) == OpConst                                                       \
       ? (ip += 1, (uint32_t)ip[-1])                                           \
       : (ip += 3, (uint32_t)ip[-3] | ((uint32_t)ip[-2] << 8) |                \
                       ((uint32_t)ip[-1] << 16)))
#define READ_VALUE(idx) (frame->closure->function->chunk.constants.values[idx])
#define READ_CONSTANT(width_op) READ_VALUE(READ_IDX(width_op))
#define READ_STRING() AS_STRING(READ_CONSTANT(READ_BYTE()))

#define RUNTIME_ERROR(...)                                                     \
  do {                                                                         \
    SAVE_IP();                                                                 \
    runtime_error(__VA_ARGS__);                                                \
    return InterpretRuntimeErr;                                                \
  } while (false)

#define BINARY_OP(value_type, op)                                              \
  do {                                                                         \
    if (!IS_NUMBER(peek_stack(0)) || !IS_NUMBER(peek_stack(1))) {              \
      RUNTIME_ERROR("Operands must be numbers.");                              \
    }                                                                          \
    double right = AS_NUMBER(pop_stack());                                     \
    double left = AS_NUMBER(pop_stack());                                      \
    push_stack(value_type(left op right));                                     \
  } while (false)

  LOAD_FRAME();

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
    disassemble_inst(&frame->closure->function->chunk,
                     (uint32_t)(ip - CODE()));
#endif /* DEBUG_TRACE_EXECUTION */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
    case OpConst:
    case OpConstLong: {
      push_stack(READ_CONSTANT(inst));
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
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      break;
    }
    case OpGetGlobal: {
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&vm.globals, name, &value)) {
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      push_stack(value);
      break;
    }
    case OpSetLocal: {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = peek_stack(0);
      break;
    }
    case OpGetLocal: {
      uint8_t slot = READ_BYTE();
      push_stack(frame->frame_ptr[slot]);
      break;
    }
    case OpSetUpvalue: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek_stack(0);
      break;
    }
    case OpGetUpvalue: {
      uint8_t slot = READ_BYTE();
      push_stack(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OpDefineProperty: {
      ObjClass *klass = AS_CLASS(peek_stack(0));
      ObjString *name = READ_STRING();
      if (set_contains(&klass->fields, name)) {
        RUNTIME_ERROR("Field %s is already defined.", name->chars);
      }
      set_insert(&klass->fields, name);
      break;
    }
    case OpSetProperty: {
      if (!IS_INSTANCE(peek_stack(1))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(1));
      ObjString *name = READ_STRING();
      if (!set_contains(&instance->klass->fields, name)) {
        RUNTIME_ERROR("Undefined property '%s'.", name->chars);
      }
      table_insert(&instance->fields, name, peek_stack(0));
      Value value = pop_stack();
      pop_stack();
      push_stack(value);
      break;
    }
    case OpGetProperty: {
      if (!IS_INSTANCE(peek_stack(0))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(0));
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&instance->fields, name, &value)) {
        RUNTIME_ERROR("Undefined property '%s'", name->chars);
      }
      pop_stack();
      push_stack(value);
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
      if (IS_STRING(peek_stack(0)) && IS_STRING(peek_stack(1))) {
        concat();
      } else if (IS_NUMBER(peek_stack(0)) && IS_NUMBER(peek_stack(1))) {
        double right = AS_NUMBER(pop_stack());
        double left = AS_NUMBER(pop_stack());
        push_stack(NUMBER_VAL(left + right));
      } else {
        RUNTIME_ERROR("Operands must be two numbers or two strings.");
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
        RUNTIME_ERROR("Operand must be a number.");
      }
      push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
      break;
    }
    case OpNot: {
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
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
      uint16_t target = READ_WORD();
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(peek_stack(0))) {
        ip = CODE() + target;
      }
      break;
    }
    case OpJmpIfFalsePop: {
      uint16_t target = READ_WORD();
      Value condition = pop_stack();
      if (!IS_BOOL(condition)) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(condition)) {
        ip = CODE() + target;
      }
      break;
    }
    case OpJmp: {
      // Read into a temporary: `ip = CODE() + READ_WORD()` would modify ip
      // twice without a sequence point.
      uint16_t target = READ_WORD();
      ip = CODE() + target;
      break;
    }
    case OpCall: {
      uint8_t args_len = READ_BYTE();
      SAVE_IP();
      if (!call_value(peek_stack(args_len), args_len)) {
        return InterpretRuntimeErr;
      }
      LOAD_FRAME();
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
        uint8_t index = READ_BYTE();
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
      LOAD_FRAME();
      break;
    }
    default: {
      RUNTIME_ERROR("Unknown opcode %d.", inst);
    }
    }
  }
#undef LOAD_FRAME
#undef SAVE_IP
#undef CODE
#undef READ_BYTE
#undef READ_WORD
#undef READ_IDX
#undef READ_VALUE
#undef READ_CONSTANT
#undef READ_STRING
#undef RUNTIME_ERROR
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
