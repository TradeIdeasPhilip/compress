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


uint64_t simpleHash(char const *start)
{
  return simpleHash(start, strlen(start));
}

uint64_t simpleHash(char const *start, size_t length)
{
  return simpleHash(start, start + length);
}

uint64_t simpleHash(char const *start, char const *end)
{ // This was inspired by the standard Java string hash, but it uses 64 bit integers because it can.
  uint64_t result = 0x123456789abcdef0;
  uint64_t factor = 1;
  while (end > start)
  {
    end--;
    result += (*end) * factor;
    factor *= 65551;  // 0x1000F, prime!
  }
  return result;
}

uint64_t simpleHash(std::string const &string)
{
  return simpleHash(string.c_str(), string.length());
}

std::string errorString()
{
  char buffer[256];
  buffer[0] = 0;
  return buffer;
}