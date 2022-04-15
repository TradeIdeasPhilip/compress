#ifndef __Misc_h_
#define __Misc_h_

#include <stdint.h>
#include <assert.h>


// Value is always computed.  We also call assert(value) if assertions are
// enabled.  Value is discarded either way.  You do not get a warning either
// way.  This is useful when (a) a function has a side effect (b) the function
// returns true on success, and (c) failure seems unlikely, but we still want
// to check sometimes.
template < class T >
void assertTrue(T const &value)
{
  assert(value);
}

template < class T >
void assertFalse(T const &value)
{ 
  assert(!value);
}


/////////////////////////////////////////////////////////////////////
// getMicroTime() returns the number of microseconds since the Unix
// epoch.
/////////////////////////////////////////////////////////////////////

int64_t getMicroTime();



#endif
