#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjString* allocateString(int length) {
  ObjString* string = (ObjString*)allocateObject(
      sizeof(ObjString) + length + 1, OBJ_STRING);
  string->length = length;
  string->ownsChars = true;
  string->chars = string->data;
  string->data[length] = '\0';
  return string;
}

ObjString* copyString(const char* chars, int length) {
  ObjString* string = allocateString(length);
  memcpy(string->data, chars, length);
  return string;
}

ObjString* constantString(const char* chars, int length) {
  ObjString* string = (ObjString*)allocateObject(sizeof(ObjString),
                                                 OBJ_STRING);
  string->length = length;
  string->ownsChars = false;
  string->chars = chars;
  return string;
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%.*s", AS_STRING(value)->length, AS_CSTRING(value));
      break;
  }
}