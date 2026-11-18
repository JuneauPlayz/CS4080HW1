#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack(void) {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

static Value sqrtNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    runtimeError("Argument to sqrt() must be a number.");
    return UNDEFINED_VAL;
  }

  double n = AS_NUMBER(args[0]);
  if (n < 0) {
    runtimeError("Can't take sqrt() of a negative number.");
    return UNDEFINED_VAL;
  }

  return NUMBER_VAL(sqrt(n));
}

static Value floorNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    runtimeError("Argument to floor() must be a number.");
    return UNDEFINED_VAL;
  }

  return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value lenNative(int argCount, Value* args) {
  if (!IS_STRING(args[0])) {
    runtimeError("Argument to len() must be a string.");
    return UNDEFINED_VAL;
  }

  return NUMBER_VAL((double)AS_STRING(args[0])->length);
}

static Value strNative(int argCount, Value* args) {
  Value value = args[0];
  char buffer[64];

  if (IS_STRING(value)) return value;
  if (IS_NUMBER(value)) {
    snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value));
  } else if (IS_BOOL(value)) {
    snprintf(buffer, sizeof(buffer), "%s",
             AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    snprintf(buffer, sizeof(buffer), "nil");
  } else if (IS_CLOSURE(value) || IS_FUNCTION(value)) {
    ObjFunction* function = IS_CLOSURE(value)
        ? AS_CLOSURE(value)->function : AS_FUNCTION(value);
    ObjString* name = function->name;
    if (name == NULL) {
      snprintf(buffer, sizeof(buffer), "<script>");
    } else {
      snprintf(buffer, sizeof(buffer), "<fn %.50s>", name->chars);
    }
  } else {
    snprintf(buffer, sizeof(buffer), "<native fn>");
  }

  return OBJ_VAL(copyString(buffer, (int)strlen(buffer)));
}

static Value substrNative(int argCount, Value* args) {
  if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) ||
      !IS_NUMBER(args[2])) {
    runtimeError("substr() takes a string and two numbers.");
    return UNDEFINED_VAL;
  }

  ObjString* string = AS_STRING(args[0]);
  double start = AS_NUMBER(args[1]);
  double end = AS_NUMBER(args[2]);
  if (start != floor(start) || end != floor(end) ||
      start < 0 || end < start || end > string->length) {
    runtimeError("substr() range %g to %g is out of bounds for length %d.",
                 start, end, string->length);
    return UNDEFINED_VAL;
  }

  return OBJ_VAL(copyString(string->chars + (int)start,
                            (int)(end - start)));
}

static void defineNative(const char* name, NativeFn function,
                         int arity) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function, arity)));
  tableSet(&vm.globalSlots, AS_STRING(vm.stack[0]),
           NUMBER_VAL((double)vm.globalValues.count));
  writeValueArray(&vm.globalValues, vm.stack[1]);
  writeValueArray(&vm.globalNames, vm.stack[0]);
  pop();
  pop();
}

void initVM(void) {
  vm.stack = NULL;
  vm.stackCapacity = 0;
  resetStack();
  vm.objects = NULL;

  initTable(&vm.globalSlots);
  initValueArray(&vm.globalValues);
  initValueArray(&vm.globalNames);
  initTable(&vm.strings);

  defineNative("clock", clockNative, 0);
  defineNative("sqrt", sqrtNative, 1);
  defineNative("floor", floorNative, 1);
  defineNative("len", lenNative, 1);
  defineNative("str", strNative, 1);
  defineNative("substr", substrNative, 3);
}

void freeVM(void) {
  freeTable(&vm.globalSlots);
  freeValueArray(&vm.globalValues);
  freeValueArray(&vm.globalNames);
  freeTable(&vm.strings);
  freeObjects();
  FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
  vm.stack = NULL;
  vm.stackCapacity = 0;
  resetStack();
}

void push(Value value) {
  int count = (int)(vm.stackTop - vm.stack);
  if (vm.stackCapacity < count + 1) {
    Value* oldStack = vm.stack;
    int oldCapacity = vm.stackCapacity;
    vm.stackCapacity = GROW_CAPACITY(oldCapacity);
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stackCapacity);
    vm.stackTop = vm.stack + count;

    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL;
         upvalue = upvalue->next) {
      upvalue->location = vm.stack + (upvalue->location - oldStack);
    }
  }

  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop(void) {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",
        closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = (int)(vm.stackTop - vm.stack) - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        ObjNative* native = (ObjNative*)AS_OBJ(callee);
        if (argCount != native->arity) {
          runtimeError("Expected %d arguments but got %d.",
              native->arity, argCount);
          return false;
        }

        Value result = native->function(argCount,
                                        vm.stackTop - argCount);
        if (IS_UNDEFINED(result)) return false;

        vm.stackTop -= argCount + 1;
        push(result);
        return true;
      }
      default:
        break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL &&
         vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
}

static InterpretResult run(void) {
  CallFrame* frame;
  register uint8_t* ip;

#define LOAD_FRAME() \
    (frame = &vm.frames[vm.frameCount - 1], ip = frame->ip)
#define STORE_IP() (frame->ip = ip)
#define READ_BYTE() (*ip++)
#define READ_SHORT() \
    (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        STORE_IP(); \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  LOAD_FRAME();

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(&frame->closure->function->chunk,
        (int)(ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP: pop(); break;
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(vm.stack[frame->slots + slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        vm.stack[frame->slots + slot] = peek(0);
        break;
      }
      case OP_GET_GLOBAL: {
        uint8_t slot = READ_BYTE();
        Value value = vm.globalValues.values[slot];
        if (IS_UNDEFINED(value)) {
          STORE_IP();
          runtimeError("Undefined variable '%s'.",
                       AS_CSTRING(vm.globalNames.values[slot]));
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        vm.globalValues.values[READ_BYTE()] = peek(0);
        pop();
        break;
      }
      case OP_SET_GLOBAL: {
        uint8_t slot = READ_BYTE();
        if (IS_UNDEFINED(vm.globalValues.values[slot])) {
          STORE_IP();
          runtimeError("Undefined variable '%s'.",
                       AS_CSTRING(vm.globalNames.values[slot]));
          return INTERPRET_RUNTIME_ERROR;
        }
        vm.globalValues.values[slot] = peek(0);
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          STORE_IP();
          runtimeError(
              "Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          STORE_IP();
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
#ifdef IN_PLACE_NEGATE
        vm.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm.stackTop[-1]));
#else
        push(NUMBER_VAL(-AS_NUMBER(pop())));
#endif
        break;
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        STORE_IP();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        LOAD_FRAME();
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] =
                captureUpvalue(vm.stack + frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        break;
      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(vm.stack + frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        vm.stackTop = vm.stack + frame->slots;
        push(result);
        LOAD_FRAME();
        break;
      }
    }
  }

#undef LOAD_FRAME
#undef STORE_IP
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
