#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cmath>
#include "../shared/File.h"
#include "../shared/RansHelper.h"

/**
 * One type of compression involves tagging some strings as interesting when we
 * first read them, then referring to these strings later by index number.
 *
 * The implementation involved a hash table.  However, in a lot of ways this
 * algorithm is like the LZ78 algorithm.  They both build a dictionary based
 * on what was previously read.  Repeating a string is cheap because you only
 * have to specify an index number.  This algorithm is different in part because
 * it is free to overwrite an index with newer data.
 *
 * This works best when limited to strings with a length between 4 and 6 bytes.
 */
class HashListCounter
{
private:
  int _sourceBytesEncoded;
  // The total cost of everything we've been asked to output.
  // We aren't really outputting anything yet, but we can create a decent
  // approximation of what will come out of the entry encoder.
  double _costInBits;
  // The total of all of the values in the _counts map.
  // This will be the denominator when we talk to the entropy encoder.
  double _totalCount;
  // This is a map from the index number to the popularity of that index.
  // 0 means that the index is not currently available.
  // Larger numbers mean that the index is more popular.
  // The popularity is used as the numerator when we talk to the entropy encoder.
  std::map<int, int> _counts;

public:
  HashListCounter() : _sourceBytesEncoded(0), _costInBits(0.0), _totalCount(0.0) {}

  // Call this any time we write to an index.
  // Before the first call to write to an index you can't try to read from that index.
  void save(int index)
  { // There is a 0% probability of using an empty spot.
    // We assume the probability goes up every time it gets used.
    // I.e. the more something's been used in the past, the more we expect it to be used in the future.
    int &count = _counts[index];
    if (count == 0)
    { // As a special case, each index gets 1 here when we first set the value.
      // So this will have the smallest non 0 probability of being used.
      count = 1;
      _totalCount++;
      // std::cout<<"save("<<index<<")"<<std::endl;
    }
  }

  // Call this any time we read from an index.
  void use(int index)
  {
    int &count = _counts[index];
    const double newCost = pCostInBits(count / _totalCount);
    /*
    std::cout<<"use("<<index<<") count="<<count
    <<", _totalCount="<<_totalCount<<", newCost="<<newCost
    <<", _costInBits="<<_costInBits<<std::endl;
    */
    _costInBits += newCost;
    count += 1;
    _totalCount += 1;
    _sourceBytesEncoded += 5; // Ouch!  This should be the length of the string, but I don't know that so I had to guess.
  }

  // Write multiple lines of debug data.
  void dump(std::ostream &out) const
  {
    std::map<int, int> binCounter;
    for (auto const &kvp : _counts)
    {
      const int useCount = kvp.second;
      binCounter[useCount]++;
    }
    for (auto const &kvp : binCounter)
    {
      const int useCount = kvp.first;
      const int indexCount = kvp.second;
      out << indexCount << " indicies were each used " << (useCount - 1) << " times." << std::endl;
    }
  }

  // How many (simulated) bits did it take to do all of the writes?
  double getCostInBits() const { return _costInBits; }

  // How much of the input were we able to compress.
  int sourceBytesEncoded() const { return _sourceBytesEncoded; }
};

// https://en.wikipedia.org/wiki/ANSI_escape_code
// All of these work in the standard mac terminal window.
// Blink does not work in the terminal integrated into VS Code.
// Nothing works when debugging in VS Code.  (DEBUG CONSOLE tab)
static char const *ansiReset = "\x1b[39;49;25m";
static char const *ansiBlink = "\x1b[5m";
static char const *ansiYellow1 = "\x1b[93;100m";
static char const *ansiYellow2 = "\x1b[90;103m";
static char const *ansiBlue1 = "\x1b[37;44m";
static char const *ansiBlue2 = "\x1b[34;47m";

class AlternateColors
{
private:
  char const *const _first;
  char const *const _second;
  char const *_last;

public:
  AlternateColors(char const *first, char const *second) : _first(first), _second(second), _last(second) {}
  char const *next()
  {
    if (_last == _first)
    {
      _last = _second;
    }
    else
    {
      _last = _first;
    }
    return _last;
  }
  static AlternateColors yellow;
  static AlternateColors blue;
};

AlternateColors AlternateColors::yellow(ansiYellow1, ansiYellow2);
AlternateColors AlternateColors::blue(ansiBlue1, ansiBlue2);

// Only true for debug and development!  This prints the entire
//  input file, and uses colors to show which text was compressed with
//  which algorithm.
bool echoAllInput = false;

/**
 * This class is responsible for "sliding window" or LZ77 style of compression.
 *
 * Objects of this class keep a lot of debug statistics.
 */
class SlidingWindow
{
private:
  File &_input;

  /** For debug only.  How many times was tryToCompress() called? */
  int _attempts;

  /** For debug only.  How many times did tryToCompress() succeed? */
  int _successes;

  /** For debug only.  How many bytes of input did tryToCompress() consume? */
  int _bytesCompressed;

  /**
   * For debug only. How many times did the string we were copying reference itself?
   * E.g. 123123123123123123123
   */
  int _selfReferences;

  /**
   * The estimated size of all the compressed output we created.
   */
  double _costInBits;

  /**
   * left and right should point into the file.
   * What's the longest string that's the same, starting and left and at right?
   * Returns the number of bytes that match before the first byte that does not match.
   */
  int compare(char const *left, char const *right)
  { // Tons of room for optimization here!
    assert(left < right);
    char const *const initialLeft = left;
    while (right < _input.end() && *left == *right)
    {
      right++;
      left++;
    }
    return left - initialLeft;
  }

public:
  SlidingWindow(File &input) : _input(input), _attempts(0), _successes(0), _bytesCompressed(0), _selfReferences(0), _costInBits(0.0) {}

  /**
   * Looks at the current state of the data.
   * If possible, writes some compressed data, updates startOfUncompressed and returns true.
   * Otherwise returns false, writes nothing, and leaves startOfUncompressed alone.
   */
  bool tryToCompress(char const *&startOfUncompressed)
  {
    _attempts++;
    char const *bestStart = NULL;
    int bestLength = 8;
    const auto bytesOfHistory = std::min((size_t)20000, (size_t)(startOfUncompressed - _input.begin()));
    // std::cout<<"bytesOfHistory="<<bytesOfHistory<<std::endl;
    for (char const *possibleMatch = startOfUncompressed - bytesOfHistory; possibleMatch < startOfUncompressed - 1; possibleMatch++)
    { // Should be break out of this loop early if there isn't enough space before the end of the file for a match?
      // Just an idea of optimizing speed.  Doesn't seem like it would matter most of the time.
      // std::cout<<"almost..."<<std::endl;
      const auto newMatchLength = compare(possibleMatch, startOfUncompressed);
      // std::cout<<"newMatchLength="<<newMatchLength<<std::endl;
      if (newMatchLength >= bestLength)
      { // In case of a tie, take the last location.
        bestStart = possibleMatch;
        bestLength = newMatchLength;
      }
    }
    if (bestStart == NULL)
    { // We were not able to do any compression.
      return false;
    }
    else
    {
      const bool selfReferencing = bestLength + bestStart > startOfUncompressed;
      // std::cout << "SW back="<<AlternateColors::yellow.next() << (startOfUncompressed - bestStart) << ansiReset<< ", len=" << AlternateColors::blue.next()<< bestLength<<ansiReset;
      if (selfReferencing)
      {
        _selfReferences++;
        // std::cout << " selfReferencing";
      }
      //<<", body=❝"<<std::string(bestStart, bestLength)
      //<<", startOfUncompressed=❝"<<std::string(startOfUncompressed, 3)<<"❞"
      // std::cout << std::endl;
      if (echoAllInput)
      {
        if (selfReferencing)
        {
          std::cout << ansiBlink;
        }
        std::cout << AlternateColors::blue.next() << std::string(bestStart, bestLength) << ansiReset;
      }
      _successes++;
      _bytesCompressed += bestLength;
      startOfUncompressed += bestLength;
      return true;
    }
  }
  /**
   * The estimated size of all the compressed output we created.
   */
  double costInBits() const { return _costInBits; }

  /** Sends  small amount of data */
  void debugOut(std::ostream &out) const
  {
    out << "(SlidingWindow: _attempts=" << _attempts
        << ", _successes=" << _successes
        << ", _bytesCompressed=" << _bytesCompressed
        << ", _selfReferences=" << _selfReferences
        << ", _costInBits=" << _costInBits << ")";
  }
};

/**
 * Store a list of 0 to 7 bytes, in order.
 *
 * This is used in some places to keep statistics about recently found bytes.
 * E.g. What bytes have we recently seen following a "q" or following a " q"?
 *
 * These objects are intended to be efficient in time and space.
 * A traditional alternative is a map from bytes to ints, so we can keep
 * track of all bytes we've seen in this situation, not just the last 7.
 */
class ShortCharList
{
private:
  // The least significant byte of the _queue is the length of the queue.
  // The second least significant byte is the newest item in the queue
  // (assuming the queue has at least one item it it).
  // The most significant byte is only used if the queue is full, and if
  // so it contains the oldest byte.
  uint64_t _queue;
  unsigned char getSize() const { return _queue & 0xff; }
  void setSize(unsigned char newSize)
  {
    _queue &= 0xffffffffffffff00;
    _queue |= newSize;
  }

public:
  /* Starts with an empty queue of bytes. */
  ShortCharList() : _queue(0) {}

  /** Add this byte to the list.  If the list is already full, remove the oldest byte. */
  void addToQueue(char ch)
  {
    unsigned char size = getSize();
    if (size < 7)
    {
      size++;
    }
    setSize(ch);
    _queue <<= 8;
    setSize(size);
  }
  double probability(char ch) const
  {
    const unsigned char size = getSize();
    if (size == 0)
    { // avoid 0/0
      return 0.0;
    }
    int found = 0;
    auto queue = _queue;
    for (unsigned char i = 0; i < size; i++)
    {
      queue >>= 8;
      if ((queue & 0xff) == ch)
      {
        found++;
      }
    }
    return found / (double)size;
  }
  // What portion of the of the items in the queue (a) match the input char and
  // (b) are not yet marked as impossible.
  //
  // Return 0 if ch is not in the queue at all, even if the queue is empty.
  // I.e. 0/0 returns 0.
  //
  // Mark all items that we saw in the queue as impossible.  I.e. if we looked at
  // this queue and we decided to keep looking, that must mean that the byte we
  // are looking for is not one of these.
  double probability(char ch, std::set<char> &impossible) const
  {
    const unsigned char size = getSize();
    int denominator = 0;
    int numerator = 0;
    auto queue = _queue;
    for (unsigned char i = 0; i < size; i++)
    {
      queue >>= 8;
      const char toExamine = queue;
      if (toExamine == ch)
      {
        numerator++;
        denominator++;
      }
      else if (!impossible.count(toExamine))
      {
        denominator++;
      }
    }
    queue = _queue;
    for (unsigned char i = 0; i < size; i++)
    {
      queue >>= 8;
      const char toDisable = queue;
      impossible.insert(toDisable);
    }
    if (numerator == 0)
    {
      return 0;
    }
    else
    {
      return numerator / (double)denominator;
    }
  }
  /**
   * Remove the oldest bytes.  This is never actually required, but sometimes
   * we want to mark the data before a certain point as old, and less interesting
   * than the data that comes after it.
   *
   * If count is greater than the size of the queue, remove all bytes.
   */
  bool remove(unsigned int count = 1)
  {
    auto const size = getSize();
    if (count >= size)
    {
      setSize(0);
      return true; // Emtpy.
    }
    else
    {
      setSize(size - count);
      return false; // Not empty.
    }
  }
  /**
   * Report the bytes in the queue.  The most recently
   * added byte will be first.
   *
   * Ideally the control characters would be cleaned
   * up for printing to standard out.  TODO.
   *
   * The would be optimized if it was used for anything
   * but debugging.
   */
  std::string debugString() const
  {
    auto const size = getSize();
    std::string result;
    auto queue = _queue;
    for (unsigned char i = 0; i < size; i++)
    {
      queue >>= 8;
      const char toCopy = queue;
      switch (toCopy)
      {
      case '\t':
      {
        result += "⇥";
        break;
      }
      case '\n':
      {
        result += "↚";
        break;
      }
      case ' ':
      {
        result += "·";
        break;
      }
      default:
      {
        result += toCopy;
        break;
      }
      }
    }
    return result;
  }
};

class HashOfStats
{
private:
  File &_file;
  const int _bytesOfContext;
  const int _weight;
  std::vector<ShortCharList> _counters;
  double _costInBits;
  std::map<int, int> _numberOfFreePasses; // Key is the number of items in the queue at the time.
  std::map<int, int> _numberOfTimesThisSolvedIt;
  std::map<int, int> _numberOfTimesThisWasNotHelpful;
  int getIndex(char const *toCompress)
  {
    const auto bytesOfHistory = toCompress - _file.begin();
    if (bytesOfHistory < _bytesOfContext)
    {
      return -1;
    }
    else
    {
      return simpleHash(toCompress - _bytesOfContext, toCompress) % _counters.size();
    }
  }

public:
  HashOfStats(File &file, int bytesOfContext, size_t size, int weight) : _file(file), _weight(weight), _bytesOfContext(bytesOfContext), _counters(size), _costInBits(0.0) {}
  ~HashOfStats()
  {
    std::cout << "[HashOfStats bytesOfContext=" << _bytesOfContext << ", size=" << _counters.size() << ", weight="<<_weight
    <<", _costInBits="<<_costInBits;
    std::map<std::string, int> counts;
    for (auto const &counter : _counters)
    {
      counts[counter.debugString()]++;
    }
    for (auto const &kvp : counts)
    {
      std::cout << ' ' << kvp.second << ":" << kvp.first;
    }
    std::cout << "]" << std::endl;
  }
  bool tryToCompress(char const *toCompress, std::set<char> &impossible)
  {
    const auto index = getIndex(toCompress);
    if (index < 0)
    {
      return false;
    }
    auto &counter = _counters[index];
    auto const probability = counter.probability(*toCompress, impossible);
    // TODO consider the cost of saying that this is or is not to be used.
    if (probability == 0)
    {
      return false;
    }
    // This would cost 0 if there was only one byte in history and it matched the current byte.
    _costInBits += pCostInBits(probability);
    return true;
  }
  void storeContext(char const *toCompress)
  {
    const auto index = getIndex(toCompress);
    if (index >= 0)
    {
      _counters[index].addToQueue(*toCompress);
    }
  }
  double costInBits() const { return _costInBits; }
};

class TwoByteStats
{
private:
  File &_file;
  ShortCharList _counters[0x10000];
  double _costInBits;
  ShortCharList *getCounter(char const *toCompress)
  {
    const auto bytesOfHistory = toCompress - _file.begin();
    if (bytesOfHistory < 2)
    {
      return NULL;
    }
    else
    {
      return &_counters[*(((uint16_t const *)toCompress) - 1)];
    }
  }

public:
  TwoByteStats(File &file) : _file(file), _costInBits(0.0) {}
  bool tryToCompress(char const *toCompress, std::set<char> &impossible)
  {
    if (auto counter = getCounter(toCompress))
    {
      auto const probability = counter->probability(*toCompress, impossible);
      // TODO consider the cost of saying that this is or is not to be used.
      if (probability == 0)
      {
        return false;
      }
      // This would cost 0 if there was only one byte in history and it matched the current byte.
      _costInBits += pCostInBits(probability);
      return true;
    }
    else
    {
      return false;
    }
  }
  void storeContext(char const *toCompress)
  {
    if (auto counter = getCounter(toCompress))
    {
      counter->addToQueue(*toCompress);
    }
  }
  double costInBits() const { return _costInBits; }
};

class OneByteStats
{
private:
  File &_file;
  ShortCharList _counters[0x100];
  double _costInBits;
  ShortCharList *getCounter(char const *toCompress)
  {
    const auto bytesOfHistory = toCompress - _file.begin();
    if (bytesOfHistory < 1)
    {
      return NULL;
    }
    else
    {
      return &_counters[(uint8_t) * (toCompress - 1)];
    }
  }

public:
  OneByteStats(File &file) : _file(file), _costInBits(0.0) {}
  bool tryToCompress(char const *toCompress, std::set<char> &impossible)
  {
    if (auto counter = getCounter(toCompress))
    {
      auto const probability = counter->probability(*toCompress, impossible);
      // TODO consider the cost of saying that this is or is not to be used.
      if (probability == 0)
      {
        return false;
      }
      // This would cost 0 if there was only one byte in history and it matched the current byte.
      _costInBits += pCostInBits(probability);
      return true;
    }
    else
    {
      return false;
    }
  }
  void storeContext(char const *toCompress)
  {
    if (auto counter = getCounter(toCompress))
    {
      counter->addToQueue(*toCompress);
    }
  }
  double costInBits() const { return _costInBits; }
};

class LastResort
{
private:
  double _costInBits;

public:
  LastResort() : _costInBits(0.0) {}
  void tryToCompress(char const *&toCompress, std::set<char> const &impossible)
  {
    assert(!impossible.count(*toCompress));
    // We've marked 0 or more items as impossible.  What is the probability of finding
    // the given character in the list of bytes that are still possible?
    // Assume all possible bytes have equal probability.
    const double probability = 1.0 / (256 - impossible.size());
    _costInBits += pCostInBits(probability);
  }

  double costInBits() const { return _costInBits; }
};

class OneByteAtATime
{
private:
  std::vector<HashOfStats> _hashedStats;
  TwoByteStats _twoByteStats;
  OneByteStats _oneByteStats;
  LastResort _lastResort;
  int _inputCount;

public:
  OneByteAtATime(File &file) : _twoByteStats(file), _oneByteStats(file), _inputCount(0)
  {
    _hashedStats.emplace_back(file, 7, 1021, 16);
    _hashedStats.emplace_back(file, 6, 2053, 8);
    _hashedStats.emplace_back(file, 5, 2053, 4);
    _hashedStats.emplace_back(file, 4, 4093, 2);
    _hashedStats.emplace_back(file, 3, 4093, 1);
  }
  void compress(char const *toCompress)
  {
    _inputCount++;
    std::set<char> impossible;
    bool compressed = false;
    for (HashOfStats &hashedStats : _hashedStats)
    {
      compressed = hashedStats.tryToCompress(toCompress, impossible);
      if (compressed)
        break;
    }
    compressed || _twoByteStats.tryToCompress(toCompress, impossible) || _oneByteStats.tryToCompress(toCompress, impossible) || (_lastResort.tryToCompress(toCompress, impossible), true);
    for (HashOfStats &hashedStats : _hashedStats)
    {
      hashedStats.storeContext(toCompress);
    }
    _twoByteStats.storeContext(toCompress);
    _oneByteStats.storeContext(toCompress);
  }
  double costInBits() const
  {
    double result = _lastResort.costInBits() + _oneByteStats.costInBits() + _twoByteStats.costInBits();
    for (HashOfStats const &hashedStats : _hashedStats)
    {
      result += hashedStats.costInBits();
    }
    return result;
  }
  int inputCount() const { return _inputCount; }
};

/**
 * Compress the file.  The output is simulated, then summarized on the screen.
 *
 * file is the input to be compressed.
 *
 * hashBufferSize, minHashEntrySize, maxHashEntrySize are inputs to the hash compression
 * algorithm.  They are inputs so we can easily try different values and compare the
 * results.
 */
void processFileRange(File &file, int hashBufferSize, int minHashEntrySize, int maxHashEntrySize)
{
  HashListCounter hashListCounter;
  // std::cout<<"processFile()"<<std::endl;
  size_t hashEntries = 0;
  std::vector<std::string> hashBuffer(hashBufferSize);
  auto current = file.begin();
  auto const recordNewHash = [&]()
  {
    auto const bytesProcessed = current - file.begin();
    for (int hashEntrySize = minHashEntrySize; hashEntrySize <= maxHashEntrySize; hashEntrySize++)
    {
      if (bytesProcessed >= hashEntrySize)
      {
        const std::string newEntry(current - hashEntrySize, hashEntrySize);
        const int index = simpleHash(newEntry) % hashBufferSize;
        hashBuffer[index] = newEntry;
        hashListCounter.save(index);
      }
    }
  };
  SlidingWindow slidingWindow(file);
  OneByteAtATime oneByteAtATime(file);

  while (current < file.end())
  {
    bool madeProgress = slidingWindow.tryToCompress(current);
    const int64_t bytesRemaining = file.end() - current;
    for (int hashEntrySize = minHashEntrySize;
         (!madeProgress) && (hashEntrySize <= maxHashEntrySize) && (hashEntrySize <= bytesRemaining);
         hashEntrySize++)
    {
      const std::string possibleEntry(current, hashEntrySize);
      // std::cout<<"possibleEntry="<<possibleEntry<<std::endl;
      const int index = simpleHash(possibleEntry) % hashBufferSize;
      // std::cout<<"possibleEntry="<<possibleEntry<<", index="<<index<<""
      if (hashBuffer[index] == possibleEntry)
      {
        if (echoAllInput)
        {
          std::cout << AlternateColors::yellow.next() << possibleEntry << ansiReset;
        }
        madeProgress = true;
        current += hashEntrySize;
        hashEntries++;
        // std::cout<<"hash entry:  "<<possibleEntry<<std::endl;
        hashListCounter.use(index);
      }
    }
    if (!madeProgress)
    {
      if (echoAllInput)
      {
        std::cout << *current;
      }
      oneByteAtATime.compress(current);
      current++;
      // recordNewHash();
    }
  }
  // std::cout<<"HERE A"<<std::endl;
  std::map<int, int> hashBufferLengths;
  for (std::string const &hashEntry : hashBuffer)
  {
    hashBufferLengths[hashEntry.length()]++;
  }
  for (auto const &kvp : hashBufferLengths)
  {
    auto const length = kvp.first;
    auto const count = kvp.second;
    if (length == 0)
    {
      std::cout << "Free";
    }
    else
    {
      std::cout << length << "bytes";
    }
    std::cout << ": count=" << count << std::endl;
  }
  // hashListCounter.dump(std::cout);
  auto const hashBufferFree = hashBufferLengths[0];
  // Assume 1 bit for simplicity.  The entropy encoded can probably do better, but not by much.
  const auto decisionCostInBits = (hashEntries * 2 /*+ individualBytes*/);
  // For simplicity just look at the maximum size of the buffer.  in fact many of the entries will
  // be written when the buffer is mostly empty, so it will take less space to write this value
  // than shown here.
  // For simplicity assume that all indexes are just as likely.  At some point it's worth checking
  // to see if this is try.  If some index values come up a lot more than others, then the
  // entropy encoder can help us even more.
  const auto simpleHashCodeCostInBits = std::log(hashBufferSize - hashBufferFree) / std::log(2) * hashEntries;
  const auto betterHashCodeCostInBits = hashListCounter.getCostInBits();
  // If we only look at the of the input file that the hashing algorithm was able to compresses,
  // and the corresponding compressed output, how much of a savings did we get?  It's a percent, in the
  // same format used by gzip to report its savings.
  const auto hashSavings = (hashListCounter.sourceBytesEncoded() - betterHashCodeCostInBits / 8) * 100.0 / hashListCounter.sourceBytesEncoded();
  // Assume that each of these will be written out literally.
  // In fact there is a plan to compress bytes that aren't in the buffer.
  // At the moment we're only testing the hashing part of the algorithm.
  const auto oneByteAtATimeCostInBits = oneByteAtATime.costInBits();
  const double totalCostInBytes = std::ceil((decisionCostInBits + betterHashCodeCostInBits + oneByteAtATimeCostInBits + slidingWindow.costInBits()) / 8);
  auto const fileSize = file.end() - file.begin();
  std::cout << "processFileRange() fileSize=" << fileSize
            << ", hashBufferSize=" << hashBufferSize
            << ", hashBufferFree=" << hashBufferFree
            << ", hashEntrySize=" << minHashEntrySize << '-' << maxHashEntrySize
            << ", hashEntries=" << hashEntries
            << ", simpleHashCodeCostInBits=" << simpleHashCodeCostInBits
            << ", betterHashCodeCostInBits=" << betterHashCodeCostInBits
            << ", hashSavings=" << hashSavings
            << ", oneByteAtATimeCostInBits=" << oneByteAtATimeCostInBits
            << ", oneByteInputCount=" << oneByteAtATime.inputCount()
            << ", totalCostInBytes=" << totalCostInBytes
            << ", savings=" << ((fileSize - totalCostInBytes) * 100.0 / fileSize) << '%'
            << " ";
  slidingWindow.debugOut(std::cout);
  std::cout << std::endl;
}

int main(int argc, char **argv)
{
  if (echoAllInput)
  {
    std::cout << "Legend: " << AlternateColors::blue.next()
              << "Long Strings" << ansiReset << ", "
              << AlternateColors::yellow.next() << "Medium Strings"
              << ansiReset << ", not yet compressed" << std::endl;
  }
  for (int i = 1; i < argc; i++)
  {
    char const *const fileName = argv[i];
    std::cout << "File name: " << fileName << std::endl;
    File file(fileName);
    // std::cout<<std::endl<<"  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"<<std::endl<<std::endl;
    processFileRange(file, 4093, 4, 6);
  }

  /*
    // * Double underline and crossed out both worked on the terminal integrated into VS code, but not the normal mac terminal.
    // * Fonts did nothing on either terminal.
    // * Red worked perfectly in both terminals.
    std::cout << "\x1b[11m"
              << "11 - Alternative font #1" << std::endl
              << "\x1b[12m"
              << "12 - Alternative font #2" << std::endl
              << "\x1b[13m"
              << "13 - Alternative font #3" << std::endl
              << "\x1b[14m"
              << "14 - Alternative font #4" << std::endl
              << "\x1b[15m"
              << "15 - Alternative font #5" << std::endl
              << "\x1b[16m"
              << "16 - Alternative font #6" << std::endl
              << "\x1b[17m"
              << "17 - Alternative font #7" << std::endl
              << "\x1b[18m"
              << "18 - Alternative font #8" << std::endl
              << "\x1b[19m"
              << "19 - Alternative font #9" << std::endl
              << "\x1b[20m"
              << "20 - Fraktur (Gothic)" << std::endl
              << "\x1b[10m"
              << "10 - Normal font" << std::endl
              << "\x1b[21m"
              << "21 - Double underline" << std::endl
              << "\x1b[9m"
              << "9 - Crossed out" << std::endl
              << "\x1b[31m"
              << "31 - red" << std::endl;
              */
}
