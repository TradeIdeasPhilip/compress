#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include "File.h"


size_t File::roundUpToPageSize(size_t size)
{
  auto const pageSize = sysconf(_SC_PAGESIZE);
  return (size + pageSize - 1) / pageSize * pageSize;
}

File::File(char const *name, std::string const &preamble) :
  File(name, preamble.length())
{
  if (valid())
  {
    assert(preambleBegin() + preamble.length() == preambleEnd());
    std::copy(preamble.begin(), preamble.end(), preambleBegin());
  }
}

File::File(std::string const &name, size_t preambleSize) :
  File(name.c_str(), preambleSize)
{
}

File::File(char const *name, size_t preambleSize) :
  _preambleSize(preambleSize), _preambleFirstAllocated(NULL),
  _begin(NULL), _end(NULL)
{
  int handle = open(name, O_RDONLY);
  if (handle < 0)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " open(“";
    _errorMessage += name;
    _errorMessage += "”)";
    return;
  }
  off_t length = lseek(handle, 0, SEEK_END);
  if (length == (off_t)-1)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " lseek(“";
    _errorMessage += name;
    _errorMessage += "”)";
    close(handle);
    return;
  }
  void *recommendedStart = NULL;
  if (preambleSize)
  {
    // _preambleFirstAllocated includes the preamble and some padding we had
    // to add to the front of it.  _preambleFirstAllocated is what we need to
    // give to munmap() in the destructor.  preambleBegin() is what the user
    // sees.  preambleBegin() is typically > _preambleFirstAllocated.
    auto const extraSpace = roundUpToPageSize(preambleSize);
    // Request enough space for everything.  We will only keep the preamble.
    // The rest will be overwritten when we mmap() the actual file.  We make
    // this request big so the operating system will allocated enough
    // continuous address space.
    _preambleFirstAllocated =
      (char *)mmap(NULL,
		   extraSpace + length,
		   PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE,
		   -1, 0);
    if (_preambleFirstAllocated == (void *)-1)
    {
      _errorMessage = errorString();
      _errorMessage += " mmap(MAP_ANONYMOUS)";
      _preambleFirstAllocated = NULL;
      close(handle);
      return;
    }
    recommendedStart = _preambleFirstAllocated + extraSpace;
  }
  void *address = mmap(recommendedStart, length, PROT_READ,
		       MAP_SHARED | (recommendedStart?MAP_FIXED:0),
		       handle, 0);
  if (address == (void *)-1)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " mmap(“";
    _errorMessage += name;
    _errorMessage += "”)";
    close(handle);
    return;
  }
  close(handle);
  int madviseResult = madvise(address, length, MADV_SEQUENTIAL);
  if (madviseResult)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " madvise(MADV_SEQUENTIAL)";
    assertFalse(munmap(address, length));
    return;
  }
  _begin = (char const *)address;
  _end = _begin + length;
}

File::~File()
{
  if (valid())
    assertFalse(munmap((void *)begin(), size()));
  // TODO clean up the preamble, too.  It's not terrible, but I'm worried about
  // all the different places where the constructor could abort.
}

