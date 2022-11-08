#ifndef __EightShared_h_
#define __EightShared_h_

#include <string>

#include "../shared/RansHelper.h"
#include "../shared/RansBlockReader.h"
#include "../shared/RansBlockWriter.h"


// When looking back at context, pretend like this data came at the very
// beginning of the file, before the first actual byte of the file.
// We do this mostly because it keeps the algorithm simple.
// Don't actually try to encode, compress, or store this data!
extern const std::string preloadContents;

// This should be tunable.  And probably we need to record the value in
// to file so the reader will be able to run the identical algorithm.
// I'm using 8,000 bytes right now because that's the default buffer size
// that gzip uses.  (I'm not saying that's the right answer.  Just changing
// fewer things at a time.)
//
// Note:  At one time we looked back this far plus 8 bytes.  That's
// unnecessarily complicated.
//
// If you have more than maxBufferSize bytes in memory (including the actual
// file and the preLoadContents) you can release the stuff at the beginning.
extern const int maxBufferSize;



class HistorySummary
{
private:
  uint32_t _frequencies[256];
  uint32_t _denominator;

  static int matchingByteCount(int64_t a, int64_t b);
  static int64_t getContext(char const *ptr);
  
public:
  // end is the byte that you are about to encode / decode.  We do not look
  // at that.  (Standard STL, stop right before end.)
  // begin is the first byte available to the algorithm.  We are shooting for
  // maxBufferSize bytes.  If you provide extra, this algorithm will skip the
  // extra bytes from the beginning.  If you don't have enough bytes, add
  // preloadContents to the beginning of the file.  If you still don't have
  // enough data, point to the first byte you have, i.e. the first byte of
  // your copy of preloadContents.
  HistorySummary(char const *begin, char const *end);

  // The encoder will build a HistorySummary then add the next character.
  // You can call canEncode() to check if this algorithm will work at all.
  // Or you can skip that step, and always call encode, then call
  // RansRange::valid() on the result.
  bool canEncode(char toEncode) const;
  RansRange encode(char toEncode) const;

  char getAndAdvance(RansBlockReader &source) const;
};

// The compressor gives bytes to this class one at a time.
// The decompressor reads bytes from this class one at a time.
// This class will defer a lot of the hard work to HistorySummary.
// This class will directly handle the cases that HistorySummary can't, using
//   the simplist possible encoding.
// And this class takes care of encoding / decoding the choice that we make,
//   HistorySummary or the simple method.
class TopLevel
{
private:
  static const bool TRIVIAL = false;
  static const bool SMART = true;  
  BoolCounter _smartCount;

  int _counter;
  
  // How often do we go back to the counter and ask it to trim its results?
  // If we never do it, things will overflow.  :(
  // Effectively we are looking at an exponential moving average in _history.
  // Making this number smaller gives less weight to older history.
  static const int MAX_COUNTER = 5000;

  void trivialEncode(char toEncode, RansBlockWriter &writer);
  char trivialDecode(RansBlockReader &reader);
  
public:
  TopLevel();

  void encode(char toEncode, HistorySummary const &historySummary,
	      RansBlockWriter &writer);

  char decode(char const *begin, char const *end, RansBlockReader &reader);
};



#endif
