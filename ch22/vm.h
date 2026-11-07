#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define UNDEFINED_VAL       OBJ_VAL(NULL)
#define IS_UNDEFINED(value) (IS_OBJ(value) && AS_OBJ(value) == NULL)

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value* stack;
  int stackCapacity;
  Value* stackTop;
  Table globalSlots;
  ValueArray globalValues;
  ValueArray globalNames;
  ValueArray globalConsts;
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