#include "bench.h"

#define COUNT 3000
#define BUCKET_BITS 4095u
#define REPS 20

static uint32_t fnv1a(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static ObjString** makeCollidingKeys(int count, long* tried) {
  ObjString** keys = (ObjString**)malloc(sizeof(ObjString*) * count);
  char buffer[32];
  int found = 0;
  long candidate = 0;
  while (found < count) {
    int length = snprintf(buffer, sizeof(buffer), "c%ld", candidate++);
    if ((fnv1a(buffer, length) & BUCKET_BITS) == 0) {
      keys[found++] = copyString(buffer, length);
    }
  }
  *tried = candidate;
  return keys;
}

static void run(const char* name, ObjString** keys) {
  double insertTime = 0;
  double lookupTime = 0;
  double probes = 0;
  int capacity = 0;
  Value value;
  int hits = 0;

  for (int r = 0; r < REPS; r++) {
    Table table;
    initTable(&table);

    double start = nowSeconds();
    for (int i = 0; i < COUNT; i++) {
      tableSet(&table, OBJ_VAL(keys[i]), NUMBER_VAL(i));
    }
    double middle = nowSeconds();
    for (int i = 0; i < COUNT; i++) {
      if (tableGet(&table, OBJ_VAL(keys[i]), &value)) hits++;
    }
    double end = nowSeconds();

    insertTime += middle - start;
    lookupTime += end - middle;
    probes = averageProbes(&table, keys, COUNT);
    capacity = table.capacity;
    freeTable(&table);
  }

  double ops = (double)COUNT * REPS;
  printf("%-22s %9d %12.1f %12.1f %12.1f\n", name, capacity,
         insertTime / ops * 1e9, lookupTime / ops * 1e9, probes);
  if (hits != COUNT * REPS) printf("  unexpected: %d hits\n", hits);
}

int main(void) {
  initVM();

  long tried;
  ObjString** colliding = makeCollidingKeys(COUNT, &tried);
  ObjString** normal = makeKeys("key", COUNT);

  printf("%d keys; colliding keys all have hash %% 4096 == 0 "
         "(found by testing %ld candidates)\n", COUNT, tried);
  printf("%-22s %9s %12s %12s %12s\n", "key set", "capacity",
         "ns/insert", "ns/lookup", "probes/hit");
  run("ordinary keys", normal);
  run("colliding keys", colliding);

  free(colliding);
  free(normal);
  freeVM();
  return 0;
}