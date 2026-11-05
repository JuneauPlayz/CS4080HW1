#include "bench.h"

#define LIVE 100000
#define ROUNDS 10

int main(void) {
  initVM();

  ObjString** batches[ROUNDS + 1];
  for (int r = 0; r <= ROUNDS; r++) {
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "r%d_", r);
    batches[r] = makeKeys(prefix, LIVE);
  }
  ObjString** missing = makeKeys("miss", LIVE);

  Table table;
  initTable(&table);
  for (int i = 0; i < LIVE; i++) {
    tableSet(&table, OBJ_VAL(batches[0][i]), NUMBER_VAL(i));
  }

  printf("Keep %d live keys; each round deletes them all and inserts %d new ones\n",
         LIVE, LIVE);
  printf("%6s %10s %8s %11s %10s %12s %13s\n", "round", "capacity",
         "count", "tombstones", "round ms", "ns/miss", "probes/miss");

  Value value;
  for (int r = 0; r <= ROUNDS; r++) {
    double roundTime = 0;
    if (r > 0) {
      double start = nowSeconds();
      for (int i = 0; i < LIVE; i++) {
        tableDelete(&table, OBJ_VAL(batches[r - 1][i]));
      }
      for (int i = 0; i < LIVE; i++) {
        tableSet(&table, OBJ_VAL(batches[r][i]), NUMBER_VAL(i));
      }
      roundTime = nowSeconds() - start;
    }

    int falseHits = 0;
    double start = nowSeconds();
    for (int rep = 0; rep < 20; rep++) {
      for (int i = 0; i < LIVE; i++) {
        if (tableGet(&table, OBJ_VAL(missing[i]), &value)) falseHits++;
      }
    }
    double missTime = (nowSeconds() - start) / (20.0 * LIVE) * 1e9;

    printf("%6d %10d %8d %11d %10.1f %12.1f %13.2f\n", r, table.capacity,
           table.count, countTombstones(&table), roundTime * 1000,
           missTime, averageProbes(&table, missing, LIVE));
    if (falseHits != 0) printf("  unexpected false hits: %d\n", falseHits);
  }

  freeTable(&table);
  for (int r = 0; r <= ROUNDS; r++) free(batches[r]);
  free(missing);
  freeVM();
  return 0;
}