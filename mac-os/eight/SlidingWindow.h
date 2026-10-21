#ifndef __SlidingWindow_h_
#define __SlidingWindow_h_

#include <iostream>
#include "../shared/File.h"

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
      /*
      if (echoAllInput)
      {
        if (selfReferencing)
        {
          std::cout << ansiBlink;
        }
        std::cout << AlternateColors::blue.next() << std::string(bestStart, bestLength) << ansiReset;
      }
      */
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

  /** Sends a small amount of data */
  void debugOut(std::ostream &out) const
  {
    out << "(SlidingWindow: _attempts=" << _attempts
        << ", _successes=" << _successes
        << ", _bytesCompressed=" << _bytesCompressed
        << ", _selfReferences=" << _selfReferences
        << ", _costInBits=" << _costInBits << ")";
  }
};

#endif