#ifndef __File_h_
#define __File_h_

#include <string>

#include "Misc.h"


// For simplicity and performance, use mmap() to read the entire file into
// memory.  This implementation is lacking a few things.  We can't handle
// streaming data / pipes.  The file size is limited.  These have nothing to
// do with the compressions algorithm.  You could make another implementation
// which handles those cases better.
class File
{
private:
  const size_t _preambleSize;
  char *_preambleFirstAllocated;
  char const *_begin;
  char const *_end;
  std::string _errorMessage;
  static size_t roundUpToPageSize(size_t size);
public:
  File(char const *name, size_t preambleSize = 0);
  File(char const *name, std::string const &preamble);
  File(std::string const &name, size_t preambleSize = 0);
  ~File();
  File(const File&) =delete;
  void operator=(const File&) =delete;

  char const *begin() const { return _begin; }
  char const *end() const { return _end; } // Standard STL:  Stop before here.
  size_t size() const { return _end - _begin; }
  bool valid() const { return _begin; }

  // Note that the preamble is explicitly read/write and the main body is
  // explicitly read-only.  Because the preamble comes right before the
  // main body, it would be easy to accidentally try to write over the
  // main body.  That would probably cause a core dump.  Always respect
  // preambleEnd().
  char *preambleBegin() const
  { return const_cast< char * >(_begin) - _preambleSize; }
  char const *preambleEnd() const { return begin(); }

  std::string const &errorMessage() const { return _errorMessage; }
};


#endif
