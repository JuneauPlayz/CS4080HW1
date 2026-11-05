#include <stdio.h>
#include <string.h>

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

static int failures = 0;

static Value str(const char* chars) {
  return OBJ_VAL(copyString(chars, (int)strlen(chars)));
}

static void printKey(Value key) {
  switch (key.type) {
    case VAL_BOOL:   printf("bool   "); break;
    case VAL_NIL:    printf("nil    "); break;
    case VAL_NUMBER: printf("number "); break;
    case VAL_OBJ:    printf("string "); break;
  }
  if (IS_STRING(key)) {
    printf("\"");
    printValue(key);
    printf("\"");
  } else {
    printValue(key);
  }
}

static void expectFound(Table* table, Value key, Value expected) {
  Value actual;
  bool found = tableGet(table, key, &actual);
  bool ok = found && valuesEqual(actual, expected);
  if (!ok) failures++;

  printf("  get ");
  printKey(key);
  printf(" -> ");
  if (found) {
    printValue(actual);
  } else {
    printf("(not found)");
  }
  printf("   %s\n", ok ? "ok" : "FAIL");
}

static void expectMissing(Table* table, Value key) {
  Value actual;
  bool found = tableGet(table, key, &actual);
  if (found) failures++;

  printf("  get ");
  printKey(key);
  printf(" -> %s   %s\n", found ? "(found)" : "(not found)",
         found ? "FAIL" : "ok");
}

int main(void) {
  initVM();

  Table table;
  initTable(&table);

  printf("Insert one key of every type:\n");
  tableSet(&table, NUMBER_VAL(1), str("number one"));
  tableSet(&table, NUMBER_VAL(2.5), str("number two and a half"));
  tableSet(&table, BOOL_VAL(true), str("boolean true"));
  tableSet(&table, BOOL_VAL(false), str("boolean false"));
  tableSet(&table, NIL_VAL, str("nil"));
  tableSet(&table, str("1"), str("string one"));
  tableSet(&table, NUMBER_VAL(0), str("number zero"));

  printf("Read every key back:\n");
  expectFound(&table, NUMBER_VAL(1), str("number one"));
  expectFound(&table, NUMBER_VAL(2.5), str("number two and a half"));
  expectFound(&table, BOOL_VAL(true), str("boolean true"));
  expectFound(&table, BOOL_VAL(false), str("boolean false"));
  expectFound(&table, NIL_VAL, str("nil"));
  expectFound(&table, str("1"), str("string one"));
  expectFound(&table, NUMBER_VAL(0), str("number zero"));

  printf("Keys that look alike stay distinct, and -0 equals 0:\n");
  expectFound(&table, NUMBER_VAL(-0.0), str("number zero"));
  expectMissing(&table, NUMBER_VAL(2));
  expectMissing(&table, str("2.5"));

  printf("Overwrite nil, delete true, then read the rest back:\n");
  bool isNew = tableSet(&table, NIL_VAL, str("nil again"));
  printf("  set nil returned isNewKey = %s   %s\n",
         isNew ? "true" : "false", isNew ? "FAIL" : "ok");
  if (isNew) failures++;
  bool deleted = tableDelete(&table, BOOL_VAL(true));
  printf("  delete true -> %s   %s\n", deleted ? "deleted" : "missing",
         deleted ? "ok" : "FAIL");
  if (!deleted) failures++;
  expectMissing(&table, BOOL_VAL(true));
  expectFound(&table, BOOL_VAL(false), str("boolean false"));
  expectFound(&table, NIL_VAL, str("nil again"));
  expectFound(&table, NUMBER_VAL(1), str("number one"));
  expectFound(&table, NUMBER_VAL(2.5), str("number two and a half"));
  expectFound(&table, str("1"), str("string one"));
  expectFound(&table, NUMBER_VAL(0), str("number zero"));

  printf("Re-insert the deleted key:\n");
  tableSet(&table, BOOL_VAL(true), str("true is back"));
  expectFound(&table, BOOL_VAL(true), str("true is back"));

  printf("Insert 1000 number keys and read them all back:\n");
  int wrong = 0;
  for (int i = 0; i < 1000; i++) {
    tableSet(&table, NUMBER_VAL(i * 0.5 + 100), NUMBER_VAL(i));
  }
  for (int i = 0; i < 1000; i++) {
    Value value;
    if (!tableGet(&table, NUMBER_VAL(i * 0.5 + 100), &value) ||
        AS_NUMBER(value) != i) {
      wrong++;
    }
  }
  if (wrong > 0) failures++;
  printf("  %d wrong, count = %d, capacity = %d   %s\n",
         wrong, table.count, table.capacity, wrong == 0 ? "ok" : "FAIL");

  printf("String interning still works:\n");
  ObjString* a = copyString("hello", 5);
  ObjString* b = copyString("hello", 5);
  printf("  copyString(\"hello\") twice gives the same object: %s   %s\n",
         a == b ? "yes" : "no", a == b ? "ok" : "FAIL");
  if (a != b) failures++;

  printf("\n%d failure(s)\n", failures);

  freeTable(&table);
  freeVM();
  return failures == 0 ? 0 : 1;
}