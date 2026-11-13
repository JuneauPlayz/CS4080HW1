#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

#define UNDEFINED_VAL       OBJ_VAL(NULL)
#define IS_UNDEFINED(value) (IS_OBJ(value) && AS_OBJ(value) == NULL)

typedef struct {
  ObjFunction* function;
  uint8_t* ip;
  int slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value* stack;
  int stackCapacity;
  Value* stackTop;
  Table globalSlots;
  ValueArray globalValues;
  ValueArray globalNames;
  Table strings;
  Obj* objects;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source);
void push(Value value);
Value pop(void);

#endif