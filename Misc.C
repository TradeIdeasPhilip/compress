#include <sys/time.h>
#include <stddef.h>

#include "Misc.h"

int64_t getMicroTime()
{
  timeval inPieces;
  assertFalse(gettimeofday(&inPieces, NULL));
  int64_t result = inPieces.tv_sec;
  result *= 1000000;
  result += inPieces.tv_usec;
  return result;
}


