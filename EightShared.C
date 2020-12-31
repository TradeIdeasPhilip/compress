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

HistorySummary::HistorySummary(char const *begin, char const *end)
{ // TODO
}

bool HistorySummary::canEncode(char toEncode) const
{ // TODO
  return false;
}

RansRange HistorySummary::encode(char toEncode) const
{ // TODO
  throw std::runtime_error("TODO");
}

char HistorySummary::getAndAdvance(RansBlockReader &source) const
{ // TODO
  throw std::runtime_error("TODO");
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
  const unsigned char result = reader.get(256);
  reader.advance(RansRange((unsigned char)result, 1, 256));
  return result;
}

