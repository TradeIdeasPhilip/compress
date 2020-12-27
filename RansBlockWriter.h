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
};


#endif
