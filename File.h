#ifndef __File_h_
#define __File_h_

#include <string>


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

// For simpliciy and performance, use mmap() to read the entire file into
// memory.  This implementation is lacking a few things.  We can't handle
// streaming data / pipes.  The file size is limited.  These have nothing to
// do with the compressions algorithm.  You could make another implementation
// which handles those cases better.
class File
{
private:
  char const *_begin;
  char const *_end;
  std::string _errorMessage;
public:
  File(char const *name);
  ~File();
  File(const File&) =delete;
  void operator=(const File&) =delete;

  char const *begin() const { return _begin; }
  char const *end() const { return _end; } // Standard STL:  Stop before here.
  size_t size() const { return _end - _begin; }
  bool valid() const { return _begin; }
  std::string const &errorMessage() const { return _errorMessage; }
};


#endif
