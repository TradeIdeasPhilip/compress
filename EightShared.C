// TODO / bug:  One of my test files compresses and decompresses perfectly,
// and is about the expected size.  Another one crashes when I try to
// decompress it.  Committing to save a lot of good work, but this is not
// ready for prime time!

#include <string.h>
#include <iostream>
#include <map>
#include <iomanip>

#include "JumpBackSummary.h"

#include "EightShared.h"

// Originally I picked "#include" because a lot of files start with that
// string.  On closer inspection that actually was the worst thing I could
// do.  Every time the compressor saw "#include" it though there was a strong
// chance it would see "#include" a second time, immediately after that.
// I could still try to craft something clever, but the simple thing is to
// try to make preloadContents something that is unlikely to match any part of
// any real file.
const std::string preloadContents = "\xdc\xe4\xeb\xf1\xf6\xfa\xfd\xff";

const int maxBufferSize = 8000;


/////////////////////////////////////////////////////////////////////
// DebugDump
/////////////////////////////////////////////////////////////////////

class DebugDump
{
private:
  void dumpChar(unsigned char ch, std::ostream &out)
  {
    out<<'\'';
    if ((ch < ' ') || (ch >= 127))
    { // Save and restore state:  https://stackoverflow.com/a/30968371/971955
      std::ios oldState(nullptr);
      oldState.copyfmt(out);
      out<<"\\x"<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)ch;
      out.copyfmt(oldState);
    }
    else
    {
      out<<ch;
    }
    out<<'\'';
  }
  std::map<char, int64_t> _trivialEncodeCount;
  std::map<int, int64_t> _jumpBackHowFar;
public:
  void trivialEncode(char ch) { _trivialEncodeCount[ch]++; }
  void jumpBack(int howFar) { _jumpBackHowFar[howFar]++; }
  ~DebugDump()
  {
    auto &out = std::cerr;
    out<<"[[DebugDump start]]"<<std::endl;
    // This is completely unavoidable.  The first time we see a new byte, we
    // always do trivial encoding.
    out<<"trivial encoding once:  ";
    bool first = true;
    for (auto const &kvp : _trivialEncodeCount)
    {
      auto const count = kvp.second;
      if (count == 1)
      {
	if (first)
	  first = false;
	else
	  out<<", ";
	dumpChar(kvp.first, out);
      }
    }
    if (first)
      out<<"none";
    out<<std::endl;
    int extras = 0;
    for (auto const &kvp : _trivialEncodeCount)
    {
      auto const count = kvp.second;
      if (count > 1)
      {
	out<<"trivial encoding ";
	dumpChar(kvp.first, out);
	out<<' '<<count<<" times."<<std::endl;
	extras += (count - 1);
      }
    }
    // I was concerned that the new JumpBack code would skip too many
    // interesting bytes.  In particular, what if you had to do a lot of
    // trivial encodings because we were skipping some bytes.  I was
    // considering using a different algorithm that looked at all bytes.
    // So far the results say that will not be nesessary.  So far I have
    // seen very few extra encodings, and the total output size is almost
    // unchanged.
    std::cout<<"A total of "<<extras<<" extra trivial encodings."<<std::endl;
    
    int jumpBackCount = 0;
    int jumpBackSavings = 0;
    for (auto const &kvp : _jumpBackHowFar)
    {
      auto const stepsBack = kvp.first;
      auto const repeats = kvp.second;
      jumpBackCount += repeats;
      jumpBackSavings += (stepsBack - 1) * repeats;
    }
    // What I had to do + what I could skip = what I originally thought I had
    // to do.
    auto const jumpBackOriginal = jumpBackCount + jumpBackSavings;
    for (auto const &kvp : _jumpBackHowFar)
    {
      auto const stepsBack = kvp.first;
      auto const repeats = kvp.second;
      auto const saved = (stepsBack-1) * repeats;
      out<<"Jump Back by "<<stepsBack<<" steps "<<repeats<<" times to save "
	 <<saved<<" comparisons, "<<(saved * 100.0 / jumpBackOriginal)
	 <<'%'<<std::endl;
    }
    out<<"Total Jump Back savings:  "<<jumpBackSavings<<", "
       <<(jumpBackSavings * 100.0 / jumpBackOriginal)<<'%'<<std::endl;
    
    out<<"[[DebugDump end]]"<<std::endl;

    // TODO / bug:  On a really big input file the Jump Back statistics will
    // include negative numbers.  I'm not sure why.  I thought it was an
    // overflow, but switching to 64 bit integers didn't help.  ☹
    
    /* Sample output:
[phil@joey-mousepad test_data]$ ../eight Analyze3.C
[[DebugDump start]]
trivial encoding once:  '\x80', '\x9c', '\x9d', '\xe2', '\x09', '\x0a', ' ', '!', '"', '#', '%', '&', ''', '(', ')', '*', '+', ',', '-', '.', '/', '0', '1', '2', '3', '4', '5', '6', '8', ':', ';', '<', '=', '>', 'A', 'B', 'C', 'D', 'F', 'H', 'I', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'Y', '[', '\', ']', '_', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '}', '~'
trivial encoding 'E' 2 times.
A total of 1 extra trivial encodings.
Jump Back by 1 steps 53159648 times to save 0 comparisons, 0%
Jump Back by 2 steps 539401 times to save 539401 comparisons, 0.981085%
Jump Back by 3 steps 92270 times to save 184540 comparisons, 0.335649%
Jump Back by 4 steps 27195 times to save 81585 comparisons, 0.14839%
Jump Back by 5 steps 26430 times to save 105720 comparisons, 0.192288%
Jump Back by 6 steps 10943 times to save 54715 comparisons, 0.0995179%
Jump Back by 7 steps 6412 times to save 38472 comparisons, 0.0699745%
Jump Back by 8 steps 14165 times to save 99155 comparisons, 0.180347%
Total Jump Back savings:  1103588, 2.00725%
[[DebugDump end]]
    */
    
  }
};

static DebugDump debugDump;

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
    // What does __builtin_clzl(0) return?  It depends if you have optimization
    // on or off!  This is the best possible case, a perfect match.
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
  JumpBackSummary jumpBackSummary(end);
  assert(maxBufferSize < 0xffff);
  uint16_t byteVsContextLengthToByteCount[9][256];
  memset(byteVsContextLengthToByteCount, 0,
	 sizeof(byteVsContextLengthToByteCount));
  for (char const *compareTo = end - 1; compareTo >= begin; )
  {
    auto const count =
      matchingByteCount(initialContext, getContext(compareTo));
    byteVsContextLengthToByteCount[count][(unsigned char)*compareTo]++;
    auto const howFar = jumpBackSummary.howFar(count);
    compareTo -= howFar;
    debugDump.jumpBack(howFar);
  }
  // We are back to the best weighting we tried.  All matches of length 8
  // added together got a weight of 256.  All matches of length 7 put together
  // got a weight of 128.  ... matches of length 0, weight = 1.
  //
  // How to do it right.  Notice that we are typically looking at a small
  // number of samples.  The highest value we'd expect to see is around 8,000
  // which could be represented with 13 bits.  And the highest extra weight
  // we will use is 256 which can be represented in 9 bits.  In the worst
  // case all 8000 entries were all in the same place and all given a weight
  // of 256.  Multiplying by 256 would add 8 bits so you need 17 bits to
  // hold the largest number we might possible care about.
  //
  // We add extra bits for precision.  We have plenty to spare.  At this
  // point we're not even worried about fitting into 31 bits.  We can use
  // 64 bit number to do the intermediate work.  Add up all of the separate
  // weighted sums for each byte.  Only after doing all the adding, consider
  // trimming back.  See what the total is and decide how many bits we
  // need to >> every value to get below 31 bits, maybe 30 to be safe.
  // Standard rounding.  Some positive values might get rounded down to 0.
  // Take the new total after shifting and rounding the individual values.
  // Plug that into the rANS decoder.
  //
  // This is how we apply the weights while minimizing round off error.
  // First we multiply each number in our table by some large integer.
  // Maybe 2^62 / maxBufferSize.  Then we safely know that every individual
  // number will be less than 2^62.  Then we iterate over the match sizes.
  // Each match size gets a number:  the number of matches for that size *
  // 2^(8-(match size)).  Then we iterate over all of the values.  We divide
  // each by the number we just computed for it's match size.  Then we add
  // the result into a counter that is specific to the byte.  That leaves
  // us with an array of 256 64-bit numbers.  So far no positive numbers have
  // been rounded down to 0.  Now do the >> described above to get us into a
  // reasonable range, possibly converting some things to 0.  Then take the
  // actual total so we can give that to RansRange() as the denominator.
  uint64_t totals[256];
  memset(totals, 0, sizeof(totals));
  for (int matchLength = 0; matchLength <= 8; matchLength++)
  {
    int count = 0;
    for (int i = 0; i < 256; i++)
      count += byteVsContextLengthToByteCount[matchLength][i];
    if (count > 0)
    {
      const uint64_t weight = (1ul<<62) / (1<<(8 - matchLength)) / count;
      for (int i = 0; i < 256; i++)
	totals[i] += byteVsContextLengthToByteCount[matchLength][i] * weight;
    }
  }
  uint64_t grandTotal = 0;
  for (int i = 0; i < 256; i++)
    grandTotal += totals[i];
  int reduceBy = 0;
  // Quick and dirty scale back until it all fits into 31 bits.  We have enough
  // bits to spare, we don't have to be super careful.
  while (grandTotal >= RansRange::SCALE_END)
  { // TODO Replace this loop with another call to __builtin_clzl().
    grandTotal >>= 1;
    reduceBy++;
  }
  _denominator = 0;
  for (int i = 0; i < 256; i++)
    _denominator += _frequencies[i] = totals[i]>>reduceBy;
}

bool HistorySummary::canEncode(char toEncode) const
{
  return (_denominator > 0) && (_frequencies[(unsigned char)toEncode] > 0);
}

RansRange HistorySummary::encode(char toEncode) const
{
  uint32_t before = 0;
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
  { // Precondition:  Do not call if reader.eof() returns true.
    assertFalse(reader.eof());
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
  debugDump.trivialEncode(toEncode);
  writer.write(RansRange((unsigned char)toEncode, 1, 256));
}

char TopLevel::trivialDecode(RansBlockReader &reader)
{
  reader.eof();
  const unsigned char result = reader.get(256);
  reader.advance(RansRange((unsigned char)result, 1, 256));
  return result;
}

