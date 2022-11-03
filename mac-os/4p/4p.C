#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include "../shared/File.h"
#include "../shared/RansHelper.h"

// g++ -o 4p -O3 -ggdb -std=c++0x -Wall 4p.C ../shared/File.C ../shared/Misc.C

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
static char const *ansiStart = "\x1b[31;105m";
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

//Only true for debug and development!  This prints the entire
// input file, and uses colors to show which text was compressed with 
// which algorithm.
bool echoAllInput = true;

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
          std::cout<<ansiBlink;
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
  size_t individualBytes = 0;
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
      current++;
      recordNewHash();
      individualBytes++;
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
  const auto decisionCostInBits = (hashEntries + individualBytes);
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
  const auto individualByteCostInBits = individualBytes * 8;
  const double totalCostInBytes = std::ceil((decisionCostInBits + betterHashCodeCostInBits + individualByteCostInBits + slidingWindow.costInBits()) / 8);
  auto const fileSize = file.end() - file.begin();
  std::cout << "processFileRange() fileSize=" << fileSize
            << ", hashBufferSize=" << hashBufferSize
            << ", hashBufferFree=" << hashBufferFree
            << ", hashEntrySize=" << minHashEntrySize << '-' << maxHashEntrySize
            << ", hashEntries=" << hashEntries
            << ", simpleHashCodeCostInBits=" << simpleHashCodeCostInBits
            << ", betterHashCodeCostInBits=" << betterHashCodeCostInBits
            << ", hashSavings=" << hashSavings
            << ", individualBytes=" << individualBytes
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
    //std::cout<<std::endl<<"  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"<<std::endl<<std::endl;
    processFileRange(file, 4093, 4, 6);
  }
}
