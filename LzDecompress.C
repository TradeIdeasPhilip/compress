#include <string.h>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

#include "../shared/MiscSupport.h"


// g++ -o lz_decompress -O4 -ggdb -std=c++0x LzDecompress.C ../shared/MiscSupport.C

class exception : public std::exception
{
private:
  const std::string _what;
public:
  exception(std::string const &what) : _what(what) { }
  virtual const char* what() const noexcept { return _what.c_str(); }
};

class MruList
{ // TODO  Rotating a vector of std::string takes longer than it should.
  // We need a list of int16_t values responsible for the order.  Rotate that
  // list to rearrage things.  Each int16_t is an index into the actual list of
  // strings.
  const int _maxSize;
  std::vector< std::string > _strings;
  std::string _recent1;
  std::string _recent2;
public:
  MruList(int maxSize = 4096);
  std::string const &get(int index);
  std::string flush();
};

MruList::MruList(int maxSize) : _maxSize(maxSize)
{
  _strings.reserve(_maxSize);
  for (int i = 0; i < 256; i++)
    _strings.push_back(std::string(1, (char)i));
};

std::string const &MruList::get(int index)
{
  if (!_recent2.empty())
  { // We need to concatinate the result of the last two get()'s and insert
    // that into the MRU list.
    if (_strings.size() >= _maxSize)
      // Delete a string so we don't have too many in the table.
      for (auto it = _strings.end(); it != _strings.begin();)
      { // Start from the oldest and pick the first that is longer than 1 byte.
	it--;
	if (it->length() > 1)
	{
	  _strings.erase(it);
	  break;
	}
      }
    // Insert the new string at the beginning, index 0.
    _strings.insert(_strings.begin(), _recent1 + _recent2);
    // Empty the buffer.  So we don't accidentally use these strings again.
    _recent1.clear();
    _recent2.clear();
  }
  if ((index < 0) || (index >= _strings.size()))
    throw exception("Invalid Input.  Index out of range.  index=" + ntoa(index) + ", _strings.size()=" + ntoa(_strings.size()));
  // Move the requested string to index 0.  That says the requested string was
  // the most recently used item.
  std::rotate(_strings.begin(), _strings.begin() + index, _strings.begin() + index + 1);
  if (_maxSize > 256)
  {
    if (_recent1.empty())
      // This is the first part of a new string we want to create.
      _recent1 = _strings[0];
    else
      // This is the second part of a new string we want to create.  The next
      // time someone calls get() we will create that string.  Unless someone
      // calls flush() first.
      _recent2 = _strings[0];
  }
  return _strings[0];
}

std::string MruList::flush()
{
  _recent1.clear();
  _recent2.clear();
}

std::istream *compressedInput;

const int END_OF_FILE = -1;

int readNextIndex()
{
  const int first = compressedInput->get();
  if (compressedInput->eof())
    return END_OF_FILE;
  const int second = compressedInput->get();
  if (compressedInput->eof())
    throw exception("Unexpected end of file.  Odd number of bytes.");
  //std::cerr<<"readNextIndex() --> "<<second<<':'<<first<<std::endl;
  return (second<<8) ^ first;
}

void writeString(std::string const &s)
{
  std::cout<<s<<std::flush;
}

int main(int argc, char *argv[])
{
  if ((argc >= 2) && strcmp(argv[1], "-"))
  {
    compressedInput = new std::ifstream(argv[1], std::ios::binary | std::ios::in);
    if (!compressedInput)
      throw exception(errorString() + "  std::ifstream(" + argv[1] + ')');
  }
  else
    compressedInput = &std::cin;
  MruList mruList;
  while (true)
  {
    const int index = readNextIndex();
    if (index == END_OF_FILE)
      break;
    writeString(mruList.get(index));
  }
  return 0;
}
