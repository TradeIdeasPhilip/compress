#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <cmath>
#include <climits>
#include <iomanip>

#include "LzBlockShared.h"

/* Recent thoughts and plans, 12/21/2021
 * 1)  The maximum length of the MRU list can be set on a per block basis.  We
 *     aim for 4096 entries, but if that fails we add more.  On encoding we
 *     might have to redo part of the work for a block, but not all of the
 *     work. The first pass creates a list of interesting back references.
 *     The second pass will trim some of those.  And the second pass will
 *     rearranges things in case a useful MRU item was about to fall off the
 *     end.  The rearranging part might need to be redone if we change the
 *     max length, but nothing else.
 * 2)  Should the block header state the largest MRU index used in the block?
 *     Usually that will trim a lot of items, but the probability of those
 *     items was already low, so it might be a small improvement.
 * 3)  The probabilities of the various MRU items should *not* be dynamic.
 *     If we can describe the probabilities up front, in the header, suddenly
 *     the code gets *a lot* faster.  Updating the probabilities *each time*
 *     we write something to the entropy encoder makes things very slow.
 *     Just changing the size of the table, so the probability of any index
 *     above N goes to 0, is still fast.
 * 4)  How does the header describe the probabilities?  One option available
 *     is to say "use the probabilities we measured in the previous block."
 *     That might work pretty well most of the time.
 * 5)  Sometimes you will *not* want to use the previous statistics.  In
 *     particular, in the first block.  We need some way to describe the
 *     probabilities efficiently.  We do not want the header to contain a 32
 *     bit number for each of the 1024ish entries in the MRU.  In the past
 *     we've tried using a series of line segments to estimate a curve like
 *     this.
 * 6)  Usually the first few entries in the MRU table are the biggest and
 *     would gain the most from more precision.  And they often form strange
 *     curves that aren't nearly as smooth as the rest.  Maybe the header
 *     includes an 8 bit number specifying the probability of each of the
 *     first few entries.  This might be done all the time, and we choose
 *     between #4 and #5 for the rest of the entries on a block by block
 *     basis. */

/* This was copied from LzStream.C
 *
 * This fork looks at the idea of using a small amount of look-ahead to decide
 * which strings to save and which to discard.  LzStream.C uses the simple
 * rule that every other time you print a string, you concatenate the last two
 * strings that you printed and add that new string to the front of the list.
 * LZMW.C would consider more possible strings, but would look ahead
 * to the end of the file to know which strings are actually used.  It
 * prints a special instruction to the file so the decompressor will know
 * which strings to save.  LZMW.C gave good compression, but it became
 * unmanageable for large files.  This should be a compromise between the
 * two approaches.  
 *
 * We start by examining the first part of the file.  We see which strings
 * were created and used in this part of the file.  If a string will be used
 * we send a special save instruction to the output.  We leave all the recently
 * added strings in the MRU list, and hope they will be useful for future
 * blocks.  Then we process the next block the same way.
 *
 * Are we losing a lot of compression because we see a string in one block
 * then we see the second occurrence of it in the next block?  So we considered
 * saving it, but we decided against, and it turns out we should have saved it.
 * In general this won't be any worse than LzStream.C.  That was just guessing
 * which strings to use.  Of course, it's possible that we will save a lot
 * fewer strings than before, and without really knowing, it's better to
 * save more strings.  I don't believe that.  It seems like most strings
 * will be reused very soon, or not at all.  So only strings near the very
 * end of a block are likely to be missed.  By having a lot fewer extra
 * strings we can have a tighter grouping in our MRU list, and the entropy
 * encoder will be able to do a better job.
 *
 * The data suggests that we should save every string of length 2 or 3.  These
 * always have over a 50% chance of being used again.  That could be mixed
 * with the look-ahead.  If a string has a length of 2 or 3 or it is used in
 * this block, save it.  If we know we are working on the last block, we don't
 * have to apply this new rule.  For the last block only save items that we
 * know will be used again.
 *
 * This suggests an interesting idea.  The header of each block would include
 * a small integer N.  Every time the encoder builds a new string we compare
 * the length of that string to N.  If the string has N or fewer bytes, we
 * always add it to the MRU list.  Otherwise we use look ahead to decide
 * whether or not to add the string, and we send a Boolean to the output to
 * record our decision.  Presumably we use look ahead to determine what's the
 * best value for N, otherwise we wouldn't bother to save its value in each
 * block and it would just be a constant.
 *
 * A slight variation:  Each block header contains more information about the
 * probability of saving a string based on the string's length.  E.g. 2 or 3,
 * bytes -> 100% chance.  4 -> 40%, 5 -> 30%, >= 6 -> 20%.  Seems like the
 * length of the new string is a useful piece of information that the
 * compressor and the decompressor share.  I don't have that data in front of
 * me at the moment, but I've definitely seen trends similar to what I've
 * described, and that's huge for the entropy encoder.
 *
 * In light of all of the recent changes, do we still limit ourselves to only
 * create and save a new string every OTHER time that we print a string?  Would
 * there be any value to deciding this at run time, each block?
 *
 * LZMW.C also had a delete command.  The last time we used a string
 * we deleted it from the MRU.  We had to send a command to the decompressor
 * because it would have no other way to know.  But we saved so much in the
 * entropy encoder that it more than made up for the extra delete commands.
 * The entropy encoder worked better because we had so many more low numbers
 * to compress.  We might use a similar strategy, but only for the last block.
 * It wouldn't make sense anywhere else. */


/* This is a second attempt at a new compression program.  This is based on
 * some of the success of LZMW.C, but fixing some things that bothered me.
 */

// Production:  g++ -o lz_bcompress -O4 -ggdb -std=c++14 LzBlock.C
// Profiler:  g++ -o lz_bcompress -O2 -pg -ggdb -std=c++14 LzBlock.C
//            gprof ./lz_bcompress gmon.out > analysis.txt



// Value is always computed.  We also call assert(value) if assertions are
// enabled.  Value is discarded either way.  You do not get a warning either
// way.  This is useful when (a) a function has a side effect (b) the function
// returns true on success, and (c) failure seems unlikely, but we still want
// to check sometimes.
template < class T >
void assertTrue(T const &value)
{
  assert(value);
}

template < class T >
void assertFalse(T const &value)
{ 
  assert(!value);
}

int64_t getMicroTime()
{
  timeval inPieces;
  assertFalse(gettimeofday(&inPieces, NULL));
  int64_t result = inPieces.tv_sec;
  result *= 1000000;
  result += inPieces.tv_usec;
  return result;
}

class StopWatch
{
 private:
  int64_t _last;
 public:
  // The clock starts as soon as you create this objet.
  StopWatch() : _last(getMicroTime()) { }

  // Get the number of microseconds since the last time we reset and then
  // immediately reset.
  int64_t getMicroSeconds() 
  { int64_t start = _last; _last = getMicroTime(); return _last - start; }
};


class Profiler
{
private:
  int _n;
  int64_t _microseconds;
public:
  Profiler() : _n(0), _microseconds(0) {}
  void add(int64_t microseconds) { _n++; _microseconds += microseconds; }
  void write(std::ostream &out) const
  {
    out<<_microseconds<<"µs/"<<_n; if (_n) out<<"="<<(_microseconds/_n)<<"µs";
  }
  
  class Update
  {
  private:
    int64_t _start;
    Profiler &_owner;
  public:
    Update(Profiler &owner) : _start(getMicroTime()), _owner(owner) { }
    ~Update() { _owner.add(getMicroTime() - _start); }
  };
};

std::ostream &operator <<(std::ostream &out, Profiler const &profiler)
{
  profiler.write(out);
  return out;
}

// For simplicity and performance, use mmap() to read the entire file into
// memory.  This implementation is lacking a few things.  We can't handle
// streaming data / pipes.  The file size is limited.  These have nothing to
// do with the compression algorithm.  You could make another implementation
// which handles those cases better.
class File
{
private:
  char const *_begin;
  char const *_end;
  std::string _errorMessage;
public:
  File(char const *name);
  ~File();
  File(const File&) =delete;
  void operator=(const File&) =delete;

  char const *begin() const { return _begin; }
  char const *end() const { return _end; } // Standard STL:  Stop before here.
  size_t size() const { return _end - _begin; }
  bool valid() const { return _begin; }
  std::string const &errorMessage() const { return _errorMessage; }
};

File::File(char const *name) : _begin(NULL), _end(NULL)
{
  int handle = open(name, O_RDONLY);
  if (handle < 0)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " open(“";
    _errorMessage += name;
    _errorMessage += "”)";
    return;
  }
  off_t length = lseek(handle, 0, SEEK_END);
  if (length == (off_t)-1)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " lseek(“";
    _errorMessage += name;
    _errorMessage += "”)";
    close(handle);
    return;
  }
  void *address = mmap(NULL, length, PROT_READ, MAP_SHARED, handle, 0);
  if (address == (void *)-1)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " mmap(“";
    _errorMessage += name;
    _errorMessage += "”)";
    close(handle);
    return;
  }
  close(handle);
  int madviseResult = madvise(address, length, MADV_SEQUENTIAL);
  if (madviseResult)
  {
    _errorMessage = strerror(errno);
    _errorMessage += " madvise(MADV_SEQUENTIAL)";
    assertFalse(munmap(address, length));
    return;
  }
  _begin = (char const *)address;
  _end = _begin + length;
}

File::~File()
{
  if (valid())
    assertFalse(munmap((void *)begin(), size()));
}

// A simple version of a string.
//
// This implementation is a POD type.  That was more important when we were
// looking at different data structures for holding the strings.  Currently
// that doesn't seem as important.
//
// PString is a POD type because someone else is responsible for memory
// management.  Typically we're talking about strings that we've read from the
// input file or one byte strings that are created when we first start the
// program.
//
// Some methods, like join() and next(), will only treat two PString objects
// identically if they are pointing to the exact same memory.  Most methods
// treat two objects as the same as long as they contain identical data.
// In particular, operator ==(), operator <() and std::hash() all focus on the
// contents of the strings, not where they are in memory.  So you can use
// a PString as the key in an STL container, just like you would with a
// std::string.
class PString
{
private:
  static char all[256];
  char const *_begin;
  size_t _length;
public:
  PString() : _begin(NULL), _length(0) { }
  PString(char ch) : _begin(&all[(unsigned char)ch]), _length(1)
  { all[(unsigned char)ch] = ch; }
  PString(char const *begin, char const *end) :
    _begin(begin), _length(end - begin) { assert(end >= begin); }
  PString(char const *begin, size_t length) : _begin(begin), _length(length)
  { assert(_begin <= (_begin + _length)); }
  bool operator <(PString const &other) const
  {
    const int direction =
      memcmp(_begin, other._begin, std::min(_length, other._length));
    if (direction < 0)
      return true;
    if (direction > 0)
      return false;
    return _length < other._length;
  }
  bool operator ==(PString const &other) const
  {
    if (_length != other._length)
      return false;
    return !memcmp(_begin, other._begin, _length);
  }
  bool empty() const { return _length == 0; }
  size_t length() const { return _length; }
  char const *begin() const { return _begin; }

  // "ABCDE".removeFromFront(0) --> "ABCDE"
  // "ABCDE".removeFromFront(3) --> "DE"
  // "ABCDE".removeFromFront(5) --> ""
  // "ABCDE".removeFromFront(7) --> undefined
  void removeFromFront(size_t toRemove)
  { // If we make this an assertion now, it's easy enough to change it later.
    // We could say that removing too much makes the sting empty.  Or that it
    // throws an exception.
    assert(toRemove <= _length);
    _begin += toRemove;
    _length -= toRemove;
  }

  void removeFromEnd(size_t toRemove)
  {
    assert(toRemove <= _length);
    _length -= toRemove;
  }

  // "A".isAPrefixOf("ABC") --> true
  // "ABC".isAPrefixOf("ABC") --> true
  // "ABCD".isAPrefixOf("ABC") --> false
  // "A".isAPrefixOf("aABC") --> false
  // "".isAPrefixOf(anything) --> true
  // anythingButEmpty.isAPrefixOf("") --> false;
  bool isAPrefixOf(PString const &longer) const
  {
    if (_length > longer._length) return false;
    return !memcmp(_begin, longer._begin, _length);
  }

  PString join(PString const &second) const
  {
    assert(_begin + _length == second._begin);
    return PString(_begin, _length + second._length);
  }

  PString next(int length) const
  {
    assert(_begin + _length + length >= _begin);
    return PString(_begin + _length, length);
  }
};

char PString::all[256];

namespace std
{
  template<> struct hash< PString >
  {
    typedef PString argument_type;
    typedef std::size_t result_type;
    result_type operator()(argument_type const& string) const noexcept
    {
      return std::_Hash_impl::hash(string.begin(), string.length());
    }
  };
}


std::ostream &operator <<(std::ostream &out, PString const &s)
{
  return out.write(s.begin(), s.length());
}


std::ostream *compressedOutput = NULL;


typedef uint16_t WriteInfo;

struct Profilers
{
  Profiler possibleMru_findLongest;
  Profiler possibleMru_addString;
  Profiler possibleMru_findStrings;
  Profiler finalOrderMru_find;
  Profiler finalOrderMru_add;
  Profiler finalOrderMru_reportStrings;
} profilers;
// Profiler::Update pu(profilers.finalOrderMru_find);

std::ostream &operator <<(std::ostream &out, Profilers const &p)
{
  const auto dump = [&out] (char const *name, Profiler const &p)
  { out<<"«"<<name<<" "<<p<<"» "; };
  // ORNATE LEFT PARENTHESIS 64830 U+fd3e
  // ORNATE RIGHT PARENTHESIS 64831 U+fd3f
  // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK 171 U+ab
  // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK 187 U+bb
  dump("PossibleMru::findLongest", p.possibleMru_findLongest);
  dump("PossibleMru::addString", p.possibleMru_addString);
  dump("PossibleMru::findStrings", p.possibleMru_findStrings);
  dump("FinalOrderMru::find", p.finalOrderMru_find);
  dump("FinalOrderMru::add", p.finalOrderMru_add);
  dump("FinalOrderMru::reportStrings", p.finalOrderMru_reportStrings);
}

// Collect strings that we might want to reuse.
class PossibleMru
{
private:
  std::set< PString > _alphabetical;

  PString findLongest(PString &remainderOfFile)
  {
    Profiler::Update pu(profilers.possibleMru_findLongest);
    assert(!remainderOfFile.empty());  // Must be at least one byte.
    /* Imagine this is the contents of our string list.  Notice it's sorted.
     * a
     * b           [0]
     * bbcc@
     * bbcca       [1]
     * bbcca@      [2]
     * bbccaadd    [3]
     * bbccaq
     * 
     * The file contains bbccaadd and no more.  A normal find() operation on a
     * sorted list or a binary search tree will quickly tell you that [3] is a
     * perfect match.
     *
     * What if [3] wasn't in the table?  Then we work backwards.  [2] is the
     * first thing we'll inspect.  That is not a prefix of of the file, so we
     * skip it and keep going backwards.  Next we get to [1] which is the
     * longest prefix, exactly what we were looking for.
     * 
     * Notice that [0] is also a prefix of the file.  We didn't choose it
     * because we found a longer prefix, and that's always our preference.  But
     * we know that [0] will be in the file because that's an invariant of the
     * MruList class.  So we know we'll always find a prefix with a length of
     * at least one byte.  So we know we'll always make progress.
     *
     * Generally this is quite fast.  However, there could be more than one
     * item where we found [2].  In fact, it's possible that we will have to
     * walk through the entire table looking for the best match.  If you have
     * full control over the file and the string list, that's easy to set up.
     * I don't know how bad you can make it if you only have control over the
     * file and the string list is built up the normal way.
     *
     * Proposal:  After we see that there is no exact match we'll still go back
     * one item in the table and look at [2].  If [2] were valid, then return
     * it as the match.  But it's not, so look for the longest common prefix
     * between what we were looking for (bbccaadd) and what we found (bbcca@)
     * which is bbcca.  Repeat the process from the beginning, but looking for
     * this new, shorter string, instead.  In this case we'd still jump to [1]
     * next, but if there were more items between [1] and [2] this trick would
     * have saved us time.  In this case, however, it might have cost us time,
     * O(ln(n)) to do a new lookup vs O(1) (I think!) to step back, it--. */
    // upper_bound(x) gives you the smallest value that's > x.
    // lower_bound(x) gives you the smallest values that's >= x.
    auto it = _alphabetical.upper_bound(remainderOfFile);
    // We really want the largest value that's <= x.  So we start from the
    // upper_bound() and go back one.  upper_bound() might be the end of the
    // list, and that's not a special case.
    while (true)
    {
      it--;
      // The table should be set up so we never get all the way past the
      // beginning.  That's a precondition.
      assert(it != _alphabetical.end());
      if (it->isAPrefixOf(remainderOfFile))
	// Found it!  This is the longest prefix.
	break;  
      // Keep looking.  There are some optimizations we could think about here.
    }
    // Advance the file pointer.
    remainderOfFile.removeFromFront(it->length());
    return *it;
  }
  
public:
  // If the string is already in the list, return false and do nothing else.
  // Otherwise add the string to the list and return true.
  bool addString(PString const &string)
  {
    Profiler::Update pu(profilers.possibleMru_addString);
    static const int MAX_LENGTH = 30000;
    if (string.length() > MAX_LENGTH)
      // Somewhat artificial.  Mostly so we can export list of lengths from
      // findStrings() as 15 bit integers.  (See WriteInfo for more details on
      // how it gets stored.)  PString can handle much larger strings; it was
      // convenient for the entire input file to be stored in on PString, but
      // this list will be smaller, allowing us to read and process larger
      // blocks at once.  It's tempting to make this one byte, as most strings
      // I see would fit in there.
      return false;
    return _alphabetical.insert(string).second;
  }

  void findStrings(PString &remaining,
		   std::unordered_map< PString, int > &recentUses,
		   std::vector< WriteInfo > &toWrite)
  {
    Profiler::Update pu(profilers.possibleMru_findStrings);
    char const *lastPrint = NULL;
    // TODO 4096+2048 is just a rough estimate which works with my test
    // data.  We need a better way of dealing with this.  Anything bigger than
    // 4096 is not guaranteed to work.  However, making this number too small
    // will rob us of some potential compression in most files.  We probably
    // need some smarter test, possibly trying a higher number, then trying
    // again if there's a failure later in the algorithm.
    // TODO 4096 should not be hard coded, even as part of a heuristic.  Notice
    // FinalOrderMru::_maxSize.  This value can be changed.
    while((!remaining.empty()) && (recentUses.size() < 4096+2048))
    {
      char const *const newEntry = lastPrint;
      lastPrint = remaining.begin();
      const PString found = findLongest(remaining);
      if (found.length() > 1)
	recentUses[found]++;
      if (newEntry)
	// Tempting to assert: this will return false (the string was already
	// in the table so nothing changed) only if the last three strings we
	// used were all identical.  
	addString(PString(newEntry, remaining.begin()));
      //if (newString)
      //std::cerr<<"⟨"<<_alphabetical.size()<<','<<PString(newEntry, remaining.begin()).length()<<"⟩";
      //if (!newString)
      //	if (newEntry)
      //	  std::cout<<"⟨"<<PString(newEntry, remaining.begin())<<"⟩"<<std::endl;
      //else
      //  std::cout<<"⟨NULL⟩"<<std::endl;
      toWrite.emplace_back(found.length());
    }
    
    /*
    int count = 0;
    for (PString const &string : _alphabetical)
    {
      count++;
      if (count <= 256) continue;
      if (count > 512) break;
      std::cerr<<string<<", ";
    }
    */
  }

  size_t size() const { return _alphabetical.size(); }
};

class FinalOrderMru
{
private:
  MruBase< PString > _strings;
  SymbolCounter _indexCounter;
  WriteStats _writeStats;
  BoolCounter _deleteStats;

  // Move the given item to the front of the list.  Return the index of the
  // item before we moved it.  0 is the front of the list.  Therefore, if you
  // call this twice in a row on the same input, the second call will always
  // return 0.  Precondition:  The input must already be in the list.
  int find(PString const &toFind)
  {
    Profiler::Update pu(profilers.finalOrderMru_find);
    const size_t result = _strings.findAndPromote(toFind);
    assert(result != MruBase< PString >::NOT_FOUND);
    return result;
  }

  void add(PString const &toAdd)
  {
    Profiler::Update pu(profilers.finalOrderMru_add);
    _strings.addToFront(toAdd);
    // TODO This would be a good place to check for end of block.
  }

public:
  // TODO not max size, but desired max size.
  FinalOrderMru(int maxSize = 4096) : _strings(maxSize)
  {
    for (int i = 255; i >= 0; i--)
      _strings.addToFront(PString((char)i));
  }

  void reportStrings(char const *start, std::vector< WriteInfo > &toWrite,
		     std::unordered_map< PString, int > &recentUses,
		     std::vector< RansRange > &toEntropyEncoder)
  {
    std::map< int, int > saveYesCount;
    std::map< int, int > saveAllCount;

    int indexCount = 0;
    double indexCost = 0.0;
    int deleteCount = 0;
    double deleteCost = 0.0;
    int writeCount = 0;
    double writeCost = 0.0;

    Profiler::Update pu(profilers.finalOrderMru_reportStrings);
    const auto saveIndex = [&](int index){
      const auto range = _indexCounter.getRange(index, _strings.size());
      toEntropyEncoder.push_back(range);
      _indexCounter.increment(index);
      indexCount++;
      indexCost += range.idealCost();
    };
    const auto saveDelete = [&](bool value) {
      const auto range = _deleteStats.getRange(value);
      toEntropyEncoder.push_back(range);
      _deleteStats.increment(value);
      deleteCount++;
      deleteCost += range.idealCost();
    };
    const auto saveWrite = [&](int length, bool value){
      const auto range = _writeStats.getRange(length, value);
      toEntropyEncoder.push_back(range);
      _writeStats.increment(length, value);
      saveAllCount[length]++;
      if (value)
      {
	saveYesCount[length]++;
      }
      writeCount++;
      writeCost += range.idealCost();
    };
    char const *savedOlder = NULL;
    char const *savedNewer = NULL;
    for (const WriteInfo length : toWrite)
    {
      savedOlder = savedNewer;
      savedNewer = start;
      start += length;
      const PString string(savedNewer, length);
      saveIndex(find(string));
      bool recentDelete = false;
      if (length > 1)
      {
	const auto it = recentUses.find(string);
	assert(it != recentUses.end());
	//std::cout<<"recentUses[“"<<string<<"”]:  "<<it->second<<" --> "
	// 	 <<(it->second-1)<<std::endl;
	it->second--;
	recentDelete = !it->second;
	saveDelete(recentDelete);
	if (recentDelete)
	{ // That thing we just grabbed, we will never need it again.  Delete
	  // it from the MRU while it's still at index 0 and easy to find.
	  recentUses.erase(it);
	  _strings.deleteFront();
	}
      }
      if (savedOlder)
      {
	PString toSave(savedOlder, start);
	if (!_strings.isRecentDuplicate(toSave, recentDelete))
	  if (recentUses.count(toSave))
	  { // The newly created string will be used again.  Save it.
	    add(toSave);
	    saveWrite(toSave.length(), true);
	  }
	  else
	    // We could have created this string, but no one used it, so we
	    // skip it.
	    saveWrite(string.length(), false);
      }
    }

    /*
    std::cerr<<"length\tyes\tall\t%"<<std::endl;
    for (auto const &kvp : saveAllCount)
    {
      const int length = kvp.first;
      const int allCount = kvp.second;
      const int yesCount = saveYesCount[length];
      std::cerr<<length<<'\t'<<yesCount<<'\t'<<allCount<<'\t'
	       <<(yesCount * 100.0 / allCount)<<std::endl;
    }
    */

    class Counter
    {
    private:
      int _all;
      int _yes;
    public:
      Counter(): _all(0), _yes(0) { }
      void update(int all, int yes) { _all += all; _yes += yes; }
      double getCostInBits(std::ostream *dump = NULL) const
      {
	const double yesRatio = _yes / (double)_all;
	const double yesCost = _yes * pCostInBits(yesRatio);
	const int no = _all - _yes;
	const double noRatio = no / (double)_all;
	const double noCost = no * pCostInBits(noRatio);
	const double totalCost = yesCost + noCost;
	if (dump)
	{
	  (*dump)<<"  Yes count: "<<_yes<<", "<<(yesRatio*100)<<"%, bits: "
	    	 <<yesCost<<std::endl
	    	 <<"  No count: "<<no<<", "<<(noRatio*100)<<"%, bits: "
                 <<noCost<<std::endl
	    	 <<"  Total bits: "<<totalCost<<", bytes: "<<(totalCost/8)
		 <<std::endl;
	}
	return totalCost;
      }
    };
    Counter counter2;
    Counter counter3;
    Counter counterOthers;
    for (auto const &kvp : saveAllCount)
    {
      const int length = kvp.first;
      if (length == 1)
      {
	continue;
      }
      const int all = kvp.second;
      const int yes = saveYesCount[length];
      switch (length)
      {
      case 2:
	counter2.update(all, yes);
	break;
      case 3:
	counter3.update(all, yes);
	break;
      default:
	counterOthers.update(all, yes);
	break;
      }
    }
    double costInBits = 0.0;
    std::cerr<<">>> Length = 2"<<std::endl;
    costInBits += counter2.getCostInBits(&std::cerr);
    std::cerr<<">>> Length = 3"<<std::endl;
    costInBits += counter3.getCostInBits(&std::cerr);
    std::cerr<<">>> Length > 3"<<std::endl;
    costInBits += counterOthers.getCostInBits(&std::cerr);
    std::cerr<<">>> Total cost in bits: "<<costInBits
	     <<", in bytes: "<<(costInBits/8)<<std::endl;
    
    std::cerr<<"count\tbytes\tbits/\treason"<<std::endl
	     <<indexCount<<'\t'<<(indexCost/8)<<'\t'<<(indexCost/indexCount)<<'\t'<<"Index"<<std::endl
	     <<deleteCount<<'\t'<<(deleteCost/8)<<'\t'<<(deleteCost/deleteCount)<<'\t'<<"Delete"<<std::endl
	     <<writeCount<<'\t'<<(writeCost/8)<<'\t'<<(writeCost/writeCount)<<'\t'<<"Write"<<std::endl;

  }
  
  void copyTo(PossibleMru &possibleMru)
  { 
    for (auto it = _strings.getAll().begin();
	 it != _strings.visibleEnd(); it++)
      possibleMru.addString(*it);
  }

  void restoreAllFromRecycleBin() { _strings.restoreAllFromRecycleBin(); }
};

void compress(char const *begin, char const *end)
{
  FinalOrderMru finalOrderMru;
  PString remaining(begin, end);
  while(!remaining.empty())
  {
    StopWatch stopWatch;
    char const *const startOfInput = remaining.begin();
    PossibleMru possibleMru;
    finalOrderMru.restoreAllFromRecycleBin();
    finalOrderMru.copyTo(possibleMru);
    std::unordered_map< PString, int > recentUses;
    std::vector< WriteInfo > stringsToWrite;
    const auto preFindStringsTime = stopWatch.getMicroSeconds();
    possibleMru.findStrings(remaining, recentUses, stringsToWrite);
    std::cerr<<recentUses.size()<<" of "<<possibleMru.size()
	     <<" new strings used in "<<stopWatch.getMicroSeconds()<<"µs."
	     <<std::endl;
    std::vector< RansRange > toEntropyEncoder;
    finalOrderMru.reportStrings(startOfInput, stringsToWrite,
				recentUses, toEntropyEncoder);
    std::cerr<<"finalOrderMru.reportStrings() took "
	     <<stopWatch.getMicroSeconds()<<"µs."<<std::endl;
    //std::cerr<<"recentUses:  ";
    //for (auto const &kvp : recentUses)
    //  std::cerr<<"“"<<kvp.first<<"” == "<<kvp.second<<", ";
    //std::cerr<<std::endl;
    assert(recentUses.empty());
    if (compressedOutput)
    {
      Rans64State r;
      Rans64EncInit(&r);
      std::vector< uint32_t > compressed(128);
      uint32_t *p = &*compressed.end();
      for (auto it = toEntropyEncoder.rbegin();
	   it != toEntropyEncoder.rend(); it++)
      {
	auto const free = p - &compressed[0];
	if (free < 5)
	{
	  const auto newOffset = free + compressed.size();
	  compressed.insert(compressed.begin(), compressed.size(), 0);
	  p = &compressed[newOffset];
	}
	it->put(&r, &p);
      }
      Rans64EncFlush(&r, &p);
      // TODO add the number of items in this block, and maybe the number of
      // uncompressed bytes.
      auto const free = p - &compressed[0];
      compressedOutput->write((const char *)p, 4 * (compressed.size() - free));
      if (!*compressedOutput)
      {
	std::cerr<<strerror(errno)<<" while writing to output"<<std::endl;
	exit(4);
      }
    }
    const auto afterCompressTime = stopWatch.getMicroSeconds();
    std::cerr<<"Overhead:  "<<preFindStringsTime<<" + "<<afterCompressTime
	     <<" = "<<(preFindStringsTime+afterCompressTime)<<"µs"<<std::endl;
  }
  /*
  int total = 0;
  for (int i = 0; i < MruList::MAX_SIZE; i++)
  {
    int f = frequencies[i];
    if (f != 0)
    {
      total += f;
      std::cerr<<i<<" "<<f<<std::endl;
    }
  }
  std::cerr<<"TOTAL "<<total<<std::endl;
  */
  //std::cerr<<"Final strings, length × use count: ";
  std::map< size_t, int > countByLength;
  std::map< size_t, int > countByLengthUsed;
  std::map< int, int > countByUseCount;
  std::map< int, PString > exampleByUseCount;
  // TODO We should still have some version of this data.
  /*
  mruList.forEachInMruOrder([&countByLength, &countByLengthUsed,
			     &countByUseCount, &exampleByUseCount]
			    (PString const &value, int useCount)
			    {
			      std::cerr<<' '<<value.length()<<"×"<<useCount;
			      // Exactly 12 strings had a length of exactly 3 bytes:
			      // countByLength[3] = 12;
			      countByLength[value.length()]++;
			      if (useCount)
			      	// Exactly 9 strings had a length of exactly 3 bytes and were each used at least once:
				// countByLengthUsed[3] = 9
				countByLengthUsed[value.length()]++;
			      // Exactly 11 strings were used exactly 5 times each:
			      // countByUseCount[5] = 11;
			      countByUseCount[useCount]++;
			      exampleByUseCount[useCount] = value;

			    });
  */
  std::cerr<<std::endl;
  int runningCount = 0;
  int runningNumerator = 0;
  for (auto const &kvp : countByLength)
  {
    const auto length = kvp.first;
    const auto count = kvp.second;
    const auto numerator = countByLengthUsed[length];
    if (length > 1)
    { // Keep track of how many strings of length N or less were created and
      // how many were used.  Skip the strings of length 1 because those were
      // not created from the data in the file.  Specifically I'm thinking
      // about the cost of writing a command to the output file every time
      // the compressor wants the decompressor to add a new string to the
      // table.  (Like we did in LZMW.C.)  But, to make things smaller, we
      // might not want to send all of these commands explicitly.  Instead,
      // at the beginning of each block we'd say something like "only create
      // a string when explicitly told to," or "Create a string any time
      // the length of that string would be exactly 2, also any time you get
      // an explicit command," or "Create a string any time the length of that
      // string would be 4 or less, also on an explicit command."  Basically
      // a small number at the beginning of the block would say the size of
      // the largest string that should be automatically created.
      //
      // What if 90% of strings of length 2 are actually used, and we
      // automatically add them all?  What about the 10% that are never used?
      // Do we ignore them?  (No worse than now, when we create every string
      // automatically.)  Maybe the command is inverted.  For strings of length
      // 2 we only send the the command the 10% of the time when we don't want
      // to create the string.  Might help, we should try it both ways.
      runningCount += count;
      runningNumerator += numerator;
    }
    std::cerr<<numerator<<" / "<<count<<" strings of length "<<length
	     <<" were used at least once.  "
	     <<(int)std::round((numerator * 100.0)/count)<<'%';
    if (runningCount)
    {
      std::cerr<<", "<<runningNumerator<<" / "<<runningCount<<" = "
	       <<(int)std::round((runningNumerator * 100.0)/runningCount)<<'%';
    }
    std::cerr<<std::endl;
    // My various test cases gave similar results.
    // 80% - 100% of the strings of length 2 were used at least once.
    // Around 75% of the strings of length 3 were used at least once.
    // Around 50% of less of the strings of length 4 were used at least once.
    // For the most part no other strings were used more than 50% of the time.
    // As the length got higher the % used quickly got close to 0.
  }
  for (auto const &kvp : countByUseCount)
  {
    const auto useCount = kvp.first;
    const auto count = kvp.second;
    std::cerr<<count<<" strings were used "<<useCount<<" times each.";
    if (count == 1)
      std::cerr<<"  “"<<exampleByUseCount[useCount]<<"”";
    std::cerr<<std::endl;
    // In my test cases 50% - 60% of the strings were never used.
  }
}

void testPString()
{
  char const *base = "ABCabcABCx";
  PString p1(base, 3);
  PString p2(base+3, 3);
  PString p3(base+6, 3);
  std::cerr<<"p1 < p1 --> "<<((p1<p1)?"true":"false")<<std::endl;
  std::cerr<<"p1 == p1 --> "<<((p1==p1)?"true":"false")<<std::endl;
  std::cerr<<"p1 < p2 --> "<<((p1<p2)?"true":"false")<<std::endl;
  std::cerr<<"p1 == p2 --> "<<((p1==p2)?"true":"false")<<std::endl;
  std::cerr<<"p1 < p3 --> "<<((p1<p3)?"true":"false")<<std::endl;
  std::cerr<<"p1 == p3 --> "<<((p1==p3)?"true":"false")<<std::endl;

  std::cerr<<"p2 < p1 --> "<<((p2<p1)?"true":"false")<<std::endl;
  std::cerr<<"p2 == p1 --> "<<((p2==p1)?"true":"false")<<std::endl;
  std::cerr<<"p2 < p2 --> "<<((p2<p2)?"true":"false")<<std::endl;
  std::cerr<<"p2 == p2 --> "<<((p2==p2)?"true":"false")<<std::endl;
  std::cerr<<"p2 < p3 --> "<<((p2<p3)?"true":"false")<<std::endl;
  std::cerr<<"p2 == p3 --> "<<((p2==p3)?"true":"false")<<std::endl;

  std::cerr<<"p3 < p1 --> "<<((p3<p1)?"true":"false")<<std::endl;
  std::cerr<<"p3 == p1 --> "<<((p3==p1)?"true":"false")<<std::endl;
  std::cerr<<"p3 < p2 --> "<<((p3<p2)?"true":"false")<<std::endl;
  std::cerr<<"p3 == p2 --> "<<((p3==p2)?"true":"false")<<std::endl;
  std::cerr<<"p3 < p3 --> "<<((p3<p3)?"true":"false")<<std::endl;
  std::cerr<<"p3 == p3 --> "<<((p3==p3)?"true":"false")<<std::endl;

  std::cerr<<"hash(p1) --> "<<std::hash<PString>()(p1)<<std::endl;
  std::cerr<<"hash(p2) --> "<<std::hash<PString>()(p2)<<std::endl;
  std::cerr<<"hash(p3) --> "<<std::hash<PString>()(p3)<<std::endl;

  std::set< PString > set;
  set.insert(p1);
  set.insert(p2);
  set.insert(p3);
  set.insert(p1);
  set.insert(p2);
  set.insert(p3);
  std::cerr<<"set.size() --> "<<set.size()<<std::endl;
}

int main(int argc, char **argv)
{
  //testPString();return 0;
  if ((argc < 2) || (argc > 3))
  {
    std::cerr<<"Syntax:  "<<argv[0]<<" input_filename [output_filename]"
	     <<std::endl;
    return 1;
  }
  File file(argv[1]);
  if (!file.valid())
  {
    std::cerr<<file.errorMessage()<<std::endl;
    return 2;
  }
  // Use a normal variable, not dynamic memory, so the normal C++ shutdown
  // sequence will close this file, if required.
  std::ofstream outputFile;  
  if (argc == 3)
  {
    if (!strcmp(argv[2], "-"))
      // A single - for the file name means to write to the standard output.
      compressedOutput = &std::cout;
    else
    {
      outputFile.open(argv[2], std::ofstream::binary | std::ofstream::trunc);
      if (!outputFile)
      {
	std::cerr<<strerror(errno)<<" trying to open "<<argv[2]<<std::endl;
	return 3;
      }
      compressedOutput = &outputFile;
    }
  }
  const time_t start_time = time(NULL);
  std::cerr<<"Read  "<<file.size()<<" bytes of input."<<std::endl;
  compress(file.begin(), file.end());
  const time_t end_time = time(NULL);
  std::cerr<<"Success!"<<std::endl;
  std::cerr<<"Completed in "<<(end_time-start_time)<<" seconds."<<std::endl;
  std::cerr<<profilers<<std::endl;
  return 0;
}
