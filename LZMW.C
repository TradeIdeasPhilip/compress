#include <assert.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdint.h>
#include <string.h>

#include "rans64.h"

// Pass 1:  Record which strings would be created by pure LZMW, and how many
//          times each one would be used.

// Pass 2:  Move the string table out of the way.  Start with a fresh string
//          table that only has the 256 one byte entries.
//
//          Use the vanilla LZMW algorithm with a few changes.
//
//          Each time vanilla would have added to the table, look at our old
//          table first.  If there were 0 hits to this new string do nothing,
//          leave it out of the table.  Otherwise add it to the table.  And
//          output the command that tells the decompressor that we've decided
//          to add this entry to the table.
//
//          Each time we send a string to the output, Increment the counter
//          for that string.  If the new value is the same as the value we
//          recorded in step 1, send the command that tells the decompressor
//          to delete the top entry in the MRU list.
//
//          Everything we write goes to a a stack.  We need to save the
//          instructions and related data in order.  The decompressor will
//          see these items in the same order as we are adding them here.
//
//          Also save statistics about these instructions and related data so
//          the rANS encoder can compress them.

// Pass 3:  Ignore the original file.  Copy the statistics into the header so
//          the rANS decoded can decode the instructions and related data.
//          Read the remaining data off the lists we created in step 2.  Read
//          the data backwards, because the rANS process will reverse things a
//          second time, putting them back in the right order.  Copy the
//          instructions and related data into the rANS encoder, and copy the
//          output of the rANS encoder into the output file.

// This will return a string with all 256 possible bytes.  Multiple calls to
// this function will always return the same object, so it is safe to save
// a pointer into this string.  We start with 255 and work our way down to 0
// because that helps the compression routine.  This order is part of the
// protocol and should not change.
std::string const &getBootStrapData()
{
  static auto create = []()
    {
      std::string result;
      result.reserve(256);
      for (int i = 255; i >= 0; i--)
	result += (char)i;
      return result;
    };
  static const std::string result = create();
  return result;
}

class FileSlice
{
public:
  typedef uint64_t Length;
  
private:
  char const *_start;
  Length _length;
  
public:

  FileSlice() : _start(NULL), _length(0) { }
  
  FileSlice(char const *start, Length length) :
    _start(start), _length(length)
  { // Make sure the inputs are reasonable.  Make sure we didn't wrap around.
    // Note that it is not legal to have a slice that ends at the exact end of
    // our address space.  While many things would work, that would break our
    // end() iterator.
    assert(end() >= begin());
  }

  // Make this explicit to reduce the chance of accidentally using a temporary
  // string.  This was a real problem when I was first converting the code to
  // use FileSlice instead of std::string.  If you forgot to change one
  // instance of a string to a slice, the compiler would automatically do
  // the conversion for you, without telling you, and it wasn't smart enough
  // to check if the string was permanent.
  explicit FileSlice(std::string const &string) :
    FileSlice(string.c_str(), string.length()) { }

  // Create a new slice starting where the first slice starts and ending where
  // the second slice ends.  The two strings very explicitly do not have to be
  // adjacent.
  FileSlice(FileSlice const &start, FileSlice const &end) :
    _start(start._start), _length(end._start - start._start + end._length)
  {
    assert(start._start <= end._start);
  }

  void clear() { _start = NULL; _length = 0; }
  
  // Move the left end of the string toward the right.  Leave the right end in
  // place.  So if the string was ABCD then after a call to pushForward(1)
  // the string would be BCD.  Other inputs:  0 --> ABCD, 2 --> CD, 3 --> D,
  // 4 --> empty string, 5 or more --> assertion failed.
  void pushForward(Length bytes)
  {
    assert(bytes <= _length);
    _start += bytes;
    _length -= bytes;
  }

  Length length() const { return _length; }

  bool empty() const { return _length == 0; }

  // The standard begin() and end() iterators.  These are suitable for use in
  // STL or in a ranged for loop.  This is a random access iterator.
  char const *begin() const { return _start; }

  char const *end() const { return begin() + _length; }

  // This is more for discussion and comments than actual use.  == only says
  // that the contents of the slices are the same.  This method also says that
  // we are pointing to the exact same memory.  Most of the time we only care
  // about the contents.  But notice the constructor that cretes a new slice
  // from two existing slices.  The concept of identical() is required there.
  bool identical(FileSlice const &other) const
  {
    return (_start == other._start) && (_length == other._length);
  }

  // Identical contents.  Same result as if you stored the data in two
  // std::string objects and called == on those objects.
  bool operator ==(FileSlice const &other) const
  {
    if (_length != other._length)
      return false;
    return memcmp(begin(), other.begin(), _length) == 0;
  }

  // Depends only on the contents.  Same result as if you stored the data in
  // two std::string objects and called < on those objects.
  bool operator <(FileSlice const &other) const
  {
    const int prefixCompare =
       memcmp(begin(), other.begin(), std::min(_length, other._length));
    if (prefixCompare < 0)
      return true;
    else if (prefixCompare > 0)
      return false;
    else return _length < other._length;
  }

  bool isPrefixOf(FileSlice const &longer) const
  {
    if (_length > longer._length)
      return false;
    return memcmp(begin(), longer.begin(), _length) == 0;
  }

  // Shortens the string to be the same length as prefix.  Asserts that prefix
  // is a prefix of the string.  The offset field is never modified.
  FileSlice trimTo(FileSlice const &prefix)
  {
    assert(prefix.isPrefixOf(*this));
    _length = prefix.length();
    return *this;
  }

  // Returns the number of bytes which is the same in both slices, starting
  // from the beginning of each slice.
  Length commonPrefixLength(FileSlice const &other) const
  {
    const Length max = std::min(length(), other.length());
    char const *a = begin();
    char const *b = other.begin();
    for (Length i = 0; i < max; i++, a++, b++)
      if (*a != *b)
	return i;
    return max;
  }

  // This is only aimed at debugging.  If I output a string with a newline in
  // it, that makes the debug output hard to read.
  std::string quote() const
  {
    std::string result;
    for (char ch : *this)
    {
      switch (ch)
      {
      case '\n':
	result += "\\n";
	break;
      case '\t':
	result += "\\t";
	break;
      case '"':
	result += "\\\"";
      case '\\':
	result += "\\\\";
	break;
      default:
	result += ch;
      }
    }
    if ((result.length() == 0) || (result[0] == ' ')
	|| (result[result.length()-1] == ' '))
      result = '"' + result + '"';
    return result;
  }
  
};

class CountedStrings
{
private:
  // TODO Use our own custom allocator here.  This one variable typically takes
  // up far more memory than the rest of the program combined.  This memory
  // seems like the limiting factor when the program is running.  What's more,
  // this table seems to use about 3x as much memory as you'd expect.
  //
  // We should have an option to limit the maximum size of this table.  It's
  // also affected by the size of the _combineCounts list and the size and
  // contents of the input file, but it would be nice to set an upper limit
  // to avoid thrashing.
  //
  // We could use mmap to grab one large block of memory the right size for
  // this table based on the max specified on the command line.  It wouldn't be
  // allocated until it was needed.
  //
  // The allocator could use smaller values for pointers.  In fact, the idea of
  // an allocator was originally designed for that.  For strange memory models
  // like segment registers on the PC.  I haven't tried it but presumably we
  // wouldn't even need a free list.  All items should be identical sizes so
  // we can pack them into the space like one long array.
  std::map< FileSlice, int > _strings;

public:
  CountedStrings()
  {
    for (char const &ch : getBootStrapData())
      add(FileSlice(&ch, 1));
  }

  // This is something we can look up later.  If its already in the table,
  // nothing.  If not, add it with a count of 0.
  void add(FileSlice const &string) { _strings[string]; }

  // Find the longest string in this table which is a prefix of the subect.
  // Bump the reference count of whatever entry we found.  Note:  We initialize
  // the list in such a way that this can't fail.  In the worst case it will
  // return a single byte.
  FileSlice longestPrefix(FileSlice subject)
  {
    auto it = _strings.upper_bound(subject);
    auto &kvp = (it == _strings.end())?(*_strings.rbegin()):(*(--it));
    FileSlice possiblePrefix = kvp.first;
    const auto matchLength = subject.commonPrefixLength(possiblePrefix);
    assert(matchLength > 0);
    if (matchLength == possiblePrefix.length())
    {
      kvp.second++;  // Use count
      return subject.trimTo(possiblePrefix);
    }
    else
      return longestPrefix(FileSlice(subject.begin(), matchLength));
  }

  // Trim the tree.  When we look things up in the following passes it should
  // be faster if there are fewer items in the tree.  Remove any items that
  // were created but never used.
  void trimZeros()
  {
    //static auto saveCopy = _strings;
    //std::cout<<_strings.size()<<" beore trimZeros()."<<std::endl;
    for (auto it = _strings.begin(); it != _strings.end(); )
    {
      if (it->second == 0)
	it = _strings.erase(it);
      else
	it++;
    }
    //std::cout<<_strings.size()<<" after trimZeros()."<<std::endl;
  }

  bool contains(FileSlice const &string) const
  {
    return _strings.count(string);
  }

  // If the item is not there, this might fail an assertion, dump core, etc.
  int getCount(FileSlice string) const
  {
    return _strings.find(string)->second;
  }

  size_t size() const { return _strings.size(); }

  typedef std::map< FileSlice, int >::const_iterator Iterator;
  Iterator begin() const { return _strings.begin(); }
  Iterator end() const { return _strings.end(); }
  
};

// Read an entire file into a string.  Probably change to mmap() at some point.
std::string slurp(char const *fileName)
{
  std::ifstream in(fileName);
  std::stringstream sstr;
  sstr << in.rdbuf();
  return sstr.str();
}

// The codes that we send to the rANS encoder.
enum class Instruction : unsigned char
{ // The top entry in MRU list will never be used again.  Delete it.  Removing
  // junk means that we will get more requests for items near the top of the
  // list.
    DeleteTopEntry,
  // Copy a string from the string list to the output.
    PrintString,
  // Grab the last N strings we've printed, possibly including duplicates,
  // concatinate them, and save the result to the string list.
    CreateString,
  // We never send this instruction to the output.  When we encode a new
  // instruction we need to know the new instruction and the previous
  // instruction.  Initially we say that None was the previous instruction,
  // before we actually send anything.
    None
    };

std::ostream &operator <<(std::ostream &stream, Instruction instruction)
{
  switch (instruction)
  {
    //case Instruction::EndOfFile:
    //stream<<"EndOfFile";
    //break;
  case Instruction::DeleteTopEntry:
    stream<<"DeleteTopEntry";
    break;
  case Instruction::PrintString:
    stream<<"PrintString";
    break;
  case Instruction::CreateString:
    stream<<"CreateString";
    break;
  case Instruction::None:
    stream<<"None";
    break;
  default:
    abort();
  }
  return stream;
}


// Use this to store the instructions as we create them.  Read from here when
// sending the instructions to the rANS encoder.  This data structure will
// automatically reverse the order of the instructions.  The rANS process will
// reverse the order again.  The decompressor will read the instructions in
// exactly the same order that we pushed them into this list.
//
// Note:  The rANS process looks at the current instruction (the one we're
// trying to send now) and the previous instruction (the one we just sent) to
// do the encoding.  The rest of the program only cares about the current
// instruction.  peek() gives you the previous instruction.
class InstructionList
{
private:
  std::vector< Instruction > _instructions;
  std::map< std::pair< Instruction, Instruction >, int64_t > _frequencies;

  Instruction peek() const
  { return _instructions.empty()?Instruction::None:*_instructions.rbegin(); }

public:
  void push(Instruction instruction)
  {
    _frequencies[std::make_pair(peek(), instruction)]++;
    _instructions.push_back(instruction);
  }

  void pop(Instruction &next, Instruction &previous)
  {
    assert(!empty());
    next = peek();
    _instructions.resize(_instructions.size() - 1);
    previous = peek();
  }

  bool empty() const { return _instructions.empty(); }

  size_t size() const { return _instructions.size(); }
  
  void removeFinalDeletes()
  {
    while (true)
    {
      if (empty()) return;
      if (peek() != Instruction::DeleteTopEntry) return;
      _instructions.resize(_instructions.size() - 1);
    }
  }

  std::map< std::pair< Instruction, Instruction >, int64_t > const &
  getFrequencies() const { return _frequencies; }
  
  // Disable accidental copies.  This might be very large.  Currently we're
  // storing everything in an STL vector, so the default copy or assign would
  // work fine.  But we reserve the possibility of using a more complicated but
  // more efficient data structure.
  InstructionList() {}
  InstructionList(InstructionList const &other) = delete;
  InstructionList &operator =(InstructionList const &other) = delete;
};


// Add to this each time we add Instruction::CreateString to the
// InstructionList.  See InstructionList for notes about size, order and
// copying.
class CombineCountList
{
private:
  std::vector< uint8_t > _values;
  std::map< uint8_t, int64_t > _frequencies;

public:
  bool empty() const { return _values.empty(); }
  size_t size() const { return _values.size(); }
  
  void push(uint8_t count) { _values.push_back(count); _frequencies[count]++; }
  
  uint8_t pop()
  {
    assert(!_values.empty());
    uint8_t result = *_values.rbegin();
    _values.resize(_values.size() - 1);
    return result;
  }

  std::map< uint8_t, int64_t > const &getFrequencies() const
  { return _frequencies; };

  CombineCountList() { }
  CombineCountList(CombineCountList const &other) = delete;
  CombineCountList &operator =(CombineCountList const &other) = delete;
};


// Add to this each time we add Instruction::PrintString to the
// InstructionList.  See InstructionList for notes about size, order and
// copying.
//
// The rANS process needs to know the current size of the MRU list and the
// index into this list that we want to save.  Most of the program only cares
// about the index we want to save.  However, both the reader and the writer
// will have access to the current size of the MRU list, so we can give that
// to the rANS encoder and decoder.
class MruIndexList
{
private:
  struct Pair { uint32_t current; uint32_t max; };
  std::vector< Pair > _values;
  std::map< uint32_t, int64_t > _frequencies;

public:
  bool empty() const { return _values.empty(); }
  size_t size() const { return _values.size(); }

  /* Note, we are saving both the index value and the current size of the MRU
   * list.  However, currently we are only using the index value for the
   * compression.  We use the MRU list size when preparing some debug
   * reports.  We haven't yet found a good way to use this extra data, but
   * we're still looking.
   *
   * The table below shows why the obvious way to use the MRU list size
   * doesn't help us.  We start with an estimate for the probability of
   * each index.  You could say that if the current MRU list size is 65535
   * (just as an example) then we temporarily set the probability of every
   * index >= 65535 to 0.  And we scale up the remaining values so the total
   * would be 100%.  The problem is that those were pretty close to 0
   * already.  This example would only work when the MRU list was < 30% of
   * its full size, and it would only save us 1.65%.  Note that on average
   * (according to some other statistics, not copied here) the MRU size is
   * around 80% of its total.  See freq_by_percent.tcl and
   * get_frequencies.tcl for attempts to find a better way to use the
   * current MRU list size.
start   end     total           Used    MRU Size        Could be Saved
0       0       2333400224      1.01%   0.00046%        98.99%
1       2       7970168566      3.45%   0.00137%        95.54%
3       6       10306744022     4.46%   0.00319%        91.09%
7       14      12169018872     5.26%   0.00684%        85.83%
15      30      14932531024     6.46%   0.01415%        79.37%
31      62      17218765792     7.45%   0.02875%        71.92%
63      126     17852621888     7.72%   0.05795%        64.20%
127     254     16199250944     7.01%   0.11636%        57.20%
255     510     15806051072     6.84%   0.23317%        50.36%
511     1022    15852356352     6.86%   0.46680%        43.51%
1023    2046    15262490112     6.60%   0.93405%        36.91%
2047    4094    15573331968     6.73%   1.86857%        30.17%
4095    8190    17480587264     7.56%   3.73759%        22.61%
8191    16382   18265726976     7.90%   7.47563%        14.71%
16383   32766   15782494208     6.82%   14.95172%       7.89%
32767   65534   14423293952     6.24%   29.90390%       1.65%
65535   131070  2709127168      1.17%   59.80826%       0.48%
131071  219151  1110613329      0.48%   100.00000%      
                231248573733    100.00%         
  */
  
  
  void push(uint32_t current, uint32_t max)
  {
    _values.push_back({current, max});
    _frequencies[current]++;
  }

  void pop(uint32_t &current, uint32_t &max)
  {
    assert(!_values.empty());
    Pair const &pair = *_values.rbegin();
    current = pair.current;
    max = pair.max;
    _values.resize(_values.size() - 1);
    return;
  }

  std::map< uint32_t, int64_t > const &getFrequencies() const
  { return _frequencies; };

  void debugDump(std::string fileName) const
  {
    std::ofstream stream(fileName, std::ios_base::out | std::ios_base::trunc);
    for (Pair pair : _values)
      stream<<pair.current<<' '<<pair.max<<std::endl;
  }
  
  MruIndexList() {}
  MruIndexList(MruIndexList const &) = delete;
  MruIndexList &operator =(MruIndexList const &) = delete;
};


class Header
{
public:
  
  struct PrintRegionInfo
  {
    uint32_t begin;
    uint32_t end;  // Stop BEFORE this item.
    // The scale for these frequencies can be anything.  When we go to encode
    // things we will re-scale this to a total of 2^31.  It seems like a
    // little overkill to use so many bits here when we'll be removing bits
    // in the final answer.  But we are interpolating between two points.  So
    // there will be small differences when we jump from one integer value to
    // the next.  And all these little values add up.  We want the total for a
    // section to be correct.
    uint32_t firstFrequency;  // The frequency of item begin.
    uint32_t lastFrequency;  // The frequency of item end-1.

    uint32_t size() const { return end - begin; }
    bool isSingle() const { return begin == end - 1; }

    void dump() const
    {
      if (isSingle())
	std::cout<<"PrintString("<<begin<<") has frequency "<<firstFrequency
		 <<'.'<<std::endl;
      else
	std::cout<<"PringString("<<begin<<") - PrintString("<<(end-1)
		 <<") have frequencies "<<firstFrequency<<" - "<<lastFrequency
		 <<", totaling "<<((firstFrequency+(int64_t)lastFrequency)*size()/2)
		 <<'.'<<std::endl;
    }
  };
  
private:

  // Maps the number of items to concatenate to a frequency.  The total
  // frequency will be createStringTotalFreq.
  std::map< uint8_t, uint16_t > _createFrequencies;

  std::vector< PrintRegionInfo > _printFrequencies;

  // The chance, on a scale of 256, that each DeleteTopEntry will be followed
  // by a CreateString.  (256 - _deleteTopEntry_CreateString) is the chance of
  // DeleteTopEntry being followed by PrintString.  This is allowed to be 0
  // if the probability really is exactly 0.  If the real value is just close
  // to 0 we put a 1 here.
  uint8_t _deleteTopEntry_CreateString;

  // The chance, on a scale of 256, that each CreateString will be followed by
  // another CreateString.  (256 - _createString_CreateString) is the chance of
  // CreateString being followed by PrintString.  This is allowed to be 0 if
  // this never happens.  However, if it's just close to 0 we'll say 1.  If
  // rounding suggests that this is 256, we force it to be 255.
  uint8_t _createString_CreateString;

  // The chance, on a scale of 256, that each PrintString will be followed by
  // a DeleteTopEntry.  This will be 0 if and only if the probability is
  // exactly 0.  For very small probability we record a 1 here.
  uint8_t _printString_DeleteTopEntry;

  // The chance, on a scale of 256, that each PrintString will be followed by
  // a CreateString.  This will be 0 if and only if the probability is
  // exactly 0.  For very small probability we record a 1 here.  Note, the
  // chance of a PrintString being followed by another PrintString is
  // (256 - _printString_DeleteTopEntry - _printString_CreateString).
  uint8_t _printString_CreateString;

  // When we describe the frequencies of the values for PrintString, we break
  // the data into groups.  We explicitly save the largest value used for
  // PrintString in the file.  We use this function to convert that single
  // number into a list of ranges.  Later in the header we'll describe each
  // range in order, without any other metadata, just the largest value.
  //
  // pair.first is the first index in the group.  pair.second is the first
  // index after the group, or 1+ the last index in the group.  (That's the
  // standard STL idea of begin() and end().)
  //
  // Some pairs will contain exactly one item.  The description of that group
  // will be shorter than the description of a group with more items.  Every
  // group has at least one item.
  std::vector< std::pair< uint32_t, uint32_t > > getGroups(uint32_t endBefore)
  {
    std::vector< std::pair< uint32_t, uint32_t > > result;
    uint32_t groupSize = 1;
    uint32_t groupStart = 0;
    while (groupStart < endBefore)
    {
      uint32_t nextStart = std::min(groupStart + groupSize, endBefore);
      result.emplace_back(groupStart, nextStart);
      groupSize = nextStart - groupStart;
      groupSize *= 2;
      groupStart = nextStart;
    }
    return result;
  }
  
public:

  void dump() const
  {
    for (auto kvp : _createFrequencies)
      std::cout<<"CreateString("<<(int)kvp.first<<") has frequency "
	       <<kvp.second<<'.'<<std::endl;
    for (PrintRegionInfo const &region : _printFrequencies) region.dump();
    for (auto kvp : getInstructionFrequencies())
      std::cout<<kvp.first.first<<" => "<<kvp.first.second<<" has frequency "
	       <<kvp.second<<'.'<<std::endl;
  }

  std::string asBinaryString()
  { // TODO this is just a placeholder.
    return "PDS:";
  }
  
  static const int createStringScaleBits = 11;
  static const int createStringTotalFreq = 1<<createStringScaleBits;
  
  void loadCreateFrequencies(std::map< uint8_t, int64_t > const &frequencies)
  {
    _createFrequencies.clear();
    if (frequencies.empty()) return;
    int64_t totalFrequency = 0;
    int64_t largestFrequency = -1;
    uint8_t lengthOfLargestFrequency = 0;
    for (auto kvp : frequencies)
    {
      auto length = kvp.first;
      int64_t frequency = kvp.second;
      // A length can be between 2 and 32, inclusive.  0 makes no sense; that
      // would always create the empty string.  1 makes no sense; would try to
      // copy a string that's alreay in the table.  There's no real upper limit
      // on the possible length.  However, our tests show diminishing returns
      // for big numbers.  We reserve 5 bits to store the length in the file
      // header, and we reserve one special length to mark the end of the list.
      assert((length > 1) && (length < 33));
      assert(frequency > 0);
      totalFrequency += frequency;
      if (frequency > largestFrequency)
      { // Keep track of which length is associated with the highest frequency.
	lengthOfLargestFrequency = length;
	largestFrequency = frequency;
      }
    }
    
    // Rescale the frequencies so the total is 2^11.  Somewhat arbitraritly we
    // allocate 5 bits to the length and 11 bits to the frequency so we can
    // store each header entry as 16 bits.
    for (auto kvp : frequencies)
    {
      auto length = kvp.first;
      int64_t frequency = kvp.second;
      int16_t normalized =
	(int16_t)round(frequency/(double)totalFrequency*createStringTotalFreq);
      if (normalized == 0)
	normalized = 1;
      _createFrequencies[length] = normalized;
    }

    // The total should be createStringTotalFreq.  We expect to be close, but
    // because of rounding we might not be exact.  Fix that now, so the total
    // is createStringTotalFreq.  
    //
    // This is a fairly simple algorithm.  Just find out how far off we are,
    // and make all the changes to the biggest item.  This is probably good
    // enough because we have a lot of precision and we should be close.  You
    // could do slightly better.  And, if you improve this algorithm in the
    // compressor, you don't have to worry about the decompressor.
    int needToAdd = createStringTotalFreq;
    for (auto kvp : _createFrequencies)
    {
      auto frequency = kvp.second;
      needToAdd -= frequency;
    }
    auto &toAdjust = _createFrequencies[lengthOfLargestFrequency];
    toAdjust += needToAdd;
    assert((toAdjust > 0) && (toAdjust <= createStringTotalFreq));
  }

  std::map< uint8_t, uint16_t > const &getCreateFrequencies() const
  { return  _createFrequencies; }

  void loadPrintFrequencies(std::map< uint32_t, int64_t > const &frequencies)
  {
    _printFrequencies.clear();
    uint32_t endBefore = 0;
    if (!frequencies.empty())
      endBefore = frequencies.rbegin()->first + 1;
    std::vector< std::pair< uint32_t, uint32_t > > groups =
      getGroups(endBefore);
    int64_t totalFrequency = 0;
    for (auto kvp : frequencies)
    {
      const auto frequency = kvp.second;
      assert(frequency > 0);
      totalFrequency += frequency;
    }
    // TODO instead of a series of line segments, approximate the curve as a
    // series of parabola segments.  Need to test this, but I bet we could get
    // much closer to the ideal values.
    //
    // For each region we'd record the total frequency for everything in the
    // region.  When encoding and decoding we'll start by saying which region
    // we're in, so we need to know the frequency for the entire region.  Then
    // we need only the shape of the region, only comparing each item in the
    // region to other items in the same region.  (So we look at the scale
    // separately from the rest of the shape.)  That will make it easier to
    // pick appropriate sized integers to store in the header.  You're not
    // worried that one region will be too small to describe correctly.
    //
    // We assume that the point on the left of the region is the highest and
    // the point on the right is the lowest, and none are 0.  We know in some
    // real cases the regression line wants the right side to be slightly
    // higher than the left.  In that case we'd use a horizontal line as our
    // approximation.  We need to set reasonable limits so we can encode this
    // shape efficiently in the header.  We can reduce the number of these
    // strange cases by explicitly listing more of the first few points, and
    // only using this approximation as we get further from x=0.
    //
    // The second integer we need to store says how far down the last point is.
    // The largest possible number would mean that we didn't go down at all,
    // we had a horizontal line.  0 would mean that the last value is 0, or
    // something close to 0.  (We'd have to round that up to some small value
    // since we can't handle a 0 probability.)  Half the maximum value would
    // mean that the point on the right has half the probability of the point
    // on the left.
    //
    // The third integer describes the curvature.  Again we limit the range
    // based on our expectation.  (Again, we'll print the most extreme value
    // allowed if the measured values are out of our allowed range.)  One
    // extreme is a (diagonal but) straight line segment from the left to the
    // right.  The other extreme is that that curve is exactly horizontal on
    // the right and starts sloping upwards as you move left.  You could
    // imagine encoding that angle at the right side where 0 means the least
    // curvature and the largest integer means the most curvature.  That's
    // perfect because we can use all the precision in our integer.  However,
    // we might consider using something different just to avoid floating point
    // math.
    //
    // Note that we're trying to avoid floating point math in the parts of the
    // code which we have to reproduce in the compressor and the decompressor.
    // Even under ideal circumstances round-off error can be tough.  I don't
    // want to think about this on a different processor and compiler.  When
    // we build our approximation we can use whatever we want.  But when we ask
    // what is the probability of this number, or later what is the number
    // associated with this probability, we want to use integer math.
    //
    // Note that the rANS library isn't concerned with the actual probability
    // of each item.  We only care about the cumulative probabilities.  What is
    // the probability that the value will be less than x?  What value of x
    // falls between two given probabilities?  That's just an integral.  We
    // don't have to determine the individual values then add them up.  Our
    // probability curves (for each region) are defined by a polynomial, and
    // it's easy to integrate a polynomial.
    //
    // Note that you normally use 3 points to define a parabola.  Or two points
    // and the slope at one point.  That's at least 5 numbers.  We already know
    // the x values for the first and last point.  That's stored implicitly in
    // the header.  So we only need to store three more values.
    struct UnscaledRegion
    {
      uint32_t begin;
      uint32_t end;  // Stop BEFORE this item.
      double firstFrequency;  // The frequency of item begin.
      double lastFrequency;  // The frequency of item end-1.
      uint32_t size() const { return end - begin; }
      bool isSingle() const { return size() == 1; }
    };
    std::vector< UnscaledRegion > unscaledRegions;
    unscaledRegions.reserve(groups.size());
    for (auto group : groups)
    {
      UnscaledRegion region;
      region.begin = group.first;
      region.end = group.second;
      double sx = 0;
      double sy = 0;
      double sxx = 0;
      double syy = 0;
      double sxy = 0;
      for (auto x = region.begin; x < region.end; x++)
      {
	const auto it = frequencies.find(x);
	const double y =
	  ((it==frequencies.end())?0:it->second)/(double)totalFrequency;
	sx += x;
	sy += y;
	sxx += x*x;
	syy += y*y;
	sxy += x*y;
      }
      double m, b;  // y = mx + b
      if (region.isSingle())
      { // If we have just one point, make it a horizontal line.
	b = sy;
	m = 0;
      }
      else
      {
	const auto n = region.size();
	b = (sy * sxx - sx * sxy) * 1.0 / (n * sxx - sx * sx);
	m = (n * sxy - sx * sy) * 1.0 / (n * sxx - sx * sx);
      }
      // Save the end points of this line segment.
      region.firstFrequency = m * region.begin + b;
      region.lastFrequency = m * (region.end - 1) + b;
      // If one of the end points is less than 0, adjust it.  Change the slope
      // of the line segment, but keep the center point the same, so the area
      // under the line segment (the total frequency of the entire group) stays
      // the same.
      if (region.firstFrequency < 0)
      {	
	region.lastFrequency += region.firstFrequency;
	region.firstFrequency = 0;
      }
      else if (region.lastFrequency < 0)
      {
	region.firstFrequency += region.lastFrequency;
	region.lastFrequency = 0;
      }
      unscaledRegions.push_back(region);
    }
    double maxFrequency = 0;
    for (UnscaledRegion &region : unscaledRegions)
    {
      maxFrequency = std::max(maxFrequency, region.firstFrequency);
      maxFrequency = std::max(maxFrequency, region.lastFrequency);
    }
    const double scaleFactor = 0xffffffff / maxFrequency;
    _printFrequencies.reserve(unscaledRegions.size());
    for (UnscaledRegion &region : unscaledRegions)
    {
      PrintRegionInfo final;
      final.begin = region.begin;
      final.end = region.end;
      final.firstFrequency = (uint32_t)(region.firstFrequency * scaleFactor);
      final.lastFrequency = (uint32_t)(region.lastFrequency * scaleFactor);
      _printFrequencies.push_back(final);
    }
  }

  std::vector< PrintRegionInfo > const &getPrintFrequencies() const
  { return _printFrequencies; }
  
  
  static const std::set< std::pair< Instruction, Instruction > >
  legalTransitions;

  static const int instructionFrequenciesScaleBits = 8;
  
  void loadInstructionFrequencies(std::map< std::pair< Instruction,
				  Instruction >, int64_t > pairCounts)
  {
    std::map< Instruction, int64_t > bySource;
    for (auto kvp : pairCounts)
    {
      const auto instructions = kvp.first;
      assert(legalTransitions.count(instructions));
      const Instruction source = instructions.first;
      const auto frequency = kvp.second;
      assert(frequency > 0);
      bySource[source] += frequency;
    }
    const auto summarize = [&pairCounts, &bySource]
      (Instruction source, Instruction current, uint8_t &scaled)
      {
	const auto total = bySource[source];
	if (total == 0)
	  scaled = 128;  // No data so say 50% - 50%.
	else
	{
	  const auto ourPart = pairCounts[std::make_pair(source, current)];
	  if (ourPart == 0)
	    scaled = 0;  // Exactly 0.  Anything more and we'd round up.
	  else
	    scaled =
	      std::max(1, std::min(255, (int)round(ourPart * 256.0 / total)));
	}
      };
    summarize(Instruction::DeleteTopEntry, Instruction::CreateString,
	      _deleteTopEntry_CreateString);
    summarize(Instruction::CreateString, Instruction::CreateString,
	      _createString_CreateString);
    const auto total = bySource[Instruction::PrintString];
    if (total == 0)
    {
      _printString_DeleteTopEntry = 85;
      _printString_CreateString = 85;
    }
    else
    {
      auto deleteTopEntryRaw =
	pairCounts[std::make_pair(Instruction::PrintString,
				  Instruction::DeleteTopEntry)];
      int deleteTopEntry = (int)round(deleteTopEntryRaw * 256.0 / total);
      if ((deleteTopEntry == 0) && (deleteTopEntryRaw != 0))
	deleteTopEntry = 1;
      else if (deleteTopEntry == 256)
	deleteTopEntry = 255;
      auto createStringRaw =
	pairCounts[std::make_pair(Instruction::PrintString,
				  Instruction::CreateString)];
      int createString = (int)round(createStringRaw * 256.0 / total);
      if ((createString == 0) && (createStringRaw != 0))
	createString = 1;
      else if (createString == 256)
	createString = 255;
      int printString = 256 - deleteTopEntry - createString;
      if (printString == 0)
      {
	auto printStringRaw =
	  pairCounts[std::make_pair(Instruction::PrintString,
				    Instruction::PrintString)];
	if (printStringRaw != 0)
	{
	  printString++;
	  if (createString > deleteTopEntry)
	    createString--;
	  else
	    deleteTopEntry--;
	}
      }
      _printString_DeleteTopEntry = deleteTopEntry;
      _printString_CreateString = createString;
    }
  }
  
  std::map< std::pair< Instruction, Instruction >, int >
  getInstructionFrequencies() const
  {
    std::map< std::pair< Instruction, Instruction >, int > result;
    result[std::make_pair(Instruction::None,
			  Instruction::PrintString)] =
      256;
    result[std::make_pair(Instruction::DeleteTopEntry,
			  Instruction::CreateString)] =
      _deleteTopEntry_CreateString;
    result[std::make_pair(Instruction::DeleteTopEntry,
			  Instruction::PrintString)] =
      256 - _deleteTopEntry_CreateString;
    result[std::make_pair(Instruction::CreateString,
			  Instruction::CreateString)] =
      _createString_CreateString;
    result[std::make_pair(Instruction::CreateString,
			  Instruction::PrintString)] =
      256 - _createString_CreateString;
    result[std::make_pair(Instruction::PrintString,
			  Instruction::DeleteTopEntry)] =
      _printString_DeleteTopEntry;
    result[std::make_pair(Instruction::PrintString,
			  Instruction::CreateString)] =
      _printString_CreateString;
    result[std::make_pair(Instruction::PrintString,
			  Instruction::PrintString)] =
      256 - _printString_DeleteTopEntry - _printString_CreateString;
    return result;
  }
  
  static bool isLittleEndian()
  {
    union
    {
      int8_t asByte[4];
      int32_t asWord;
    } u;
    u.asWord = 0x01020304;
    return (u.asByte[0] == 4) && (u.asByte[1] == 3) && (u.asByte[2] == 2)
      && (u.asByte[3] == 1);
  }

  Header()
  { // For the most part this code would run on a big endian machine, but it
    // would give different results.  Our goal is to create a file format that
    // is identical regardless of the details of the machine creating or
    // reading the file.  This code was written to be simple on an x86 / little
    // endian machine, but there's no reason you couldn't make some changes so
    // the code would give the same results on a big endian machine.
    assert(isLittleEndian());
  }
};

const std::set< std::pair< Instruction, Instruction > >
Header::legalTransitions = {
  {Instruction::None, Instruction::PrintString},
  {Instruction::DeleteTopEntry, Instruction::PrintString},
  {Instruction::DeleteTopEntry, Instruction::CreateString},
  {Instruction::PrintString, Instruction::DeleteTopEntry},
  {Instruction::PrintString, Instruction::PrintString},
  {Instruction::PrintString, Instruction::CreateString},
  {Instruction::CreateString, Instruction::PrintString},
  {Instruction::CreateString, Instruction::CreateString}};

class Mru
{
private:
  std::vector< FileSlice > _items;

public:
  Mru()
  {
    _items.reserve(getBootStrapData().length());
    for (char const &ch : getBootStrapData())
      _items.push_back(FileSlice(&ch, 1));
  }
  
  void pop()
  {
    assert(!_items.empty());
    _items.resize(_items.size()-1);
    //_items.erase(_items.begin() + _items.size() - 2);
  }
  void push(FileSlice const &value)
  {
    //assert(!_items.count(value));  // This would be expensive!
    _items.push_back(value);
    //_items.insert(_items.begin() + _items.size() - 2, value);
  }
  int get(FileSlice const &value)
  {
    for (int index = _items.size() - 1; index >= 0; index--)
    {
      if (_items[index] == value)
      {
	_items.erase(_items.begin() + index);
	_items.push_back(value);
	//_items.insert(_items.begin() + _items.size() - 2, value);
	return _items.size() - index - 1;
      }
    }
    abort();
  }
  size_t size() const { return _items.size(); }
};

void describeSlantRANS(std::map< uint32_t, int64_t > const &frequencies)
{
  int64_t totalOccurances = 0;
  for (auto kvp : frequencies)
    totalOccurances += kvp.second;

  double totalActualCost = 0.0;
  double totalIdealCost = 0.0;
  
  // This is not 100% right.  We might have to go the highest possible number,
  // not just the highest that we've actually seen.  Currently pass 2 creates
  // an estimate and pass 3 might not use exactly the same values.
  // lastPossible should come from the highWaterMark or similar.
  const int lastPossible = frequencies.rbegin()->first;
  const int endBefore = lastPossible + 1;
  int groupSize = 1;
  int groupStart = 0;
  while (groupStart < endBefore)
  {
    int nextStart = std::min(groupStart + groupSize, endBefore);
    groupSize = nextStart - groupStart;

    double sx = 0;
    double sy = 0;
    double sxx = 0;
    double syy = 0;
    double sxy = 0;
    for (int x = groupStart; x < nextStart; x++)
    {
      const auto it = frequencies.find(x);
      const double y =
	((it==frequencies.end())?0:it->second)/(double)totalOccurances;
      sx += x;
      sy += y;
      sxx += x*x;
      syy += y*y;
      sxy += x*y;
    }
    double m, b;  // y = mx + b
    if (groupSize == 1)
    { // If we have just one point, assume it's a horizontal line.
      b = sy;
      m = 0;
    }
    else
    {
      b = (sy * sxx - sx * sxy) * 1.0 / (groupSize * sxx - sx * sx);
      m = (groupSize * sxy - sx * sy) * 1.0 / (groupSize * sxx - sx * sx);
    }
    
    double groupRansCost = 0;
    double groupIdealCost = 0;

    const auto begin = frequencies.lower_bound(groupStart);
    const auto end = frequencies.lower_bound(nextStart);
    for (auto it = begin; it != end; it++)
    { 
      const auto frequency = it->second;
      double acutalProbability = frequency / (double)totalOccurances;
      const double estimatedProbability = m * it->first + b;
      const double ransCost = frequency * -log2(estimatedProbability);
      groupRansCost += ransCost;
      totalActualCost += ransCost;
      const double idealCost = frequency * -log2(acutalProbability);
      groupIdealCost += idealCost;
      totalIdealCost += idealCost;
    }

    std::cout<<groupStart<<" - "<<(nextStart-1)<<":  probability "
	     <<(m * groupStart + b)<<" - "<<(m * (nextStart-1) + b)
	     <<", "<<groupRansCost
	     <<" actual cost, "<<groupIdealCost<<" ideal cost."<<std::endl;
    
    groupStart = nextStart;
    groupSize *= 2;
  }
  std::cout<<"TOTAL:  "<<totalActualCost<<" actual cost, "
	   <<totalIdealCost<<" ideal cost."<<std::endl;
}

// TODO This assumes that we are always considering the frequencies for every
// value that was ever possible.  However, we can cheat.  When we first start
// there are only 256 possibly entries.  That number can build up to something
// bigger near the middle of our computation.  Then it will drop again, finally
// being even lower than were we started.  We should start with our estimate of
// the frequencies (as stored in the header) but then use the current size of
// the MRU list to set many of these probabilities to 0.  Note, the way way we
// plan to use integrals to compute the probabilities, rather than listing out
// the individual probabilities of each item means that we don't need to
// rebuild the entire table each time the MRU list grows or shrinks.
template < class T >
int64_t getArithmeticCodingCost(std::map< T, int64_t > const &frequencies)
{
  int64_t totalUseCount = 0;
  for (auto kvp : frequencies) { totalUseCount += kvp.second; }
  double totalCost = 0.0;
  for (auto kvp : frequencies)
  {
    double useCount = kvp.second;
    double probability = useCount / totalUseCount;
    double cost = useCount * -log2(probability);
    totalCost += cost;
  }
  return totalCost;
}


class RANSWriter
{
private:
  const std::string _fileName;
  const std::string _header;
  Rans64State _rans64State;
  union {
    uint32_t *_ptr;
    char *_cptr;
  };
  std::string _data;
  char *&cptr() { return _cptr; }
  char *cptr() const { return _cptr; }
  void ensureFreeSpace()
  {
    const size_t minFreeSpace = 128;
    const size_t initialFreeSpace = cptr() - &_data[0];
    if (initialFreeSpace < minFreeSpace)
    {
      const size_t additionalFreeSpace =
	std::max(minFreeSpace, _data.length());
      _data.insert(0, additionalFreeSpace, 'P');
      cptr() = &_data[initialFreeSpace+additionalFreeSpace];
    }
  }

  void close()
  {
    ensureFreeSpace();
    Rans64EncFlush(&_rans64State, &_ptr);
    std::ofstream file(_fileName, std::ios_base::out | std::ios_base::trunc);
    file.write(_header.c_str(), _header.length());
    size_t unusedSpace = cptr() - &_data[0];
    file.write(&_data[unusedSpace], _data.length() - unusedSpace);
  }
  
public:
  RANSWriter(std::string const &fileName, std::string const &header) :
    _fileName(fileName), _header(header)
  {
    Rans64EncInit(&_rans64State);
    cptr() = &_data[0];
  }

  ~RANSWriter() { close(); }
  
  void add(Rans64EncSymbol const* sym, uint32_t scale_bits)
  {
    ensureFreeSpace();
    Rans64EncPutSymbol(&_rans64State, &_ptr, sym, scale_bits);
  }

  void add(uint32_t start, uint32_t freq, uint32_t scale_bits)
  {
    ensureFreeSpace();
    Rans64EncPut(&_rans64State, &_ptr, start, freq, scale_bits);
  }

  void dump() const
  {
    size_t unusedSpace = cptr() - &_data[0];
    std::cout<<"Final size in bytes: header="<<_header.length()
	     <<", body="<<(_data.length() - unusedSpace)
	     <<", total="<<(_header.length() + _data.length() - unusedSpace)
	     <<std::endl;
  }
};

// TrapezoidStats was made for use with rANS.  Sometimes we explicitly state
// the frequency of all symbols.  This class lets us estimate the frequencies.
// The symbols are all consecutive numbers.  You specify the frequencies of the
// first and last number, and this class interpolates the values in between.
//
// The plan is to represent the MRU list as a series of line segments.  That's
// why this starts from firstX rather than 0.  Perhaps the first line segment
// goes from 0 to 10, but the next line segment goes from 11 to 30.  So the
// first trapezoid will have firstX = 0 and the second will have firstX = 11.
//
// We will always spit out values between 0 and totalFrequency.  The start of
// item N+1 is always the start of item N + the frequency of item N.  In other
// words, the output of get() is a perfect input for a rANS encoder.
//
// We do some rounding to make this work.  It's possible that you expect two
// symbols to have the same frequency, but one has a frequency that is larger
// than the other by one.  In we try to spread those extra spaces out evenly.
// In any case, the rounding is completely deterministic, so different machines
// and different compilers should always give you the exact same result.
//
// This class is fairly light weight.  In particular, we don't list out every
// symbol.  We can compute the start and frequency for the requested symbol
// without computing any others.  We expect these objects to change a bit.  You
// might have 256 symbols in the MRU list, so this will go from 0 to 255.  But
// that list is constantly growing and shrinking, so next time you might need
// a different trapezoid with fewer or more symbols in it.
//
// We use the word "trapezoid" as a reminder that we are integrating the area
// below the line segment.
class TrapezoidStats
{
private:
  uint32_t _firstX;
  uint32_t _count;
  int64_t _intercept;
  int64_t _slope;
  int64_t _scale;

  // x should be between 0 and _count, inclusive.
  int64_t unscaledGet(int64_t x) const
  { // The basic formula for the sum of an arithmetic series is pretty
    // streightforward.  It's the average of the first and last items times
    // the number of items.
    //
    // We play some games with x before we get started.  We compute the slope
    // and intercept as if the first index was always 0.  That works for
    // computing the number of items AT one particular position.  (We are using
    // line segments to approximate the number of items at any one position,
    // so we can use the standard formula y = mx + b for that.)  But the
    // obvious formula to compute the sum at x would give us the sum for
    // everything up to and including x.  If we wanted to know everything up
    // to but NOT including x we'd have to subtract 1 from x.  Normally that
    // would work perfectly, but in this case we're working with unsigned
    // integers, and 0 is a reasonable first value.  If we tried to use a -1
    // here it would fail badly.  So I took the obvious formula for the sum and
    // translated it so 0, not -1, would give us everything before the first
    // item.  Also, the value for 0 always comes out perfectly.  If we tried to
    // plug in -1, even if we got around the issue of negative numbers, we'd
    // still have round off error for 0.
    //
    // There's also the issue of scaling.  We want the total at the far extreme
    // to be exactly totalFrequency.  (totalFrequency is the largest
    // denominator the rANS library will accept, and it gives us the most
    // precision.)  We could try computing the sum first, then multiplying by
    // totalFrequency then dividing by the highest value before we did ANY
    // scaling.  With real numbers that's a perfect way to set the scale.  But
    // with integers we often lose way too much precision and the result is
    // meaningless.  Instead we multiply the inputs by a large number, making
    // them as large as we feel comfortable with.  This is done once in the
    // constructor.  Then we compute _slope and _intercept.  (We also divide
    // _slope by 2 at that same time, just to avoid doing that work over and
    // over.)  Whoever calls this should divide the result by _scale to get a
    // number in the proper scale.
    assert(x <= _count);
    return (_intercept + _slope * (x - 1)) * x;
  }

  // Returns the cumulative total for all items up to but not including x.
  // get(_firstX) should return 0.  get(x + 1) - get(x) should return
  // the frequency of item x.
  //
  // Valid values of x are 0 through (_firstX + _count).  In terms of the
  // inputs to the constructor, valid values of x are firstX through (lastX +
  // 1).
  uint32_t get(uint32_t x) const
  {
    assert(x >= _firstX);
    x -= _firstX;
    if (x == _count)
      return totalFrequency;
    int64_t result = unscaledGet(x);
    // Rescale so we always go from 0 to totalFrequency.
    result /= _scale;
    assert(result <= totalFrequency);
    return result;
  }

  static int floorLog2(int64_t x)
  {
    return 63 - __builtin_clzl(x);
  }

  static int ceilLog2(int64_t x)
  {
    if (x == 1)
      // On my system __builtin_clzl(0) returns 63.  64 would make more sense
      // and would be more consistent.  According to stackoverflow this result
      // can get even stranger and you should just avoid __builtin_clzl(0).
      return 0;
    else
      return floorLog2(x-1) + 1;
  }

  // Separate this from the constructor.  That allows us to use recursion.
  // If we start with the basic algorithm and we get an edge case, we can
  // always retry with simpler inputs.
  void init(uint32_t firstFreq, uint32_t lastFreq)
  {
    // Deal with 0's.
    if ((firstFreq == 0) && (lastFreq == 0))
    { // Nothing exciting, just avoid a /0 error.  We'll look at the case where
      // only one side is 0 in a moment.
      firstFreq = 1;
      lastFreq = 1;
    }

    // See how big the total frequency would be, the sum of all items in
    // this trapezoid.  Use a different formula than the one we'll use for
    // the individual items.  This formula is more precise, regardless of any
    // scaling.  (This formula only works for the entire size, we have to use
    // a different formula for the indivual items.)  Use this to figure out
    // how much bigger we can make the numbers without fear of an overflow.
    // multiply the inputs by that number.  Using bigger numbers will help
    // us limit round off errors.
    int64_t initialTotalSize = firstFreq;
    initialTotalSize += lastFreq;
    initialTotalSize *= _count;
    initialTotalSize /= 2;
    const int extraBits = 62 - ceilLog2(initialTotalSize);
    assert(extraBits >= 0 || "The input was too big!");
    const int64_t firstFreq64 = ((int64_t)firstFreq) << extraBits;
    const int64_t lastFreq64 = ((int64_t)lastFreq) << extraBits;

    // Update slope and intercept.
    { // Fencepost:  If there are 12 items, the distance between the
      // first and last is only 11.
      const int64_t run = _count-1;
      if (run == 0)
	// Only one data point.  Any slope would be correct.
	_slope = 0;
      else
	// We're actually storing slope/2, as the other formulas expect.
	_slope = (lastFreq64 - firstFreq64 + run) / (run * 2);
      _intercept = firstFreq64;
    }

    // Update scale
    {
      const int64_t totalUnscaled = unscaledGet(_count);
      // Try to set _scale so the largest output from unscaledGet() / _scale
      // will equal totalFrequency.  Note:  We can't always get this perfect.
      // get(uint32_t) will take care of the details so it will return
      // exactly totalFrequency when it should.  The difference between the
      // ideal value and actual value from unscaledGet()/_scale is typically
      // very small.
      _scale = (totalUnscaled + totalFrequency/2) / totalFrequency;
    }

    // Ordinary cases are done now.  Check for special cases.

    // Check for one side or the other being 0 after the scaling.  It's
    // possible that one side was much smaller than the other, and got set to
    // 0 after the scaling.  Go ahead and set the minimum to 8 rather than
    // 1, just to be safe.  Typical small values for a frequency are in the
    // range of 2^15 - 2^16.  So this is pretty close to 0 for most purposes.
    static const uint32_t MIN_LEGAL = 8;
    bool fixFirst = false;
    bool fixLast = false;
    uint32_t start;
    uint32_t freq;
    get(_firstX, start, freq);
    if (freq < MIN_LEGAL)
      fixFirst = true;
    else
    {
      get(_firstX + _count - 1, start, freq);
      if (freq < MIN_LEGAL)
      fixLast = true;
    }
    if (fixFirst || fixLast)
    { // Draw a trapezoid.  Draw a horizontal line for the base.  The length
      // of the of the base is _count.  Draw two vertical lines for the sides
      // touching the base.  The shorter one will have MIN_LEGAL for its
      // height.  The area of the trapezoid is totalFrequency.  Notice the
      // formula we used above to find the initialTotalSize.  Use some algebra
      // to rearrange that formula and solve for the height of the larger
      // vertical side.
      int64_t tallerSide = totalFrequency;
      tallerSide *= 2;
      tallerSide = (tallerSide + _count/2) / _count;
      tallerSide -= MIN_LEGAL;
      assert((tallerSide >= MIN_LEGAL) || "_count is WAY too big.");
      std::cout<<"Trying again because the "<<(fixFirst?"first":"last")
	       <<" frequency was too small.  "
	       <<"_firstX="<<_firstX
	       <<", _count="<<_count
	       <<", _scale="<<_scale
	       <<", _slope="<<_slope
	       <<", _intercept="<<_intercept
	       <<", initial first frequency="<<firstFreq
	       <<", initial last frequency="<<lastFreq;
      if (fixFirst)
      {
	firstFreq = MIN_LEGAL;
	lastFreq = tallerSide;
      }
      else
      {
	firstFreq = tallerSide;
	lastFreq = MIN_LEGAL;	
      }
      std::cout<<", new first frequency="<<firstFreq
	       <<", new last frequency="<<lastFreq
	       <<std::endl;
      // Try again.
      init(firstFreq, lastFreq);
    }
  }
  
public:
  TrapezoidStats() :
    // Uninitialized.  This is just enough so the compiler won't complain if
    // we create the object before we set it to a reasonable value.  Setting
    // _count = 0 means that any attempt to get() a frequency from here will
    // fail an assertion.
    _firstX(0), _count(0), _intercept(totalFrequency), _slope(0), _scale(1) { }
  
  TrapezoidStats(uint32_t firstX, uint32_t firstFreq,
		 uint32_t lastX, uint32_t lastFreq) :
    _firstX(firstX), _count(lastX - firstX + 1)
  {
    assert(firstX <= lastX);
    init(firstFreq, lastFreq);
  }

  // start, freq, and totalFrequency are exactly the inputs we need for
  // the rANS encoder.
  void get(uint32_t x, uint32_t &start, uint32_t &freq) const
  {
    start = get(x);
    uint32_t end = get(x+1);
    freq = end - start;
  }

  static const uint32_t scaleBits = 31;
  static const uint32_t totalFrequency = 1<<scaleBits;

  static void dump3(std::ostream &dest, uint32_t value)
  {
    std::ios oldState(nullptr);
    oldState.copyfmt(dest);
    dest<<value<<" = 0x"<<std::hex<<value<<" = "
	<<std::setiosflags(std::ios::fixed) << std::setprecision(3)
	<<(value * 100.0 / totalFrequency)<<'%';
    dest.copyfmt(oldState);
  }

  void debugDump(uint32_t x) const
  {
    uint32_t start, freq;
    get(x, start, freq);
    std::cout<<"x = "<<x<<", start = ";
    dump3(std::cout, start);
    std::cout<<", frequency = ";
    dump3(std::cout, freq);
    std::cout<<std::endl;
  }

  void debugDump() const
  {
    std::cout<<"_firstX = "<<_firstX
	     <<", _count = "<<_count
	     <<", _intercept = "<<_intercept
	     <<", _slope = "<<_slope
	     <<", _scale = "<<_scale<<std::endl;      
    static const uint32_t MAX_PER_SIDE = 5;
    for (uint32_t i = 0; i < std::min(MAX_PER_SIDE, _count); i++)
      debugDump(i + _firstX);
    for (uint32_t i = std::max(MAX_PER_SIDE, _count - MAX_PER_SIDE);
	 i < _count; i++)
      debugDump(i + _firstX);
    std::cout<<"end = ";
    dump3(std::cout, get(_firstX + _count));
    std::cout<<std::endl;
  }

  static void interactiveDebug()
  {
    /*
    std::cout<<"floorLog2("<<totalFrequency<<") = "<<floorLog2(totalFrequency)
	     <<", ceilLog2("<<totalFrequency<<") = "<<ceilLog2(totalFrequency)
	     <<", expecting "<<scaleBits<<std::endl; 
    for (int i = 1; i < 35; i++)
      std::cout<<"floorLog2("<<i<<") = "<<floorLog2(i)
	       <<", ceilLog2("<<i<<") = "<<ceilLog2(i)<<std::endl;
    */
    while (true)
    {
      std::cout<<"firstX?  ";
      uint32_t firstX;
      std::cin>>firstX;
      std::cout<<"firstFreq?  ";
      uint32_t firstFreq;
      std::cin>>firstFreq;
      std::cout<<"lastX?  ";
      uint32_t lastX;
      std::cin>>lastX;
      std::cout<<"lastFreq?  ";
      uint32_t lastFreq;
      std::cin>>lastFreq;
      if (!std::cin)
	return;
      TrapezoidStats trapezoidStats(firstX, firstFreq, lastX, lastFreq);
      trapezoidStats.debugDump();
    }
  }
  
};

class TrapezoidList
{
private:
  struct Entry
  {
    Header::PrintRegionInfo info;
    TrapezoidStats stats;
    int64_t totalFrequency;
    uint32_t start;
    uint32_t scaledFreq;
  };
  std::map< uint32_t, Entry > _byFirstValue;
  
public:
  // This makes one big list which is good for the entire file.  We know that's
  // overkill most of the time.  Both the reader and the writer know the
  // largest value that's currently possible, so we could scale this down.
  // We could have a slightly different list almost every time we encode an
  // MRU item.  TODO
  TrapezoidList(Header const &header)
  {
    int64_t totalFrequency = 0;
    int64_t largestFrequency = -1;
    uint32_t indexOfLargestFrequency;
    for (Header::PrintRegionInfo const &info : header.getPrintFrequencies())
    {
      Entry entry;
      entry.info = info;
      if (info.isSingle())
      {
	entry.totalFrequency = info.firstFrequency;
      }
      else
      {
	entry.stats = TrapezoidStats(info.begin, info.firstFrequency,
				     info.end-1, info.lastFrequency);
	entry.totalFrequency = info.firstFrequency;
	entry.totalFrequency += info.lastFrequency;
	entry.totalFrequency *= info.size();
	entry.totalFrequency /= 2;
      }
      _byFirstValue[info.begin] = entry;
      totalFrequency += entry.totalFrequency;
      if (entry.totalFrequency > largestFrequency)
      {
	largestFrequency = entry.totalFrequency;
	indexOfLargestFrequency = info.begin;
      }
    }
    const int64_t scale =
      (totalFrequency + TrapezoidStats::totalFrequency/2)
      / TrapezoidStats::totalFrequency;
    int64_t totalScaledFreq = 0;
    for (auto &kvp : _byFirstValue)
    {
      Entry &entry = kvp.second;
      entry.scaledFreq = entry.totalFrequency / scale;
      if (entry.scaledFreq == 0)
	// In some similar places we check to see if a frequency was always 0,
	// or if it became 0 because of round off error.  Since we are only
	// setting this to 1 / (1<<31) it doesn't hurt us to always include it.
	entry.scaledFreq = 1;
      totalScaledFreq += entry.scaledFreq;
    }
    const int64_t excess = totalScaledFreq - TrapezoidStats::totalFrequency;
    _byFirstValue[indexOfLargestFrequency].scaledFreq -= excess;
    int64_t scaledFreqSoFar = 0;
    for (auto &kvp : _byFirstValue)
    {
      Entry &entry = kvp.second;
      entry.start = scaledFreqSoFar;
      scaledFreqSoFar += entry.scaledFreq;
    }
    assert(scaledFreqSoFar == TrapezoidStats::totalFrequency);
  }

  double debugGetFrequency(uint32_t value) const
  {
    const auto it = --_byFirstValue.upper_bound(value);
    assert(it != _byFirstValue.end());
    Entry const &entry = it->second;
    double result = entry.scaledFreq;
    result /= TrapezoidStats::totalFrequency;
    if (!entry.info.isSingle())
    {
      uint32_t start, freq;
      entry.stats.get(value, start, freq);
      result *= freq;
      result /= TrapezoidStats::totalFrequency;
    }
    return result;
  }
  
  void add(RANSWriter &writer, uint32_t value) const
  {
    const auto it = --_byFirstValue.upper_bound(value);
    assert(it != _byFirstValue.end());
    Entry const &entry = it->second;
    if (!entry.info.isSingle())
    {
      uint32_t start, freq;
      entry.stats.get(value, start, freq);
      writer.add(start, freq, TrapezoidStats::scaleBits);
      //if (debugPrintNow)
      //std::cout<<"("<<start<<", "<<freq<<"), ";
    }
    // Remember, the decompressor will read these in the opposite order.  The
    // decompressor will need the information from the following add() to know
    // how to interpret the information from the previous add().
    writer.add(entry.start, entry.scaledFreq, TrapezoidStats::scaleBits);
    //if (debugPrintNow)
    //  std::cout<<"("<<entry.start<<", "<<entry.scaledFreq<<")"<<std::endl;
  };
  
  void dump(std::map< uint32_t, int64_t > const &printFrequencies) const
  {
    for (auto const &kvp : _byFirstValue)
    {
      Entry const &entry = kvp.second;
      std::cout<<"*** First Value = "<<kvp.first<<" *** start = ";
      TrapezoidStats::dump3(std::cout, entry.start);
      std::cout<<", freq = ";
      TrapezoidStats::dump3(std::cout, entry.scaledFreq);
      std::cout<<std::endl
	       <<"Total Frequency = "<<entry.totalFrequency<<std::endl;
      entry.info.dump();
      entry.stats.debugDump();
    }

    for (int i = 0; i < 50; i++)
      std::cout<<"Frequency of mru("<<i<<") = "<<(debugGetFrequency(i)*100)
	       <<"%"<<std::endl;
    const int max = _byFirstValue.rbegin()->second.info.end;
    for (int i = max - 50; i < max; i++)
      std::cout<<"Frequency of mru("<<i<<") = "<<(debugGetFrequency(i)*100)
	       <<"%"<<std::endl;

    int64_t totalFrequency = 0;
    for (auto kvp : printFrequencies)
    {
      int64_t const &actualFrequency = kvp.second;
      totalFrequency += actualFrequency;
    }
    double totalBitCost = 0;
    for (auto kvp : printFrequencies)
    {
      uint32_t const &index = kvp.first;
      int64_t const &actualFrequency = kvp.second;
      const double estimatedFrequency = debugGetFrequency(index);
      const double cost = actualFrequency * -log2(estimatedFrequency);
      totalBitCost += cost;
    }
    std::cout<<"TrapezoidList estimated cost "<<(int64_t)totalBitCost
	     <<" bits, "<<(int64_t)ceil(totalBitCost/8)
	     <<" bytes."<<std::endl;
  }
};


class Compressor
{
private:

  // Prepare the data that goes with each Instruction::CreateString.
  static std::map< uint8_t, Rans64EncSymbol >
  prepRANS(std::map< uint8_t, uint16_t > const &frequencies)
  {
    std::map< uint8_t, Rans64EncSymbol > result;
    uint32_t cumulativeFrequency = 0;
    for (auto kvp : frequencies)
    {
      const uint8_t count = kvp.first;
      const uint16_t frequency = kvp.second;
      Rans64EncSymbol &symbol = result[count];
      Rans64EncSymbolInit(&symbol, cumulativeFrequency, frequency,
			  Header::createStringScaleBits);
      cumulativeFrequency += frequency;
    }
    assert(cumulativeFrequency == Header::createStringTotalFreq);
    return result;
  }
  
  // Prepare to store each Instruction.
  static std::map< std::pair< Instruction, Instruction >, Rans64EncSymbol >
  prepRANS(std::map< std::pair< Instruction, Instruction >, int > const &f)
  {
    std::map< std::pair< Instruction, Instruction >, Rans64EncSymbol > result;
    std::map< Instruction, uint32_t > cumulativeFrequencies;
    for (auto kvp : f)
    {
      Instruction previousInstruction = kvp.first.first;
      Instruction newInstruction = kvp.first.second;
      int frequency = kvp.second;
      uint32_t &cumulativeFrequency =
	cumulativeFrequencies[previousInstruction];
      Rans64EncSymbol &symbol =
	result[std::make_pair(previousInstruction, newInstruction)];
      Rans64EncSymbolInit(&symbol, cumulativeFrequency, frequency,
			  Header::instructionFrequenciesScaleBits);
      cumulativeFrequency += frequency;
    }
    assert([&]()
	   {
	     for (auto kvp : cumulativeFrequencies)
	       if (kvp.second != (1<<Header::instructionFrequenciesScaleBits))
		 return false;
	     return true;
	   }());
    return result;
  }
  
  // We have a lot of pointers into this string.  We need to keep this string
  // alive as long as those other pointers are alive.
  const std::string _wholeFile;

  FileSlice getWholeFile() const { return FileSlice(_wholeFile); }

  const std::vector< int > _combineCounts;
  
  const int _maxCombineCount;
  
  CountedStrings _unoptimizedStrings;

  void firstPass()
  {
    FileSlice input = getWholeFile();
    std::cout<<"Compressing "<<input.length()<<" bytes."<<std::endl;
    std::vector< FileSlice > recentlyPrintedSubStrings;
    while (!input.empty())
    {
      const FileSlice nextSubString = _unoptimizedStrings.longestPrefix(input);
      //std::cout<<"Pass 1 printing:  "<<quote(nextSubString)<<" ("<<strings[nextSubString]<<')'<<std::endl; 
      input.pushForward(nextSubString.length());
      recentlyPrintedSubStrings.push_back(nextSubString);
      if ((int)recentlyPrintedSubStrings.size() > _maxCombineCount)
	recentlyPrintedSubStrings.erase(recentlyPrintedSubStrings.begin());
      for (int combineCount : _combineCounts)
	if (combineCount <= (int)recentlyPrintedSubStrings.size())
	{
	  //std::cout<<quote(lastSubString)<<" + "<<quote(nextSubString)
	  //       <<" -> "<<quote(lastSubString + nextSubString)<<std::endl;
	  //if (strings.size() < 8912)  // A somewhat arbitrary cutoff, a test.
	  // Limiting the number of entries makes things a lot faster, but the
	  // file size grows more than I'd like.
	  FileSlice newString(*(recentlyPrintedSubStrings.end()-combineCount), nextSubString);
	  _unoptimizedStrings.add(newString);
	}
    }
    _unoptimizedStrings.trimZeros();
  }

  CountedStrings _recentStrings;
  Header _header;

  template < class T >
  void dumpAllFrequencies(std::map< T, int64_t > const &frequencies,
			  std::string const &fileName)
  { return;  // temporarily disabled.
    std::fstream out(fileName.c_str(),
		     std::ios_base::out | std::ios_base::trunc);
    out<<"r,freq"<<std::endl;
    for (auto kvp : frequencies)
      out<<kvp.first<<','<<kvp.second<<std::endl;
  }

  InstructionList _instructionList;
  CombineCountList _combineCountList;
  MruIndexList _mruIndexList;
  
  void secondPass()
  {
    FileSlice input = getWholeFile();
    Mru mru;
    std::vector< FileSlice > recentlyPrintedSubStrings;

    //std::map< int, int > printCountByMruSize;
    
    while (!input.empty())
    {
      const FileSlice nextSubString = _recentStrings.longestPrefix(input);
      //std::cout<<"Longest prefix:  "<<quote(nextSubString)<<std::endl;
      const auto simulateWrite = [&](FileSlice toWrite)
	{
	  int index = mru.get(toWrite);
	  _instructionList.push(Instruction::PrintString);
	  _mruIndexList.push(index, mru.size());
	  // Check for done.
	  auto currentCount = _recentStrings.getCount(toWrite);
	  auto maxCount = _unoptimizedStrings.getCount(toWrite);
	  if (currentCount == maxCount)
	  { // Last use of this instruction.
	    //std::cout<<"Deleting "<<quote(toWrite)<<std::endl;
	    mru.pop();
	    _instructionList.push(Instruction::DeleteTopEntry);
	  }
	};
      simulateWrite(nextSubString);
      input.pushForward(nextSubString.length());
      recentlyPrintedSubStrings.push_back(nextSubString);
      if ((int)recentlyPrintedSubStrings.size() > _maxCombineCount)
	recentlyPrintedSubStrings.erase(recentlyPrintedSubStrings.begin());
      for (size_t index = 0; index < _combineCounts.size(); index++)
      {
	const int combineCount = _combineCounts[index];
	if (combineCount <= (int)recentlyPrintedSubStrings.size())
	{
	  const FileSlice possibleNewString(*(recentlyPrintedSubStrings.end()-combineCount), nextSubString);
	  if (_unoptimizedStrings.contains(possibleNewString))
	  {
	    _recentStrings.add(possibleNewString);
	    // We should save this because someone will use it.
	    mru.push(possibleNewString);
	    _instructionList.push(Instruction::CreateString);
	    _combineCountList.push(combineCount);
	    //std::cout<<"Adding new string:  "<<quote(possibleNewString)
	    //     <<std::endl;
	  }
	  //else std::cout<<"Skipping unused string:  "<<quote(possibleNewString)<<std::endl;
	}
      }
    }

    _instructionList.removeFinalDeletes();
    
    //std::ofstream mruSize("mru_size.txt",
    //		  std::ios_base::out | std::ios_base::trunc);
    //mruSize<<"mru size,number of prints"<<std::endl;
    //for (auto kvp : printCountByMruSize)
    //  mruSize<<kvp.first<<','<<kvp.second<<std::endl;
    //mruSize.close();
  
    _header.loadInstructionFrequencies(_instructionList.getFrequencies());
    int64_t totalInstructionCount = 0;
    for (auto kvp : _instructionList.getFrequencies())
    {
      std::cout<<kvp.first.first<<" -> "<<kvp.first.second<<" seen "
	       <<kvp.second<<" times."<<std::endl;
      
    }
    std::cout<<"A total of "<<totalInstructionCount<<" instructions."
	     <<std::endl;

    /*
    int lineCounter = 10;
    std::cout<<_unoptimizedStrings.size()<<" strings in our table."<<std::endl;
    for (auto kvp : frequencies)
    {
      Instruction instruction = kvp.first;
      int frequency = kvp.second;
      debugWrite(std::cout, instruction)<<" was used "<<frequency<<" times."
					<<std::endl;
      totalCount += frequency;
      lineCounter--;
      if (lineCounter <= 0) break;
    }
    int highestInstruction = ((int)frequencies.rbegin()->first) + 1;
    std::cout<<"Highest instruction:  "<<highestInstruction<<" (2^"
	     <<log2(highestInstruction)
	     <<") possible entries in the dictionary."<<std::endl;
    std::cout<<"Actual entries in dictionary:  "<<frequencies.size()<<std::endl;
    const int dictionarySize =
      frequencies.size()*(2+(int)ceil(log2(highestInstruction)));
    std::cout<<"Dictionary size:  "<<dictionarySize<<" bits"<<std::endl;
    */
    
    dumpAllFrequencies(_combineCountList.getFrequencies(),
		       "/tmp/createSizeFrequencies.txt");
    _header.loadCreateFrequencies(_combineCountList.getFrequencies());
    std::cout<<"Body size for CreateString after arithmetic coding is "
	     <<getArithmeticCodingCost(_combineCountList.getFrequencies())
	     <<"."<<std::endl;

    dumpAllFrequencies(_mruIndexList.getFrequencies(),
		       "/tmp/printLocationFrequencies.txt");
    _header.loadPrintFrequencies(_mruIndexList.getFrequencies());
    std::cout<<"Body size for PrintString after arithmetic coding is "
	     <<getArithmeticCodingCost(_mruIndexList.getFrequencies())<<"."
	     <<std::endl;
    describeSlantRANS(_mruIndexList.getFrequencies());
  
    std::cout<<"Max is PrintString("
	     <<_mruIndexList.getFrequencies().rbegin()->first
	     <<"), count is "<<_mruIndexList.getFrequencies().size()
	     <<'.'<<std::endl;
    /*
    std::map< int, int > groupBy;
    for (auto kvp : printLocationFrequencies)
      groupBy[kvp.second]++;
    for (auto kvp : groupBy)
    {
      std::cout<<kvp.second<<" PrintString indexes were used "<<kvp.first
	       <<" times."<<std::endl;
    }
    */
    _header.dump();
  }

  void thirdPass()
  {
    const auto instructionSymbols =
      prepRANS(_header.getInstructionFrequencies());
    const auto createSymbols = prepRANS(_header.getCreateFrequencies());
    TrapezoidList mruFrequencies(_header);
    mruFrequencies.dump(_mruIndexList.getFrequencies());
    std::cout<<"Instruction count:  "<<_instructionList.size()
	     <<", MRU count:  "<<_mruIndexList.size()
	     <<", Create string count:  "<<_combineCountList.size()
	     <<std::endl;
    _mruIndexList.debugDump("mru_list.txt");
    
    //RANSWriter writer("compressed.PDS", _header.asBinaryString());
    RANSWriter instructionWriter("instructions.PDS", "");
    RANSWriter countWriter("counts.PDS", "");
    RANSWriter mruWriter("mru.PDS", "");
    while (!_instructionList.empty())
    {
      Instruction next;
      Instruction previous;
      _instructionList.pop(next, previous);
      if (next == Instruction::CreateString)
      {
	const auto it = createSymbols.find(_combineCountList.pop());
	assert(it != createSymbols.end());
	Rans64EncSymbol const &symbol = it->second;
	//writer.add(&symbol, Header::createStringScaleBits);
	countWriter.add(&symbol, Header::createStringScaleBits);
      }
      else if (next == Instruction::PrintString)
      {
	uint32_t current, max;
	_mruIndexList.pop(current, max);
	//mruFrequencies.add(writer, current);
	mruFrequencies.add(mruWriter, current);
      }
      const auto it = instructionSymbols.find(std::make_pair(previous, next));
      /*
      if (it == instructionSymbols.end())
      {
	for (auto kvp : instructionSymbols)
	  std::cerr<<"("<<kvp.first.first<<","<<kvp.first.second<<") --> "
		   <<kvp.second.freq<<std::endl;
	std::cerr<<"previous="<<previous<<", "<<"next="<<next<<std::endl;
      } 
      */     
      assert(it != instructionSymbols.end());
      Rans64EncSymbol const &symbol = it->second;
      //writer.add(&symbol, Header::instructionFrequenciesScaleBits);
      instructionWriter.add(&symbol, Header::instructionFrequenciesScaleBits);
    }
    assert(_mruIndexList.empty());
    assert(_combineCountList.empty());
    //writer.dump();
  }
  
public:
  Compressor(char const *fileName) :
    _wholeFile(slurp(fileName)), _combineCounts({2, 3, 4, 5, 6}),
    _maxCombineCount(*std::max_element(_combineCounts.begin(),
				       _combineCounts.end())) { }

  void processFile()
  {
    const time_t start = time(NULL);
    firstPass();
    secondPass();
    thirdPass();
    const time_t stop = time(NULL);
    std::cout<<"File processed in "<<(stop - start)<<" seconds."<<std::endl;
  }
  
  void moreStats()
  { // For each string length, find me the string which was used the most.
    std::map< size_t, std::pair< FileSlice, int > > bestByLength;
    for (auto &kvp : _recentStrings)
    {
      auto &best = bestByLength[kvp.first.length()];
      if (kvp.second > best.second)
	best = kvp;
    }
    // Start with the longest string.  Only print a shorer string if it was used
    // more than any string we've printed so far.
    int bestUseCountSoFar = 0;
    for (auto it = bestByLength.rbegin(); it != bestByLength.rend(); it++)
    {
      int useCount = it->second.second;
      if (useCount > bestUseCountSoFar)
      {
	bestUseCountSoFar = useCount;
	std::cout<<"Length:  "<<it->first
		 <<", Use count:  "<<useCount
		 <<", String:  "<<it->second.first.quote()<<std::endl;
      }
    }
    
    std::map< int, int > groupByUseCount;
    for (auto &kvp : _recentStrings)
      groupByUseCount[kvp.second]++;
    int i = 0;
    for (auto it = groupByUseCount.rbegin(); it != groupByUseCount.rend(); it++)
    {
      std::cout<<it->second<<" strings were used "<<it->first<<" times."
	       <<std::endl;
      if (++i >= 10) break;
    }
    i = 0;
    for (auto it = groupByUseCount.begin(); it != groupByUseCount.end(); it++)
    {
      std::cout<<it->second<<" strings were used "<<it->first<<" times."
	       <<std::endl;
      if (++i >= 10) break;
    }

    /*
    groupByUseCount.clear();
    for (auto &kvp : _recentStrings)
      if (kvp.first.length() == 2)
	groupByUseCount[kvp.second]++;
    std::cout<<groupByUseCount.size()<<" 2 byte strings were used at least once."
	     <<std::endl;
    i = 0;
    for (auto it = groupByUseCount.rbegin(); it != groupByUseCount.rend(); it++)
    {
      std::cout<<it->second<<" two byte strings were used "<<it->first<<" times."
	       <<std::endl;
      if (++i >= 10) break;
    }
    i = 0;
    for (auto it = groupByUseCount.begin(); it != groupByUseCount.end(); it++)
    {
      std::cout<<it->second<<" two byte strings were used "<<it->first<<" times."
	       <<std::endl;
      if (++i >= 10) break;
    }
    */
  }
};

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cerr<<*argv<<" filename"<<std::endl;
    return 2;
  }
  if (!strcmp(argv[1],"TEST"))
  {
    TrapezoidStats::interactiveDebug();
    return 1;
  }
  Compressor compressor(argv[1]);
  compressor.processFile();
  //compressor.moreStats();
  return 0;
}

