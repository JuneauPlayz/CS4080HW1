#include <stdio.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char* argv[]) {
  Chunk chunk;
  initChunk(&chunk);

  for (int i = 0; i < 258; i++) {
    writeConstant(&chunk, i, 1);
  }
  writeChunk(&chunk, OP_RETURN, 2);

  printf("== around the switch to long constants ==\n");
  disassembleInstruction(&chunk, 508);
  disassembleInstruction(&chunk, 510);
  disassembleInstruction(&chunk, 512);
  disassembleInstruction(&chunk, 516);
  disassembleInstruction(&chunk, 520);

  freeChunk(&chunk);
  return 0;
}