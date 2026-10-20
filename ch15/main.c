#include <stdio.h>
#include <time.h>

#include "common.h"
#include "chunk.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  initVM();

  Chunk chunk;
  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.5);
  writeChunk(&chunk, OP_CONSTANT, 1);
  writeChunk(&chunk, constant, 1);
  for (int i = 0; i < 10000000; i++) {
    writeChunk(&chunk, OP_NEGATE, 1);
  }
  writeChunk(&chunk, OP_RETURN, 1);

  clock_t start = clock();
  for (int run = 0; run < 10; run++) {
    interpret(&chunk);
  }
  double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
  printf("%.3f seconds\n", seconds);

  freeVM();
  freeChunk(&chunk);
  return 0;
}