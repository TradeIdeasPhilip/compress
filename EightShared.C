#include <string.h>

#include "EightShared.h"

// Some of my test data starts with "#include"!  I chose this in part as a
// joke as it will help some files.  It will probably do a better job than
// all nulls.  If nothing else, it's a good test case.  Whatever string I
// chose, I'm sure I'd find it at the front of some file.
const std::string preloadContents = "#include";

const int maxBufferSize = 8000;


/////////////////////////////////////////////////////////////////////
// HistorySummary
/////////////////////////////////////////////////////////////////////

inline int64_t HistorySummary::getContext(char const *ptr)
{
  char const *context = ptr - 8;
  return *reinterpret_cast< int64_t const * >(context);
}

inline int HistorySummary::matchingByteCount(int64_t a, int64_t b)
{
  int64_t difference = a ^ b;
  if (difference == 0)
    // What does clzl(0) return?  It depends if you have optimization on or
    // off!  This is the best possible case, a perfect match.
    return 8;
  return __builtin_clzl(difference) / 8;
}

HistorySummary::HistorySummary(char const *begin, char const *end)
{
  begin += 8;
  if (begin >= end)
  { // Quickly avoid any issues with signed vs unsigned arithmetic.
    // And below we can say for sure that at least one item will be found.
    _denominator = 0;
    return;
  }
  if (end - begin > maxBufferSize)
    begin = end - maxBufferSize;
  const int64_t initialContext = getContext(end);
  assert(maxBufferSize < 0xffff);
  uint16_t byteVsContextLengthToByteCount[9][256];
  memset(byteVsContextLengthToByteCount, 0,
	 sizeof(byteVsContextLengthToByteCount));
  for (char const *compareTo = begin; compareTo < end; compareTo++)
  {
    auto const count =
      matchingByteCount(initialContext, getContext(compareTo));
    byteVsContextLengthToByteCount[count][(unsigned char)*compareTo]++;
  }
  // For simplicity I'm starting with an older algorithm that I know doesn't
  // give the best results.  I'm looking to find the longest match.  If there
  // are several tied for longest, look at all of them.  denominator says
  // how many matches we found at this length.
  // TODO bring back the best weighting we tried.  All matches of length 8
  // added together got a weight of 256.  All matches of length 7 put together
  // got a weight of 128.  ... matches of length 0, weight = 1.
  int denominator = 0;
  int highestLevelWithData = 8;
  while (true)
  {
    assert(highestLevelWithData >= 0);
    for (int byteIndex = 0; byteIndex < 256; byteIndex++)
      denominator +=
	byteVsContextLengthToByteCount[highestLevelWithData][byteIndex];
    if (denominator > 0)
      // Found the best one.  The remainer could only be shorter matches
      // and we are currently ignoring them.
      break;
    highestLevelWithData--;
  }
  assert(sizeof(byteVsContextLengthToByteCount[highestLevelWithData])
	 == sizeof(_frequencies));
  memcpy(_frequencies, byteVsContextLengthToByteCount[highestLevelWithData],
	 sizeof(_frequencies));
  _denominator = denominator;
}

bool HistorySummary::canEncode(char toEncode) const
{
  return (_denominator > 0) && (_frequencies[(unsigned char)toEncode] > 0);
}

RansRange HistorySummary::encode(char toEncode) const
{
  uint16_t before = 0;
  for (int index = 0; index < (unsigned char)toEncode; index++)
    before += _frequencies[index];
  return RansRange(before,
		   _frequencies[(unsigned char)toEncode],
		   _denominator);
}

char HistorySummary::getAndAdvance(RansBlockReader &source) const
{
  if (!_denominator)
    throw std::runtime_error("invalid input");
  const uint32_t position = source.get(_denominator);
  uint32_t before = 0;
  int index = 0;
  while (true)
  {
    const auto frequency = _frequencies[index];
    const uint32_t after = before + frequency;
    if (after > position)
    { // Found the index we were looking for!
      source.advance(RansRange(before, frequency, _denominator));
      return index;
    }
    before = after;
    index++;
    assert(index < 256);
  }
}


/////////////////////////////////////////////////////////////////////
// TopLevel
/////////////////////////////////////////////////////////////////////

TopLevel::TopLevel() : _counter(-1) { }

void TopLevel::encode(char toEncode,
		      HistorySummary const &historySummary,
		      RansBlockWriter &writer)
{
  if (_counter == -1)
  { // Silly optimization.  We know 100% that the first byte will be trivially
    // encoded, so why go through the work and why store anything in the file.
    // Just jump directly to the trivialEncode() step.
    trivialEncode(toEncode, writer);
  }
  else
  {
    const bool smart = historySummary.canEncode(toEncode);
    writer.write(_smartCount.getRange(smart));
    _smartCount.increment(smart);
    if (smart)
    {
      writer.write(historySummary.encode(toEncode));
    }
    else
    {
      trivialEncode(toEncode, writer);      
    }
  }
  _counter++;
  if (_counter >= MAX_COUNTER)
  {
    assert(_counter == MAX_COUNTER);
    _smartCount.reduceOld();
    _counter = 0;
  }
}

char TopLevel::decode(char const *begin,
		      char const *end,
		      RansBlockReader &reader)
{
  bool smart;
  if (_counter == -1)
    smart = false;
  else
  {
    reader.eof();
    smart = _smartCount.readValue(reader.getRansState(), reader.getNext());
    _smartCount.increment(smart);
  }
  _counter++;
  if (_counter >= MAX_COUNTER)
  {
    assert(_counter == MAX_COUNTER);
    _smartCount.reduceOld();
    _counter = 0;
  }
  if (smart)
    return HistorySummary(begin, end).getAndAdvance(reader);
  else
    return trivialDecode(reader);
}

void TopLevel::trivialEncode(char toEncode, RansBlockWriter &writer)
{
  writer.write(RansRange((unsigned char)toEncode, 1, 256));
}

char TopLevel::trivialDecode(RansBlockReader &reader)
{
  reader.eof();
  const unsigned char result = reader.get(256);
  reader.advance(RansRange((unsigned char)result, 1, 256));
  return result;
}

