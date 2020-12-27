#ifndef __RansHelper_h_
#define __RansHelper_h_

#include <assert.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <stdint.h>

#include "rans64.h"


/* The RansRange class is a convenient way to translate between arbitrary
 * fractions (like the actual number of times we've seen this symbol / the
 * actual number of times we've seen any symbol) and fractions that rANS likes
 * (like m / (2^n)).
 *
 * The formulas are pretty simple.  The important thing is that we are
 * consistent, especially the way we round.  (For example, you might have to
 * add several numbers to get the start for a particular symbol.  You add all
 * of those numbers up BEFORE doing any rounding!)
 *
 * I made this a class instead of just a few functions because otherwise we'd
 * need to return multiple values at once. */
class RansRange
{
private:
  uint32_t _start;
  uint32_t _freq;
public:
  // We always use this when we call the rANS library.
  static const uint32_t SCALE_BITS = 31;

  // This is the denominator any time we cal the rANS library.
  static const uint32_t SCALE_END = (1u<<SCALE_BITS);

  // (n / d) ≈ (rescale(n, d) / SCALE_END)
  // Change numbers from the main program's domain into the form used by the
  // rANS library.
  static uint32_t rescale(uint32_t numerator, uint32_t denominator)
  {
    assert(numerator <= denominator);
    uint64_t value = numerator;
    value *= SCALE_END;
    // The denominator is growing from the actual real world value up to the
    // high precision universal value.  So we round up.
    value = (value + denominator - 1) / denominator;
    return value;
  }

  // n ≈ unScale(rescale(n, d), d)
  // Takes a number from the rANS domain and converts it back into a number in
  // the original domain.
  static uint32_t unScale(uint32_t fromRans, uint32_t denominator)
  {
    uint64_t value = fromRans;
    value *= denominator;
    // The denominator is shrinking from the high precision universal value
    // down to the actual real word value.  So we round down.
    value /= SCALE_END;
    return value;
  }

  // An arbitrary safe state.
  void clear() { _start = 0; _freq = SCALE_END; }
  RansRange() { clear(); }

  // Initialize from numbers in the main program's domain.
  void load(uint32_t start, uint32_t freq, uint32_t scaleEnd)
  {
    _start = rescale(start, scaleEnd);
    // We are very careful about the rounding.  This entry's end should always
    // be exactly the same as the next entry's start.
    const uint32_t originalEnd = start + freq;
    const uint32_t finalEnd = rescale(originalEnd, scaleEnd);
    _freq = finalEnd - _start;
  }
  RansRange(uint32_t start, uint32_t freq, uint32_t scaleEnd)
  { load(start, freq, scaleEnd); }

  // An invalid range.  If you try to encode this, it should fail.
  // (Mathematically speaking, the rANS encoder should require an infinite
  // number of bits to encode this.)  You should never be able to read this
  // when reading from rANS.
  RansRange(std::nullptr_t) : _start(0), _freq(0) { }
  bool valid() const { return _freq; }
  
  // use start(), freq() and SCALE_BITS when you call the rANS library.
  uint32_t start() const { return _start; }
  uint32_t freq() const { return _freq; }

  // Call the corresponding funtion in the rANS library.
  void put(Rans64State* r, uint32_t** pptr) const
  {
    Rans64EncPut(r, pptr, _start, _freq, SCALE_BITS);
  }

  // First call get() to get the next number.  You should already have a list
  // of start positions for each symbol.  Find which symbol is associated with
  // this value.  (The symbol with the greatest start that is <= the
  // result of get().)  Create a RansRange for that symbol.  Finally call
  // advance() on that RansRange object.
  static uint32_t get(uint32_t denominator, Rans64State* r)
  {
    return unScale(Rans64DecGet(r, SCALE_BITS), denominator);
  }
  void advance(Rans64State* r, uint32_t** pptr) const
  {
    Rans64DecAdvance(r, pptr, _start, _freq, SCALE_BITS);
  }

  // Assume this is a boolean.  There are exactly two symbols.  invert() will
  // switch to the description of the other symbol.  Inverting twice should
  // return you to the original symbol.
  void invert()
  {
    if (_start == 0)
      _start = _freq;
    else
    {
      assert(_start + _freq == SCALE_END);
      _start = 0;
    }
    _freq = SCALE_END - _freq;
  }

  double idealCost() const { return -std::log2(_freq / (double)SCALE_END); }
};

class SymbolCounter
{
private:
  std::vector< uint32_t > _freq;
  uint32_t freq(size_t symbol) const
  {
    if (symbol >= _freq.size())
      return 1;
    else
      return _freq[symbol];
  }
  void ensureAtLeast(size_t newSize)
  {
    if (newSize > _freq.size())
      _freq.resize(newSize, 1);
  }
  size_t getSymbol(uint32_t position, size_t symbolCount) const
  {
    uint32_t end = 0;
    for (size_t i = 0; i < symbolCount; i++)
    {
      end += freq(i);
      if (position < end)
	return i;
    }
    assert(false);
  }
public:
  void increment(size_t symbol)
  {
    ensureAtLeast(symbol + 1);
    _freq[symbol]++;
    //if (_totals[0] > (RansRange::SCALE_END / 4))
    //  reduceOld();
    // TODO maybe return this or make someone else responsible.  I.e. maybe
    // each block will always call increment some reasonable maximum number
    // of times.  Someone else is already calling reduceOld() at the end of
    // each block.
  }
  RansRange getRange(size_t symbol, size_t symbolCount) const
  {
    assert(symbol < symbolCount);
    uint32_t start = RansRange::SCALE_END;
    uint32_t total = 0;
    for (size_t i = 0; i < symbolCount; i++)
    {
      if (i == symbol)
	start = total;
      total += freq(i);
    }
    return RansRange(start, freq(symbol), total);
  }
  size_t getSymbol(Rans64State* r, uint32_t** pptr, size_t symbolCount) const
  {
    uint32_t denominator = 0;
    for (size_t i = 0; i < symbolCount; i++)
      denominator += freq(i);
    // seems like some duplication of effort between the two versions of
    // getSymbol().  Maybe some room to make things faster.
    const size_t symbol =
      getSymbol(RansRange::get(denominator, r), symbolCount);
    getRange(symbol, symbolCount).advance(r, pptr);
    return symbol;
  }
  void reduceOld()
  {
    for (uint32_t &f : _freq)
      f = (f + 1) / 2;
    // It's tempting to remove some dead weight from the end of the list.
    // If there are extra 1's at the end, we could delete them and return the
    // memory.  Probably a small savingings in memory, not worth the cost.
  }
};

class BoolCounter
{
private:
  SymbolCounter _counter;
public:
  BoolCounter(bool preload = false)
  {
    if (preload)
    { // This is the simple way to ensure nothing has a probability of 0.
      increment(false);
      increment(true);
    }
  }
  void increment(bool value)
  {
    _counter.increment(value);
  }
  RansRange getRange(bool value) const
  {
    return _counter.getRange(value, 2);
  }
  bool readValue(Rans64State* r, uint32_t** pptr) const
  {
    return _counter.getSymbol(r, pptr, 2);
  }
  void reduceOld() { _counter.reduceOld(); }
};

inline bool isIntelByteOrder()
{
  const uint64_t number = 0x0102030405060708lu;
  char const *asByte = reinterpret_cast< char const * >(&number);
  return (asByte[0] == 8) && (asByte[7] == 1);
}

#endif
