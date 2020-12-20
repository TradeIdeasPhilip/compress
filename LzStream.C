#include <iostream>
#include <fstream>
#include <algorithm>
#include <set>
#include <map>
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


/* This is a second attempt at a new compression program.  This is based on
 * some of the success of LZMW.C, but fixing some things that bothered me.
 * For more details see:
 * https://docs.google.com/document/d/1NkEfqxJJI6FGtTsM6uFpw38mwqx_cqBkInfEkvL4G9o/edit#
 */

// Production:  g++ -o lz_compress -O4 -ggdb -std=c++0x LzStream.C
// Profiler:  g++ -o lz_compress -O2 -pg -ggdb -std=c++0x LzStream.C

// I didn't notice any speed difference between -O2 and -O4.
// There was a big difference between -O2 and -O0, especially with the
// profiler enabled.

// The profiler was basically useless at -O4.
// The profiler was mostly good with a few strange things at -O2.
// The profiler was a little hard to read at -O0, with all the inlined
// functions showing up.  But that did fix the strange things I saw at -O2.


/* Test code:
cd test_data
foreach file ( * )
echo $file
../lz_compress $file
date
gzip -v <$file |wc
date
gzip -9v <$file |wc
date
end

output:
dump.sql
Compressed size:  Total cost = 6924024 bytes, Min bits per item = 4.962, Max bits per item = 22.384, Min cost = 22.384 bits, Max cost = 871032.860 bits, Non-entropy cost = 8207790, Item count = 5471860, Average bits / item = 10.123
mruList size 4096
Success!  47511635 bytes.
Completed in 21 seconds.
Sat Feb 10 14:48:43 PST 2018
 83.8%
  27164  157985 7705134
Sat Feb 10 14:48:44 PST 2018
 84.8%
  26647  157711 7212413
Sat Feb 10 14:48:49 PST 2018
GWT
Compressed size:  Total cost = 778219 bytes, Min bits per item = 6.525, Max bits per item = 19.197, Min cost = 19.197 bits, Max cost = 42583.799 bits, Non-entropy cost = 901644, Item count = 601096, Average bits / item = 10.357
mruList size 4096
Success!  1931060 bytes.
Completed in 2 seconds.
Sat Feb 10 14:48:51 PST 2018
 65.3%
   2542   14369  669225
Sat Feb 10 14:48:51 PST 2018
 65.5%
   2548   14336  667138
Sat Feb 10 14:48:51 PST 2018
log.1487212530
Compressed size:  Total cost = 586521 bytes, Min bits per item = 4.014, Max bits per item = 18.938, Min cost = 18.938 bits, Max cost = 124775.395 bits, Non-entropy cost = 753362, Item count = 502241, Average bits / item = 9.342
mruList size 4096
Success!  9368661 bytes.
Completed in 2 seconds.
Sat Feb 10 14:48:53 PST 2018
 92.7%
   2609   14845  683946
Sat Feb 10 14:48:53 PST 2018
 92.9%
   2561   14659  665719
Sat Feb 10 14:48:53 PST 2018
log.big
Compressed size:  Total cost = 3739806 bytes, Min bits per item = 3.907, Max bits per item = 21.656, Min cost = 21.656 bits, Max cost = 860504.544 bits, Non-entropy cost = 4956080, Item count = 3304053, Average bits / item = 9.055
mruList size 4096
Success!  81747170 bytes.
Completed in 12 seconds.
Sat Feb 10 14:49:05 PST 2018
 94.0%
  17789  107128 4938967
Sat Feb 10 14:49:06 PST 2018
 94.2%
  17464   99476 4754001
Sat Feb 10 14:49:08 PST 2018
LZMW.C
Compressed size:  Total cost = 6315 bytes, Min bits per item = 7.031, Max bits per item = 12.389, Min cost = 12.389 bits, Max cost = 288.282 bits, Non-entropy cost = 8045, Item count = 5363, Average bits / item = 9.420
mruList size 2937
Success!  17489 bytes.
Completed in 0 seconds.
Sat Feb 10 14:49:08 PST 2018
 68.5%
     19     118    5528
Sat Feb 10 14:49:08 PST 2018
 68.5%
     24     123    5523
Sat Feb 10 14:49:08 PST 2018
mru_list.txt
Compressed size:  Total cost = 5311560 bytes, Min bits per item = 4.982, Max bits per item = 17.360, Min cost = 468.722 bits, Max cost = 716051.656 bits, Non-entropy cost = 6813246, Item count = 4542164, Average bits / item = 9.355
mruList size 4096
Success!  19201608 bytes.
Completed in 13 seconds.
Sat Feb 10 14:49:21 PST 2018
 72.3%
  20496  116411 5314365
Sat Feb 10 14:49:22 PST 2018
 72.5%
  20757  113909 5284558
Sat Feb 10 14:49:34 PST 2018

*/

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

// For simpliciy and performance, use mmap to read the entire file into
// memory.  This implementation is lacking a few things.  We can't handle
// streaming data / pipes.  The file size is limited.  These have nothing to
// do with the compressions algorithm.  You could make another implementation
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
};

char PString::all[256];

std::ostream &operator <<(std::ostream &out, PString const &s)
{
  return out.write(s.begin(), s.length());
}


class MruList
{
public:
  static const int MAX_SIZE = 4096;
private:
  struct Node
  {
  private:
    PString _value;
  public:
    typedef uint16_t Cursor;
    Cursor previous, next;  // In MRU order.
    int useCount;
    PString const &getValue() const { return _value; }
    void setValue(PString const &value) { _value = value; useCount = 0; }
    bool operator ==(Node const &other) const { return _value == other._value; }
    bool operator <(Node const &other) const { return _value < other._value; }
  };
  std::vector< Node > _nodes;  // TODO make this array or the Node struct packed.
  const Node::Cursor END_CURSOR = 0;
  Node &end() { return _nodes[END_CURSOR]; }
  Node const &end() const { return _nodes[END_CURSOR]; }
  Node::Cursor newest() const { return end().next; }
  Node::Cursor oldest() const { return end().previous; }
  void unlink(Node::Cursor cursor)
  {
    Node const &middle = _nodes[cursor];
    Node &previous = _nodes[middle.previous];
    Node &next = _nodes[middle.next];
    next.previous = middle.previous;
    previous.next = middle.next;
  }
  void linkAfter(Node::Cursor newIndex, Node::Cursor afterIndex)
  {
    Node &newNode = _nodes[newIndex];  // To insert
    Node &insertAfter = _nodes[afterIndex];  // Insert after this
    Node &insertBefore = _nodes[insertAfter.next];  // Insert before this
    newNode.previous = insertBefore.previous;
    newNode.next = insertAfter.next;
    insertBefore.previous = newIndex;
    insertAfter.next = newIndex;
  }
  void linkFront(Node::Cursor index)
  {
    linkAfter(index, END_CURSOR);
  }

  // Find where this item is in the MRU view of the data.  0 means this is the
  // last string we've touched.  Higher numbers mean it's been longer since
  // we've touched it.  If an index gets too high, we discard the item
  // completely.  The output of this function will be the input of the
  // entropy encoder.
  int indexOf(Node::Cursor cursor) const
  {
    int result = -1;  // We might return this if the input was invalid.
    while (cursor != END_CURSOR)
    {
      result++;
      cursor = _nodes[cursor].previous;
    }
    return result;
  }

  std::map< PString, Node::Cursor > _alphabetical;

  MruList(MruList const &) =delete;
  void operator =(MruList const &) =delete;

  bool checkInvariants() const
  {
    if (_alphabetical.size() != _nodes.size() - 1)
    { // We expect exactly one more entry in _nodes than _alphabetical.
      // Every entry in _alphabetical should also be in _nodes.  But we have
      // one extra Node which points to the beginning and the end of the list.
      std::cerr<<"_alphabetical.size() == "<<_alphabetical.size()
	       <<", _nodes.size() == "<<_nodes.size()<<std::endl;
      return false;
    }
    // TODO walk through the linked list.
    return true;
  }
  
public:
  MruList();
  ~MruList() { assert(checkInvariants()); }

  void add(PString const &toAdd);
  int findLongest(PString &remainderOfFile);

  size_t size() const { return _alphabetical.size(); }

  // Debug
  PString const &peekNewest() const { return _nodes[newest()].getValue(); }

  template < class Action >
  // Action should be callable with the signature (PString const &value, int useCount).
  void forEachInMruOrder(Action action) const
  {
    for (Node::Cursor cursor = newest();
	 cursor != END_CURSOR;
	 cursor = _nodes[cursor].next)
    {
      Node const &node = _nodes[cursor];
      action(node.getValue(), node.useCount);
    }
  }
};

MruList::MruList()
{
  _nodes.reserve(MAX_SIZE + 1);
  _nodes.resize(257);
  // _nodes[0] is reserved for the special node that points to the beginning
  // and end of the list.
  // _nodes['a'+1] is reserved for the string "a".  Same for all other bytes.
  // The string with only byte 0 is initially at MRU position 0.  I.e. if
  // we see that string well send a 0 to the entropy encoder.  We expect the
  // lower numbers to generally be more common and to compress better.
  for (int i = 0; i < 256; i++)
  {
    Node &first = _nodes[i];
    Node &second = _nodes[i+1];
    first.next = i+1;
    second.previous = i;
    second.setValue((char)i);
    second.useCount = 0;
    _alphabetical[second.getValue()] = i+1;
  }
  _nodes[0].previous = 256;
  _nodes[256].next = 0;
  assert(checkInvariants());
}

void MruList::add(PString const &toAdd)
{
  assert(!toAdd.empty());

  // TODO are we certain this is true?  I think so but I want to double check.
  // When we're compressing the file this is not a big issue.  But if we're
  // sure this never happens then decompressing the file will be much easier
  // and more efficient.
  assert(!_alphabetical.count(toAdd));

  if (_alphabetical.size() >= MAX_SIZE)
  { // The list is full.  Recycle the oldest item.
    Node::Cursor cursor = oldest();
    while (true)
    { // Don't delete any strings of length 1.  Keep walking backwards,
      // toward the most recently used strings, until we find a string
      // with length > 1.
      assert(cursor != END_CURSOR);
      Node &node = _nodes[cursor];
      if (node.getValue().length() > 1)
	break;
      cursor = node.previous;
    }
    Node &node = _nodes[cursor];
    assertTrue(_alphabetical.erase(node.getValue()));
    unlink(cursor);
    node.setValue(toAdd);
    _alphabetical[toAdd] = cursor;
    linkFront(cursor);
  }
  else
  { // Create a new item and insert it into the list.
    Node::Cursor cursor = _nodes.size();
    _nodes.resize(_nodes.size()+1);
    Node &node = _nodes[cursor];
    node.setValue(toAdd);
    _alphabetical[toAdd] = cursor;
    linkFront(cursor);
  }
}

int MruList::findLongest(PString &remainderOfFile)
{
  assert(!remainderOfFile.empty());  // Must be at least one byte.
  /* Imagine this is the contents of our string list.  Notice that it's sorted.
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
   * skip it and keep going backwards.  Next we get to [1] which is the longest
   * prefix, exactly what we were looking for.
   * 
   * Notice that [0] is also a prefix of the file.  We didn't choose it because
   * we found a longer prefix, and that's always our preference.  But we know
   * that [0] will be in the file because that's an invarient of the MruList
   * class.  So we know we'll always find a prefix with a length of at least
   * one byte.  So we know we'll always make progress.
   *
   * Generally this is quite fast.  However, there could be more than one item
   * where we found [2].  In fact, it's possible that we will have to walk 
   * through the entire table looking for the best match.  If you have full
   * control over the file and the string list, that's easy to set up.  I don't
   * know how bad you can make it if you only have control over the file and
   * the string list is built up the normal way. */
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
    if (_nodes[it->second].getValue().isAPrefixOf(remainderOfFile))
      // Found it!  This is the longest prefix.
      break;  
    // Keep looking.  There are some optimizations we could think about here.
  }
  const auto internalLocation = it->second;
  // Return the original index of the matching string.
  const int result = indexOf(internalLocation);
  // Then move this string to the front of the MRU list.
  unlink(internalLocation);
  linkFront(internalLocation);
  // Keep up the statistics.  How often have we used this string?
  _nodes[internalLocation].useCount++;
  // Advance the file pointer.
  remainderOfFile.removeFromFront(_nodes[internalLocation].getValue().length());
  return result;
}


std::ostream *compressedOutput = NULL;


// Each time we find a string in the MRU list that we want to print, we send
// the index of that string to this function.  There might eventually be some
// special values, like -1 for flush, but not yet.
//
// Eventually this will be attached to an entropy encoder.  The first phase
// was specifically designed to output indexes which should compress well with
// an entroy encoder.
//
// For now we're just outputting 2 bytes for each index.  That will be
// sufficient because our table is limited to 2^12 items.  So really we only
// need 1.5 bytes per index.  It wouldn't be hard to actually spit out 3 bytes
// for every two calls to this function.  But there's no point.  You could
// see the results with a calculator.  And we know that would be replaced by
// something better soon.
void fromPhaseOne(int index)
{
  if (!compressedOutput) return;
  assert((index >= 0) && (index <= 0x0ffff));
  (*compressedOutput)<<(char)index<<((char)(index>>8));
  if (!*compressedOutput)
  {
    std::cerr<<strerror(errno)<<" while writing to output"<<std::endl;
    exit(4);
  }
}

class IdealEntropyCost
{
private:
  unsigned int _totalCount;
  std::vector< unsigned int > _counts;
public:
  IdealEntropyCost() : _totalCount(0) { }
  void increment(int index)
  {
    assert(index >= 0);
    if (index >= _counts.size())
      _counts.resize(index + 1);
    _totalCount++;
    assert(_totalCount > 0);
    _counts[index]++;
  }
  void summarize(std::ostream &out) const
  {
    if (_totalCount == 0)
      out<<"No data."<<std::endl;
    else
    {
      size_t index = 0;
      double totalCost = 0;
      double minBitsPerItem = 10000000;
      double maxBitsPerItem = -1;
      double minCost = 1e100;
      double maxCost = -1;
      std::map< int, unsigned int > indexCountByCost;
      for (auto count : _counts)
      {
	if (count == 0)
	  indexCountByCost[INT_MAX]++;
	else
	{
	  const double probability = count / (double)_totalCount;
	  const double bitsPerItem = -std::log2(probability);
	  const double cost = count * bitsPerItem;
	  totalCost += cost;
	  if (bitsPerItem < minBitsPerItem)
	    minBitsPerItem = bitsPerItem;
	  if (bitsPerItem > maxBitsPerItem)
	    maxBitsPerItem = bitsPerItem;
	  if (cost < minCost)
	    minCost = cost;
	  if (cost > maxCost)
	    maxCost = cost;
	  indexCountByCost[(int)std::round(bitsPerItem)]++;
	}	
	index++;
      }
      out<<std::fixed;
      out<<"Total cost = "<<std::setprecision(0)<<std::ceil(totalCost/8)<<" bytes"
	 <<", Min bits per item = "<<std::setprecision(3)<<minBitsPerItem
	 <<", Max bits per item = "<<maxBitsPerItem
	 <<", Min cost = "<<minCost<<" bits"
	 <<", Max cost = "<<maxCost<<" bits"
	 <<", Non-entropy cost = "<<std::setprecision(0)<<std::ceil(_totalCount * std::ceil(std::log2(_counts.size())) / 8)
	 <<", Item count = "<<_totalCount
	 <<", Average bits / item = "<<std::setprecision(3)<<(totalCost/_totalCount)
	 <<std::endl;
      for (auto kvp : indexCountByCost)
      {
	const auto bits = kvp.first;
	const auto indexCount = kvp.second;
	out<<indexCount<<" indices with a cost of ";
	if (bits < INT_MAX)
	  out<<bits<<" bits.";
	else
	  out<<"infinity";
	out<<std::endl;
      }
    }
  }
};

void compress(char const *begin, char const *end)
{
  MruList mruList;
  char const *newEntry = NULL;
  bool startNewEntry = true;
  IdealEntropyCost frequencies;
  PString remaining(begin, end);
  while(!remaining.empty())
  {
    if (startNewEntry)
      newEntry = remaining.begin();
    int index = mruList.findLongest(remaining);
    fromPhaseOne(index);
    //std::cerr<<index<<" -- "<<mruList.peekNewest()<<std::endl;
    frequencies.increment(index);
    if (!startNewEntry)
      mruList.add(PString(newEntry, remaining.begin()));
    startNewEntry = !startNewEntry;
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
  std::cerr<<"Compressed size:  ";
  frequencies.summarize(std::cerr);
  std::cerr<<"mruList size "<<mruList.size()<<std::endl;
  std::cerr<<"Final strings, length × use count: ";
  std::map< size_t, int > countByLength;
  std::map< size_t, int > countByLengthUsed;
  std::map< int, int > countByUseCount;
  std::map< int, PString > exampleByUseCount;
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
  std::cerr<<std::endl;
  for (auto const &kvp : countByLength)
  {
    const auto length = kvp.first;
    const auto count = kvp.second;
    const auto numerator = countByLengthUsed[length];
    std::cerr<<numerator<<" / "<<count<<" strings of length "<<length
	     <<" were used at least once.  "
	     <<(int)std::round((numerator * 100.0)/count)<<'%'<<std::endl;
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

int main(int argc, char **argv)
{
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
  compress(file.begin(), file.end());
  const time_t end_time = time(NULL);
  std::cerr<<"Success!  "<<file.size()<<" bytes."<<std::endl;
  std::cerr<<"Completed in "<<(end_time-start_time)<<" seconds."<<std::endl;
  return 0;
}
