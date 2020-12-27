#include "RansBlockWriter.h"


void RansBlockWriter::flush(bool force)
{
  if (force || !_stack.empty())
  {
    std::vector< uint32_t > buffer(512);
    uint32_t *writePtr = &buffer[buffer.size()];
    const int MARGIN_SIZE = 3;
    uint32_t *margin = &buffer[MARGIN_SIZE];
    Rans64State r;
    Rans64EncInit(&r);
    for (auto it = _stack.rbegin(); it != _stack.rend(); it++)
    {
      it->put(&r, &writePtr);
      if (writePtr < margin)
      { // Request more memory.
	assert(writePtr == &buffer[MARGIN_SIZE-1]);
	const auto toAdd = buffer.size();
	buffer.insert(buffer.begin(), toAdd, 0);
	margin = &buffer[MARGIN_SIZE];
	writePtr = margin + toAdd - 1;
      }
    }
    Rans64EncFlush(&r, &writePtr);
    writePtr--;
    *writePtr = _stack.size();
    _stack.clear();
    _stream.write(reinterpret_cast< char const * >(writePtr),
		  (&*buffer.end() - writePtr) * 4);
  }
}


RansBlockWriter::RansBlockWriter(std::string const &fileName) :
  _stream(fileName) { }

RansBlockWriter::~RansBlockWriter()
{
  flush();
  flush(true);
}

std::string RansBlockWriter::errorMessage() const
{
  return "TODO";
}

void RansBlockWriter::write(RansRange const &toWrite)
{
  _stack.push_back(toWrite);
  const size_t MAX_SIZE = 10000;  // Random as anything.
  if (_stack.size() >= MAX_SIZE)
    flush();
}
