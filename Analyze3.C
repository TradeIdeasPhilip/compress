#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <string>
#include <string.h>
#include <assert.h>
#include <cmath>

#include "File.h"

// g++ -o a3 -O4 -ggdb -std=c++0x -Wall Analyze3.C File.C

/* The focus of this program is on the entropy encoder.  We want to do a really
 * good job of guessing the next letter.
 * 
 * Currently we focus on processing one byte at a time.  The decoder will
 * spit out single bytes.  There is room for adding other types of data,
 * like copying a whole string at once, but for now it's just one byte at
 * a time.
 *
 * The first question, is this a new byte, or have we seen this byte before?
 * The way this program works we are mostly making reference to bytes that
 * we've seen recently.  We need to deal with new bytes, so let's make that
 * the first question, so the rest of the code doesn't have to care about
 * new bytes.  We have a pretty simple and good estimation of the chance
 * that any given byte will be a new byte.  The cost of that data is small.
 * We go the extra mile to encode the new byte, just to show off.  That
 * new byte could have been encoded simply as a single byte.
 *
 * Next we have an algorithm that detects repeated strings.  We do NOT go
 * the traditional route and say "copy 32 bytes starting 147 bytes back".
 * Instead we identify a good candidate string.  If the last two bytes
 * were "qu" then find the previous occurrence of "qu".  What was the next
 * byte last time?  There's a good chance it's the same this time.  The
 * longer the match, the better.  If the previous match was not just "qu"
 * but "The qu" then we are even more certain that the next byte will repeat.
 * The exact chance of a repeat depends on the input file and on the length
 * of the match.  Keep running statistics on how often this next match works
 * out per match length, and use that to make the probabilities for the
 * next byte to be the same.  Depending on the context we will skip this
 * step for some bytes.  Encoder and decoder will quickly detect the situation
 * and just move on.
 *
 * If the previous tests failed, then we look at the two byte context.
 * As in the previous step, we might have no two byte context.  In that
 * case we just skip this step.  Otherwise we look at the bytes commonly
 * used after the last two bytes.  We can probably improve that data
 * because we know at least one byte that is definitely not the right one,
 * the one that we guessed in the previous step.  And presumably that's a
 * popular one, so that will help a lot.  So we have some probabilities for
 * some possible bytes, but other bytes will be missing, probability 0.
 * Fortunately we are already tracking this data.  See "found ratio" in
 * the output.  That is the chance that the two byte context table will
 * include the data we are looking for.  That's another yes/no for the
 * entropy encoder.  If the next byte is in this list, that's an easy one,
 * we are looking at a list of probabilities of each byte.  If not, keep
 * reading.
 *
 * Now we fall back on the one byte context.  Again, this might not exist,
 * encoder and decoder would easily notice that and skip to the next step.
 * Again, we need to say if the next byte is in this table or not.  Again,
 * we are already keeping such statistics under the name "found ratio".
 * Now we know even more bytes that we can ignore in the current table
 * because we would have handled them in a previous step.  If you get this
 * far, look at the remaining probabilities in this table and use that to
 * encode the next byte.
 *
 * If we still haven't found the byte, we have one table left.  The
 * noContext table.  We know the byte is in here.  We remove probabilities
 * for any bytes we've already covered.  We encode the byte accordingly.
 *
 * It's tempting to change the algorithm for the noContext table.  Make
 * that an MRU table, like we've done on other projects.  The first byte
 * in the list would be the most recently used byte.  (Again, we're filtering
 * out any bytes that we've seen in previous steps.)  Collect running
 * statistics to say the chance of each position be selected.  Don't keep
 * any statistics on how common any one byte is, only how common each
 * position is.  It should be easy to try these head to head.  That said,
 * I don't think we will fall back on the noContext table much, so it
 * might not be worth optimizing.
 *
 * At some point we should put a limit on the number of 2 byte context
 * records.  Should be easy.  Just add an MRU list.  Each time a context is
 * reused it gets moved to the front of the list.  If the list gets too long,
 * remove the oldest.  "Too long" would be a command line input to the encoder,
 * and it would be saved in the compressed file's header to the decoder
 * would use the same number.  We need to set a limit just so we can
 * process arbitrarily long and random files without exploding.  This would
 * presumably make very little difference in the output.  We are already
 * limited to 256 one byte tables, so no extra limits are needed there.
 */



// The input is the probability of something happening.  The output is the
// cost in bits to represent this with an ideal entropy encoder.  We use this
// all over for prototyping.  Just ask for the cost, don't actually bother to
// do the encoding.
double pCostInBits(double ratio)
{
  return -std::log2(ratio);
}


class ByteCounter
{
private:
  // TODO this should be a std::map< char, uint16_t >.
  uint16_t _count[256];
  int _totalCount;
public:
  char const *lastIndex;  // TODO move this elsewhere.  It's only needed in certain places.
  ByteCounter() : _totalCount(0) { memset(&_count, 0, sizeof _count); }
  uint16_t getCount(uint8_t index) const { return _count[index]; }
  int getTotalCount() const { return _totalCount; }
  void increment(uint8_t index)
  {
    uint16_t &count = _count[index];
    if (count == 0xfffe)
    { // We set the cutoff to be the biggest number we can fit.  But it could
      // be lower.  If we lower this cutoff we will give a higher weight to
      // newer stuff.
      _totalCount = 0;
      for (int i = 0; i < 256; i++)
      { // Divide all the values by 2.  If a number is odd, round up.
	// 0 stays 0.  1 stays 1.  Everything else gets smaller.
	_count[i] = (_count[i] + 1) / 2;
	_totalCount += _count[i];
      }
    }
    count++;
    _totalCount++;
  }
  
};

class NoContext
{
private:
  ByteCounter _byteCounter;
  double _totalCostInBits;
public:
  NoContext() : _totalCostInBits(0) { }

  void printChar(char ch)
  {
    const int denominator = _byteCounter.getTotalCount();
    assert(denominator != 0);
    const int numerator = _byteCounter.getCount(ch);
    assert((numerator > 0) && (numerator <= denominator));
    const double asRatio = numerator / (double)denominator;
    const double costInBits = pCostInBits(asRatio);
    _totalCostInBits += costInBits;
  }

  void updateStats(char ch)
  {
    _byteCounter.increment(ch);
  }

  double getTotalCostInBits() const { return _totalCostInBits; }

  void dump(std::ostream &out)
  {
    out<<"ℕ𝕠ℂ𝕠𝕟𝕥𝕖𝕩𝕥:  cost in bytes="<<(_totalCostInBits/8)<<std::endl;
  }

};

class OneByteContext
{
private:
  struct PerChar
  {
    ByteCounter byteCounter;
    int64_t tried;
    int64_t matched;
    PerChar() : tried(0), matched(0) { }
  };
  std::map< char, PerChar > _counters;
  double _costYesOrNo;
  double _costIndex;
public:

  OneByteContext() : _costYesOrNo(0), _costIndex(0) { }

  // Returns true if we have completely handled this character.  Returns
  // false if we have not and someone else should try.  In the latter case
  // we might add to ignore.
  bool tryPrintChar(char const *ptr, File const &file,
		    std::set< char > &ignore)
  {
    if (file.begin() >= ptr)
      return false;
    else
      return tryPrintChar(*(ptr-1), *ptr, ignore);
  }
  
  // Returns true if we have completely handled this character.  Returns
  // false if we have not and someone else should try.  In the latter case
  // we might add to ignore.
  bool tryPrintChar(char context, char toPrint, std::set< char > &ignore)
  {
    assert(!ignore.count(toPrint));
    auto const it = _counters.find(context);
    if (it == _counters.end())
    {
      return false;
    }
    PerChar &perChar = it->second;
    assert(perChar.tried);
    int extraCharsInMain = 0;
    int totalCharsInMain = 0;
    int charsInPerChar = 0;
    for (int i = 0; i < 256; i++)
      if (!ignore.count(i))
      {
	const bool inPerChar = perChar.byteCounter.getCount(i);
	const bool inMain = inPerChar || _counters.count(i);
	if (inMain)
	{
	  totalCharsInMain++;
	  if (inPerChar)
	    // TODO unused variables
	    charsInPerChar++;
	  else
	    extraCharsInMain++;
	}
      }
    const double chanceInHere =
      // The first time will always fail, so initially matched = 0 and
      // tried = 1.  So our average will be 0 at first.  Add in one
      // successful match, so instead we start with a 50/50 split.
      extraCharsInMain?((perChar.matched + 1) / (double)(perChar.tried + 1)):1;
    const uint16_t countForToPrint = perChar.byteCounter.getCount(toPrint);
    if (!countForToPrint)
    { // Not in this table.  Try another.
      for (int i = 0; i < 256; i++)
	if ((i != toPrint) && perChar.byteCounter.getCount(i))
	  ignore.insert(toPrint);
      _costYesOrNo += pCostInBits(1 - chanceInHere);
      return false;
    }
    else
    {
      _costYesOrNo += pCostInBits(chanceInHere);
      int64_t denominator = 0;
      for (int i = 0; i < 256; i++)
	if (!ignore.count(i))
	  denominator += perChar.byteCounter.getCount(i);
      _costIndex += pCostInBits(countForToPrint/(double)denominator);
      return true;
    }
  }

  void updateStats(char a, char b)
  {
    PerChar &perChar = _counters[a];
    perChar.tried++;
    if (perChar.byteCounter.getCount(b))
      perChar.matched++;
    perChar.byteCounter.increment(b);
  }

  void updateStats(char const *ptr, File const &file)
  {
    if (file.begin() < ptr)
      updateStats(*(ptr-1), *ptr);
  }

  double getTotalCostInBits() const { return _costIndex + _costYesOrNo; }

  void dump(std::ostream &out)
  {
    out<<"𝕆𝕟𝕖𝔹𝕪𝕥𝕖ℂ𝕠𝕟𝕥𝕖𝕩𝕥:  y/n cost in bytes="<<(_costYesOrNo/8)
       <<", index cost in bytes="<<(_costIndex/8)
       <<", number of counters="<<_counters.size();
    int64_t tried = 0;
    int64_t matched = 0;
    for (auto const &kvp : _counters)
    {
      tried += kvp.second.tried;
      matched += kvp.second.matched;
    }
    out<<", tried="<<tried<<", matched="<<matched<<' '
       <<(matched * 100.0 / tried)<<'%'<<std::endl;
  }

};

class TwoByteContext
{
private:
  struct PerPair
  {
    ByteCounter byteCounter;
    int64_t tried;
    int64_t matched;
    char const *lastIndex;
    PerPair() : tried(0), matched(0), lastIndex(NULL) { }
  };
  
public:
  
};

class Monitor 
{
private:
  ByteCounter _byteCounter;
  int64_t _totalBytes;
  int64_t _bytesFound;
  double _whenFoundRatioSum;
  double _whenFoundCostInBits;
public:
  Monitor() :
    _totalBytes(0),
    _bytesFound(0),
    _whenFoundRatioSum(0),
    _whenFoundCostInBits(0)
  {
  }
  int64_t getTotalBytes() const { return _totalBytes; }
  int64_t getBytesFound() const { return _bytesFound; }
  double whenFoundRatioSum() const { return _whenFoundRatioSum; }
  double whenFoundCostInBits() const { return _whenFoundCostInBits; }
  void increment(uint8_t index)
  {
    if (const int denominator = _byteCounter.getTotalCount())
    { // Don't even think about the empty one.  0/0.  Skip it.
      _totalBytes++;
      if (const int numerator = _byteCounter.getCount(index))
      { // If the numerator is 0, this counter could never help us
	// find the this byte.  It's suggesting there's a 0% chance of this
	// byte happening, but here were are.  We know that happens the first
	// time we see a new byte, and we just need to know how often that
	// happens.
	_bytesFound++;
	const double asRatio = numerator / (double)denominator;
	_whenFoundRatioSum += asRatio;
	const double costInBits = pCostInBits(asRatio);
	_whenFoundCostInBits += costInBits;
      }
    }
    _byteCounter.increment(index);
  }
};

class MegaMonitor : public Monitor
{
public:
  struct PerLength
  {
    int tried;
    int matched;
    PerLength() : tried(0), matched(0) { }
    void add(PerLength const &other)
    {
      tried += other.tried;
      matched += other.matched;
    }
  };
  typedef std::map< int, PerLength > ByLength;
private:
  char const *_lastPtr;
  ByLength _byLength;
public:
  MegaMonitor() : _lastPtr(NULL) { }
  ByLength const &getByLength() const { return _byLength; }
  void increment(char const *begin, char const *ptr)
  {
    if (_lastPtr)
    {
      int matchLength = 0;
      while (true)
      {
	if ((begin + matchLength) >= _lastPtr)
	  // We got all the way back to the beginning of the file.  We can't
	  // go back any more.
	  break;
	const int nextMatchLength = matchLength + 1;
	if (_lastPtr[-nextMatchLength] != ptr[-nextMatchLength])
	  // The strings are no longer the same.  Stop looking.
	  break;
	matchLength = nextMatchLength;
      }
      PerLength &forThisLength = _byLength[matchLength];
      forThisLength.tried++;
      if (_lastPtr[0] == ptr[0])
	forThisLength.matched++;
    }
    _lastPtr = ptr;
    Monitor::increment(*ptr);
  }
};

class Accumulator
{
private:
  int _entries;
  int64_t _totalBytes;
  int64_t _bytesFound;
  double _whenFoundRatioSum;
  double _whenFoundCostInBits;
public:
  Accumulator() :
    _entries(0),
    _totalBytes(0),
    _bytesFound(0),
    _whenFoundRatioSum(0),
    _whenFoundCostInBits(0)
  {
  }
  void add(Monitor const &monitor)
  {
    _entries++;
    _totalBytes += monitor.getTotalBytes();
    _bytesFound += monitor.getBytesFound();
    _whenFoundRatioSum += monitor.whenFoundRatioSum();
    _whenFoundCostInBits += monitor.whenFoundCostInBits();
  }
  void dump(std::ostream &out)
  {
    out<<"entries = "<<_entries<<std::endl;
    out<<"totalBytes = "<<_totalBytes<<std::endl;
    out<<"bytesFound = "<<_bytesFound<<std::endl;
    out<<"found ratio = "<<(_bytesFound / (double)_totalBytes)<<std::endl;
    if (_bytesFound)
    {
      out<<"average chance of picking correct byte = "
	 <<(_whenFoundRatioSum/_totalBytes)<<std::endl;
      out<<"average cost ratio = "
	 <<(_whenFoundCostInBits / 8.0 / _bytesFound)<<std::endl;
    }
  }
};

class MegaAccumulator : public Accumulator
{
private:
  MegaMonitor::ByLength _byLength;
public:
  void add(MegaMonitor const &monitor)
  {
    MegaMonitor::ByLength const &toAdd = monitor.getByLength();
    for (auto const &kvp : toAdd)
    { // For simplicity we have just one table mapping length to % success.
      // It seems like that might vary a lot by prefix, and maybe each prefix
      // should keep its own statistics.
      _byLength[kvp.first].add(kvp.second);
    }
    Accumulator::add(monitor);
  }
  void dump(std::ostream &out)
  {
    Accumulator::dump(out);
    out<<"\tlen\ttried\tmatched\t%match\tcost"<<std::endl;
    MegaMonitor::PerLength big;
    for (auto const &kvp : _byLength)
    {
      const auto length = kvp.first;
      if (length >= 10)
	big.add(kvp.second);
      out<<"\t"<<length;
      const auto tried = kvp.second.tried;
      const auto matched = kvp.second.matched;
      out<<"\t"<<tried<<"\t"<<matched;
      if (tried > 0)
      {
	const double ratio = matched / (double)tried;
	out<<"\t"<<ratio*100;
	const double constInBits = pCostInBits(ratio);
	out<<"\t"<<constInBits;
      }
      out<<std::endl;
    }
    out<<"\t"<<">=10"<<"\t"<<big.tried<<"\t"<<big.matched;
    if (big.tried > 0)
    {
      const double ratio = big.matched / (double)big.tried;
      out<<"\t"<<ratio*100;
      const double costInBits = pCostInBits(ratio);
      out<<"\t"<<costInBits;
      out<<std::endl;
      const auto totalCostOfMatchInBytes = (big.matched * (costInBits/8));
      out<<"big.matched = "<<big.matched<<" bytes * "<<(costInBits/8)
	 <<" -> "<<totalCostOfMatchInBytes<<" bytes"<<std::endl;
      const double failCostInBits = pCostInBits(1 - ratio);
      const double totalFailCostInBytes =
	((big.tried - big.matched) * (failCostInBits/8));
      out<<"big not matched = "<<(big.tried - big.matched)
	 <<" bytes * "<<(failCostInBits/8)
	 <<" -> "<<totalFailCostInBytes
	 <<" bytes"<<std::endl;
      out<<"big cost = "<<totalCostOfMatchInBytes<<" + "
	 <<totalFailCostInBytes<<" = "
	 <<(totalCostOfMatchInBytes + totalFailCostInBytes)
	 <<" = "
	 <<((totalCostOfMatchInBytes + totalFailCostInBytes)/big.matched*100)
	 <<"%";
    }
    out<<std::endl;
  }
};

class Splitter
{
protected:
  static unsigned int getMinSize() { return 5; }
  static unsigned int getMaxSize() { return 25; }
public:
  virtual void addByte(char c) =0;
  virtual bool newString() const =0;
  virtual std::string getNewString() const =0;
  virtual ~Splitter() { }
};

class QuoteSplitter : public Splitter
{
private:
  const char _quote;
  enum State { NotPrimed, Primed, Printed };
  State _state;
  std::string _soFar;
public:
  QuoteSplitter(char quote) : _quote(quote), _state(NotPrimed) { }
  virtual void addByte(char c)
  {
    if (_state == Printed)
      _state = Primed;
    if (c == _quote)
    {
      if ((_state == Primed) && (_soFar.length() >= getMinSize()))
	_state = Printed;
      else
      {
	_state = Primed;
	_soFar.clear();
      }
    }
    else if (_state == Primed)
    {
      if (_soFar.length() >= getMaxSize())
	_state = NotPrimed;
      else
	_soFar += c;
    }
  }
  virtual bool newString() const
  {
    return _state == Printed;
  }
  virtual std::string getNewString() const { return _soFar; }
};

class InGroupSplitter : public Splitter
{
private:
  const std::set< char > _group;
  enum State { ReadyToStart, Running, Aborted, Printed };
  State _state;
  std::string _soFar;
public:
  InGroupSplitter(std::set< char > const &group) :
    _group(group), _state(ReadyToStart) { }
  static InGroupSplitter *letters()
  { // TODO some unicode version of this.  Use unicode character classes.
    std::set< char > group;
    for (char ch = 'A'; ch <= 'Z'; ch++)
      group.insert(ch);
    for (char ch = 'a'; ch <= 'z'; ch++)
      group.insert(ch);
    return new InGroupSplitter(group);
  }
  static InGroupSplitter *symbolish()
  { // TODO should NOT be able to start with a number, or maybe can't be all
    // digits.
    std::set< char > group;
    for (char ch = 'A'; ch <= 'Z'; ch++)
      group.insert(ch);
    for (char ch = 'a'; ch <= 'z'; ch++)
      group.insert(ch);
    for (char ch = '0'; ch <= '9'; ch++)
      group.insert(ch);
    group.insert('_');
    group.insert('$');
    return new InGroupSplitter(group);
  }
  virtual void addByte(char c)
  {
    if (_state == Printed)
      _state = ReadyToStart;
    if (_group.count(c))
    {
      if (_state == ReadyToStart)
      {
	_state = Running;
	_soFar = c;
      }
      else if (_state == Running)
      {
	if (_soFar.length() >= getMaxSize())
	  _state = Aborted;
	else
	  _soFar += c;
      }
    }
    else
    {
      if ((_state == Running) && (_soFar.length() >= getMinSize()))
	_state = Printed;
      else
	_state = ReadyToStart;
    }
  }
  virtual bool newString() const
  { 
    return _state == Printed;
  }
  virtual std::string getNewString() const { return _soFar; }
};

class Splitters
{
private:
  static const int MAX_MRU = 10;
  std::vector< Splitter * > _splitters;
  std::map< char, std::vector< std::string > > _mrus;
  int64_t _tried;
  int64_t _matched;
  double _costYesOrNo;
  double _costIndex;
  int64_t _bytesSaved;
public:
  Splitters() : _tried(2), _matched(1), _costYesOrNo(0), _costIndex(0),
		_bytesSaved(0)
  {
    _splitters.push_back(new QuoteSplitter('\''));
    _splitters.push_back(new QuoteSplitter('"'));
    _splitters.push_back(InGroupSplitter::letters());
    _splitters.push_back(InGroupSplitter::symbolish());
  }
  ~Splitters()
  {
    for (Splitter *splitter: _splitters)
    {
      delete splitter;
    }
  }
  void saveByte(char ch)
  {
    std::set< std::string > foundThisTime;
    for (Splitter *splitter : _splitters)
    {
      splitter->addByte(ch);
      if (splitter->newString())
        foundThisTime.insert(splitter->getNewString());
    }
    for (std::string const &string : foundThisTime)
    {
      auto &mru = _mrus[string[0]];
      auto it = std::find(mru.begin(), mru.end(), string);
      if (it == mru.end())
      { // Not found.  Add a new item.
	if (mru.size() == MAX_MRU)
	  mru.erase(mru.begin());
	mru.push_back(string);
      }
      else
      { // Move the item to the end of the list, the same place we would
	// have put it if it was new.  
	std::rotate(it, it+1, mru.end());
      }
    }
  }
  char const *tryPrint(char const *begin, char const *ptr, char const *end)
  {
    //return NULL; // disabled!
    if (begin >= ptr)
      return NULL;
    MatchedResult result = matched(ptr-1, end);
    if (!result.count)
      return NULL;
    const double chanceOfMatch = _matched / (double)_tried;
    _tried++;
    if (!result.matched())
    {
      _costYesOrNo += pCostInBits(1 - chanceOfMatch);
      return NULL;
    }
    _matched++;
    _costYesOrNo += pCostInBits(chanceOfMatch);
    // TODO I was looking at a way to weight these.  Presumably the most
    // recent one would have a better chance than the oldest one.  I need
    // to collect more data to see if we can do anything with that.
    _costIndex += pCostInBits(1.0 / result.count);
    _bytesSaved += result.moveTo - ptr;
    return result.moveTo;
  }
  struct MatchedResult
  {
    char const *moveTo;
    bool matched() const { return moveTo; }
    int index;
    int count;
    MatchedResult() : moveTo(NULL), index(-1), count(0) { }
  };
  MatchedResult matched(char const *begin, char const *end)
  {
    MatchedResult result;
    if (begin >= end)
      return result;
    const auto mruIt = _mrus.find(*begin);
    if (mruIt == _mrus.end())
      return result;
    auto mru = mruIt->second;
    result.count = mru.size();
    const uintptr_t maxLength = end - begin;
    unsigned int bestMatchSize = 0;
    for (int i = 0; i < result.count; i++)
    {
      const std::string &possibleMatch = mru[i];
      if (possibleMatch.length() < bestMatchSize)
	continue;
      if (possibleMatch.length() > maxLength)
	continue;
      if (memcmp(begin, possibleMatch.c_str(), possibleMatch.length()))
	continue;
      result.moveTo = begin + possibleMatch.length();
      result.index = i;
    }
    if (result.matched())
    { // Move this item to the end of the list.  As if it was just created
      // by saveByte().
      std::rotate(mru.begin() + result.index, mru.begin() + result.index + 1,
		  mru.end());     
      // We store the list with the highest index reserved for the most
      // recent item and index 0 reserved for the oldest.  We do that so
      // we can use push_back() instead of insert() in the common case.
      // We report the result in the opposite order, with index 0 pointing
      // to the most recent value, 1 pointing to the second most recent, etc.
      result.index = result.count - result.index - 1;
    }
    return result;
  }

  double getTotalCostInBits() const { return _costIndex + _costYesOrNo; }

  void dump(std::ostream &out)
  {
    out<<"𝕊𝕡𝕝𝕚𝕥𝕥𝕖𝕣𝕤:  y/n cost in bytes="<<(_costYesOrNo/8)
       <<", index cost in bytes="<<(_costIndex/8)
       <<", bytes saved="<<_bytesSaved
       <<", tried="<<_tried
       <<", matched="<<_matched
       <<" "<<(_matched*100.0/_tried)<<"%"<<std::endl;
  }

};

class NewChars
{
private:
  int64_t _charsInspected;
  std::set< char > _found;
  double _totalCostInBits;
public:
  NewChars() : _charsInspected(0), _totalCostInBits(0)
  {
    
  }

  // Returns true if this was a new character that we have not seen before,
  // false otherwise.  Returns true if this code completely handled the
  // character, false if someone else should try to encode the character.
  bool tryPrintChar(char ch)
  {
    const auto newCharsFound = _found.size();
    const auto newCharsAllowed = 256 - newCharsFound;
    double chanceOfNewChar;
    if (_found.size() == 0)
      chanceOfNewChar = 1;
    else if (newCharsAllowed == 0)
      chanceOfNewChar = 0;
    else
    { // We need to maintain an average.  But each time we print a new char
      // we adjust the average down because there are fewer characters
      // remaining.
      //
      // How to maintain an average.  We alreay know how many unique characters
      // have been used and how many are available and we know how many
      // bytes we have processed.  So the formula is ((number of unique
      // characters used) / (number of bytes processed)) * (number of
      // unique characters available) / 256.
      chanceOfNewChar = newCharsFound /(double)_charsInspected
	* newCharsAllowed / 256;
    }
    bool newCharFound = !_found.count(ch);
    const double chanceOfWhatHappened =
      newCharFound?chanceOfNewChar:(1-chanceOfNewChar);
    const double costInBits = pCostInBits(chanceOfWhatHappened);
    _totalCostInBits += costInBits;
    return newCharFound;
  }
  void updateStats(char ch)
  {
    _found.insert(ch);    
    _charsInspected++;
  }

  double getTotalCostInBits() const { return _totalCostInBits; }

  void dump(std::ostream &out)
  {
    out<<"ℕ𝕖𝕨ℂ𝕙𝕒𝕣𝕤:  cost in bytes="<<(_totalCostInBits/8)
       <<", inspected="<<_charsInspected
       <<", unique found="<<_found.size()<<std::endl;
  }
};

void tryFile(File const &file)
{
  NewChars newChars;
  NoContext noContext;
  OneByteContext oneByteContext;
  std::map< char, Monitor > oneByte;
  std::map< uint16_t, MegaMonitor > twoBytes;
  Splitters splitters;
  //bool splittersPrimed = false;
  char const *nextReadPtr = NULL;
  for (char const *readPtr = file.begin();
       readPtr < file.end();
       readPtr = nextReadPtr)
  { // encode *readPtr
    nextReadPtr = readPtr + 1;
    if (!newChars.tryPrintChar(*readPtr))
    {
      if (char const *jumpTo =
	  splitters.tryPrint(file.begin(), readPtr, file.end()))
	nextReadPtr = jumpTo;
      else 
      {
	std::set< char > toIgnore;
	// TODO more!
	if (!oneByteContext.tryPrintChar(readPtr, file, toIgnore))
	  noContext.printChar(*readPtr);
      }
    }

    // Update the stats based on what the decompressor now knows.
    for (char const *ptr = readPtr; ptr < nextReadPtr; ptr++)
    {
      newChars.updateStats(*ptr);
      splitters.saveByte(*ptr);
      noContext.updateStats(*ptr);
      oneByteContext.updateStats(ptr, file);
      if (ptr > file.begin())
      {
	oneByte[(unsigned char)ptr[-1]].increment(*ptr);
	if (ptr > file.begin() + 1)
	{
	  uint16_t const *context = (uint16_t const *)ptr;
	  context--;
	  twoBytes[*context].increment(file.begin(), ptr);
	}
      }
    }
  }
  const int64_t size = file.end() - file.begin();
  std::cout<<"Input file size:  "<<size<<std::endl;
  std::cout<<"================================================"<<std::endl
	   <<"context: 0 bytes"<<std::endl;
    noContext.dump(std::cout);
  std::cout<<"================================================"<<std::endl
           <<"context: 1 byte"<<std::endl;
  Accumulator a1;
  for (auto const &kvp : oneByte)
    a1.add(kvp.second);
  a1.dump(std::cout);
  /*
  std::cout<<"================================================"<<std::endl
           <<"context: 2 bytes"<<std::endl;
  MegaAccumulator a2;
  for (auto const &kvp : twoBytes)
    a2.add(kvp.second);
  a2.dump(std::cout);
  */
  newChars.dump(std::cout);
  splitters.dump(std::cout);
  oneByteContext.dump(std::cout);
  const double totalCostInBytes =
    (noContext.getTotalCostInBits() + newChars.getTotalCostInBits()
     + splitters.getTotalCostInBits() + oneByteContext.getTotalCostInBits())/8;
  std::cout<<"𝕋𝕆𝕋𝔸𝕃: "<<totalCostInBytes<<" bytes.  "
	   <<(100 - totalCostInBytes/(file.end() - file.begin())*100)
	   <<"% savings."<<std::endl;
}

void costOfNewChars(File const &file)
{
  std::set< char > found;
  double totalCost = 0;
  double maxCost = 0;
  double minCost = 100;
  for (auto ptr = file.begin(); ptr != file.end(); ptr++)
  {
    const int numberOfCharsFound = found.size();
    if ((numberOfCharsFound > 0) && (numberOfCharsFound < 256))
    { // Compute cost.
      const auto bytesProcessed = ptr - file.begin();
      const double chanceBasedOnProcessed = (bytesProcessed == 0)
	?1.0
	:(numberOfCharsFound/(double)bytesProcessed);
      const auto bytesRemaining = file.end() - ptr;
      const double chanceBasedOnRemaining = (bytesRemaining == 0)
	?1.0
	:((256 - numberOfCharsFound) / (double)bytesRemaining);	
      const double chanceOfNew =
	std::min(chanceBasedOnProcessed, chanceBasedOnRemaining);
      // This could fail, probably, for small files.
      assert((chanceOfNew > 0 && (chanceOfNew < 1)));
      //td::cout<<"chhanceOfNew ="<<chanceOfNew<<std::endl;
      const bool foundNewChar = !found.count(*ptr);
      const double chance = foundNewChar?chanceOfNew:(1-chanceOfNew);
      const double costInBits = pCostInBits(chance);
      totalCost += costInBits;
      maxCost = std::max(maxCost, costInBits);
      minCost = std::min(minCost, costInBits);
    }
    found.insert(*ptr);
  }
  std::cout<<"Min cost in bits:  "<<minCost<<std::endl
	   <<"Max cost in bits:  "<<maxCost<<std::endl
	   <<"Number of unique chars:  "<<found.size()<<std::endl
	   <<"Size of input file:  "<<(file.end() - file.begin())<<std::endl
	   <<"Total cost in bits:  "<<totalCost<<std::endl;
}

void trySplitters(File &file)
{
  std::vector< Splitter * > splitters;
  splitters.push_back(new QuoteSplitter('\''));
  splitters.push_back(new QuoteSplitter('"'));
  splitters.push_back(InGroupSplitter::letters());
  splitters.push_back(InGroupSplitter::symbolish());
  std::map< std::string, int > found;
  std::map< char, std::set< std::string > > foundByFirst;
  std::map< int, int > foundByLength;
  std::map< int, int > reusedByLength;
  std::map< int, int > reusedByBinCount;
  for (char const *ptr = file.begin(); ptr != file.end(); ptr++)
  {
    std::set< std::string > foundThisTime;
    for (Splitter *splitter : splitters)
    {
      splitter->addByte(*ptr);
      if (splitter->newString())
	foundThisTime.insert(splitter->getNewString());
    }
    for (std::string const &s : foundThisTime)
    {
      int &countForThisString = found[s];
      countForThisString++;
      if (countForThisString == 1)
      { // First occurance.  Save it.
	foundByFirst[s[0]].insert(s);
	foundByLength[s.length()]++;
      }
      else
      {
	reusedByLength[s.length()]++;
	reusedByBinCount[foundByFirst[s[0]].size()]++;
      }
    }
  }
  int64_t totalSavings = 0;
  for (auto const &kvp : found)
  {
    const std::string s = kvp.first;
    const auto count = kvp.second;
    std::cout<<count<<'\t'<<s<<std::endl;
    totalSavings += (count - 1) * (s.length() - 1);
  }
  std::cout<<"total possible savings:  "<<totalSavings
	   <<", "<<(totalSavings /(double)(file.end()-file.begin())*100)
	   <<"%"<<std::endl;

  std::cout<<"length\tfound\treused\t%"<<std::endl;
  for (auto const &kvp : foundByLength)
  {
    auto const length = kvp.first;
    auto const found = kvp.second;
    auto const reused = reusedByLength[length];
    std::cout<<length<<'\t'<<found<<'\t'<<reused<<'\t'
	     <<(reused/(double)found*100)<<std::endl;
  }

  std::map< int, int > availableBinsByBinCount;
  for (auto const &kvp : foundByFirst)
  {
    auto const binCount = kvp.second.size();
    availableBinsByBinCount[binCount]++;
    reusedByBinCount[binCount];
  }
  std::cout<<"bin count\tfound\treuse count"<<std::endl;
  for (auto const &kvp : reusedByBinCount)
  {
    auto const binCount = kvp.first;
    auto const found = availableBinsByBinCount[binCount];
    auto const reused = kvp.second;
    auto const percent = reused / (double)found * 100;
    std::cout<<binCount<<"\t\t"<<found<<'\t'<<reused<<'\t'<<percent<<std::endl;
  }
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
  {
    char const *fileName = argv[i];
    std::cout<<"======== "<<fileName
	     <<" ================================================"<<std::endl;
    File file(fileName);
    if (!file.valid())
      std::cout<<"Error:  "<<file.errorMessage()<<std::endl;
    else
      //trySplitters(file);
      tryFile(file);
    //costOfNewChars(file);
  }
}
