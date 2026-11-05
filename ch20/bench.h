#ifndef clox_bench_h
#define clox_bench_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "object.h"
#include "table.h"
#include "vm.h"

#define IS_EMPTY_KEY(value) (IS_OBJ(value) && AS_OBJ(value) == NULL)

static inline double nowSeconds(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline ObjString* makeKey(const char* prefix, int i) {
  char buffer[32];
  int length = snprintf(buffer, sizeof(buffer), "%s%d", prefix, i);
  return copyString(buffer, length);
}

static inline ObjString** makeKeys(const char* prefix, int count) {
  ObjString** keys = (ObjString**)malloc(sizeof(ObjString*) * count);
  for (int i = 0; i < count; i++) {
    keys[i] = makeKey(prefix, i);
  }
  return keys;
}

static inline double averageProbes(Table* table, ObjString** keys,
                                   int count) {
  long long probes = 0;
  for (int i = 0; i < count; i++) {
    uint32_t index = keys[i]->hash % table->capacity;
    for (;;) {
      probes++;
      Entry* entry = &table->entries[index];
      if (IS_EMPTY_KEY(entry->key)) {
        if (IS_NIL(entry->value)) break;
      } else if (AS_OBJ(entry->key) == (Obj*)keys[i]) {
        break;
      }
      index = (index + 1) % table->capacity;
    }
  }
  return (double)probes / count;
}

static inline int countTombstones(Table* table) {
  int tombstones = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (IS_EMPTY_KEY(entry->key) && !IS_NIL(entry->value)) tombstones++;
  }
  return tombstones;
}

#endif