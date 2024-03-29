#include <iostream>
#include <stdexcept>
#include "RansBlockReader.h"

RansBlockReader::RansBlockReader(char const *fileName) : _file(fileName),
                                                         // We are casting away the const.  For some reason the library likes it
                                                         // that way.  We are not going to modify anything.
                                                         _next((uint32_t *)_file.begin()),
                                                         _end(_next + _file.size()),
                                                         _remainingInBlock(0)
{
  if (!_file.valid())
    throw std::runtime_error(_file.errorMessage());
}

bool RansBlockReader::eof()
{
  if (_remainingInBlock < 0)
    // We have previously established that we are at the end of the file.
    return true;
  if (_remainingInBlock > 0)
    // We are in the middle of processing a block of data and we have at least
    // 1 item left in the current block.
    // TODO This would be a perfect place to check if we are reading past the
    // end of the file.  Report an exception ASAP.
    return false;
  // We need to start a new block.  This is why eof() is not const!
  if (_next >= _end)
    // We expect one last block with a length of 0 to signal a clean end of
    // file.  That's not strictly required but I like it for a lot of reasons.
    // Among other things, it's basically an assertion, one last check that
    // everything looks good.  It could catch a truncated file.  But it's also
    // checking for errors in the way we are reading and interpreting the
    // file.
    throw std::runtime_error("Incomplete file.");
  _remainingInBlock = *_next;
  _next++;
  // TODO this would also be a good place to catch if we are reading past the
  // end of the data.
  // std::cout<<"Next block size:  "<<_remainingInBlock<<std::endl;
  if (_remainingInBlock < 0)
    // This has been very helpful.  It is tempting to add more info to this
    // value.  It's a 32 bit value and maxBlockSize currently requires only
    // 13 bits.
    throw std::runtime_error("Corrupt file.");
  if (!_remainingInBlock)
  { // We found a properly marked end of file.
    _remainingInBlock = -1;
    return true;
  }
  // We started a new block that contains more data.
  Rans64DecInit(&_ransState, &_next);
  return false;
}

uint32_t RansBlockReader::get(uint32_t denominator)
{
  if (eof())
    throw std::runtime_error("Reading past end of file");
  return RansRange::get(denominator, &_ransState);
}

void RansBlockReader::advance(RansRange range)
{
  // TODO add an assertion to make sure we are calling get() and advance()
  // in the right order.
  if (_next >= _end)
    throw std::runtime_error("Incomplete or corrupt file.");
  range.advance(&_ransState, &_next);
  _remainingInBlock--;
}

uint32_t RansBlockReader::getWithEqualWeights(uint32_t count)
{
  const uint32_t result = get(count);
  advance(RansRange(result, 1, count));
  return result;
}

void RansBlockReader::dumpStats(std::ostream &out)
{ // Things like how many blocks we've read.  Normal stuff.
  out << "RansBlockReader::dumpStats() TODO" << std::endl;
}
