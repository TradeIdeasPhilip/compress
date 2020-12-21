#include <iostream>
#include <bitset>
#include <stdint.h>

#include "File.h"

// g++ -o eight -O4 -ggdb -std=c++0x -Wall Eight.C File.C

int main(int, char **)
{
  int64_t items[] = {0, 1, 2, 4, 8, 16, 32, 0x10000L, 0x100000000L,
		     0x1FFFFFFFFL, 0x1FFFFFFFFFFFFFFFL, 0x5FFFFFFFFFFFFFFFL};
  for (const int64_t item : items)
  {
    std::cout<<__builtin_clzl(item)<<" "<<std::bitset<64>(item)<<" "<<item<<std::endl;
  }
  // Interesting.  0 returns 63, just like 1 does.  I'd expect it to be 64.
  // Seems like I read something like this once, but I can't find it now.
  // Like this is common, but not 100% portable‽
  // Crazy!  I just ran this again, and now 0 returns 64.  I can't find my
  // previous run, so I can't verify that.  But I'm 99% sure it was 63!
}
