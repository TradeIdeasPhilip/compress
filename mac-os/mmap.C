#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>

int main(int, char**) {
  void *result = mmap(0, 1000000, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1,0);
  if (result == (void *)-1) {
    perror("mmap");
    return 1;
  }
  printf("Success!\n");
}

