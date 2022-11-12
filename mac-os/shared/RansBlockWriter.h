#ifndef __RansBlockWrite_h_
#define __RansBlockWrite_h_

#include <fstream>

#include "RansHelper.h"


class RansBlockWriter
{
private:
  std::ofstream _stream;
  std::vector< RansRange > _stack;
  void flush(bool force = false);

public:
  RansBlockWriter(std::string const &fileName);
  ~RansBlockWriter();
  bool error() const { return !_stream; }
  std::string errorMessage() const;
  void write(RansRange const &toWrite);

  /**
   * This covers the simple case where there are count possible values,
   * numbered 0 to count-1, and we assume any of these values is equally
   * likely.
   * 
   * Use this with RansBlockReader::getWithEqualWeights().
  */
  void writeWithEqualWeights(uint32_t value, uint32_t count)
  { write(RansRange(value, 1, count)); }
};


#endif
