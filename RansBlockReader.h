#ifndef __RansBlockReader_h__
#define __RansBlockReader_h__

#include "RansHelper.h"
#include "File.h"

// Most of these methods will throw a RuntimeError if there's a problem.

// This is the inverse of RansBlockWriter.
class RansBlockReader
{
private:
  File _file;
  uint32_t *_next;
  const uint32_t *_end;
  int32_t _remainingInBlock;
  Rans64State _ransState;
  
public:
  RansBlockReader(char const *fileName);
  bool eof();  // Explicitly not const.

  // First call get() to get the next number.  You should already have a list
  // of start positions for each symbol.  Find which symbol is associated with
  // this value.  (The symbol with the greatest start that is <= the
  // result of get().)  Create a RansRange for that symbol.  Finally call
  // advance() with that RansRange object.
  uint32_t get(uint32_t denominator);
  void advance(RansRange range);

  void dumpStats(std::ostream &out);

};


#endif
