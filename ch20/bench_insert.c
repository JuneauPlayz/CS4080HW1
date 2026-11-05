#include "bench.h"

int main(void) {
  initVM();

  int sizes[] = {1000, 100000, 1000000};
  int reps[] = {2000, 20, 2};

  printf("Insert N new string keys into an empty table, then overwrite them\n");
  printf("%10s %6s %10s %14s %16s\n",
         "keys", "reps", "capacity", "ns/new insert", "ns/overwrite");

  for (int s = 0; s < 3; s++) {
    int count = sizes[s];
    ObjString** keys = makeKeys("key", count);

    double insertTime = 0;
    double overwriteTime = 0;
    int capacity = 0;

    for (int r = 0; r < reps[s]; r++) {
      Table table;
      initTable(&table);

      double start = nowSeconds();
      for (int i = 0; i < count; i++) {
        tableSet(&table, OBJ_VAL(keys[i]), NUMBER_VAL(i));
      }
      double middle = nowSeconds();
      for (int i = 0; i < count; i++) {
        tableSet(&table, OBJ_VAL(keys[i]), NUMBER_VAL(-i));
      }
      double end = nowSeconds();

      insertTime += middle - start;
      overwriteTime += end - middle;
      capacity = table.capacity;
      freeTable(&table);
    }

    double ops = (double)count * reps[s];
    printf("%10d %6d %10d %14.1f %16.1f\n", count, reps[s], capacity,
           insertTime / ops * 1e9, overwriteTime / ops * 1e9);
    free(keys);
  }

  freeVM();
  return 0;
}