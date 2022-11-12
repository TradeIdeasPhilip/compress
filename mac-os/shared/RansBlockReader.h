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

  /**
   * This is a simplified alternative to calling get() and advance().
   * 
   * This assumes that the were count possible values, numbed 0 through count-1.
   * This assumes all values have equal probabilities.
   * 
   * This can decode values encoded with RansBlockWriter::writeWithEqualWeights().
   */
  uint32_t getWithEqualWeights(uint32_t count);

  // This is ugly.  There should be a better interface connecting
  // RansBlockReader with BoolCounter and SymbolCounter.
  //
  // Always call eof() before trying to read, even if you ignore the result.
  // That will possibly deal with framing issues.
  // Then do a get followed by an advance.  3 steps every time.
  //
  // Super duper ugly.
  uint32_t **getNext() { _remainingInBlock--; return &_next; }
  Rans64State *getRansState() { return &_ransState; }
  
  void dumpStats(std::ostream &out);

};


#endif
