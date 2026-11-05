#include "bench.h"

static double timeLookups(Table* table, ObjString** keys, int count,
                          int reps, int* found) {
  Value value;
  *found = 0;
  double start = nowSeconds();
  for (int r = 0; r < reps; r++) {
    for (int i = 0; i < count; i++) {
      if (tableGet(table, OBJ_VAL(keys[i]), &value)) (*found)++;
    }
  }
  double elapsed = nowSeconds() - start;
  return elapsed / ((double)count * reps) * 1e9;
}

int main(void) {
  initVM();

  int sizes[] = {6144, 6145, 786432, 786433};
  int totalLookups = 8000000;

  ObjString** present = makeKeys("key", 786433);
  ObjString** missing = makeKeys("miss", 786433);

  printf("Look up present keys (hits) and absent keys (misses)\n");
  printf("%8s %9s %6s %10s %10s %12s %12s\n", "keys", "capacity", "load",
         "ns/hit", "ns/miss", "probes/hit", "probes/miss");

  for (int s = 0; s < 4; s++) {
    int count = sizes[s];
    Table table;
    initTable(&table);
    for (int i = 0; i < count; i++) {
      tableSet(&table, OBJ_VAL(present[i]), NUMBER_VAL(i));
    }

    int reps = totalLookups / count;
    if (reps < 1) reps = 1;

    int hitsFound;
    int missesFound;
    double hitTime = timeLookups(&table, present, count, reps, &hitsFound);
    double missTime = timeLookups(&table, missing, count, reps,
                                  &missesFound);

    printf("%8d %9d %6.3f %10.1f %10.1f %12.2f %12.2f\n", count,
           table.capacity, (double)table.count / table.capacity,
           hitTime, missTime,
           averageProbes(&table, present, count),
           averageProbes(&table, missing, count));

    if (hitsFound != count * reps || missesFound != 0) {
      printf("  unexpected result: %d hits, %d false hits\n",
             hitsFound, missesFound);
    }
    freeTable(&table);
  }

  free(present);
  free(missing);
  freeVM();
  return 0;
}