#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include "File.h"
#include "RansHelper.h"


// g++ -o hash-down -O4 -ggdb -std=c++0x -Wall -lexplain HashDown.C File.C

// New idea:  Use a hash table to organize the old data.  So we don't
// have to wade through a lot of historical data.  A hash will take us
// directly to the data we need.  Should be much faster than Eight.C.
// And maybe loose some compression, but I'm thinking not much.
//
// This is another idea for compression.  This is similar to Eight.C.
// We look at one character at a time.  And we look at limited context,
// defaulting to the 8 previous bytes.
//
// The difference is that we don't look through a large buffer for each
// byte we want to encode.  Instead there's a table keeping all the statistics.
// Each time we process a byte we add to to the table, and we're done with
// it.
//
// We still look at matches of different sizes.  We keep separate statistics
// for each match size.  And we weight them the same as before:  Everything
// we know about a match of length n gets a total weight of 2^n.
//
// We save every new byte in the table.  That might push old values out of
// the table.  We limit the number of entries in the table.  We use a hash
// of the context to decide which keys to group together.  Each hash bucket is
// limited to a small number of values, defaulting to 3.  After that limit
// is reached, the oldest entry in this bucket is thrown out.
//
// We will be throwing out some data.  We will always favor newer data over
// older data.  We will sometimes group unrelated items.  That makes
// everything fast and easy.  And I don't think it will hurt the result much.
//
// I think this is promising because of the good results we had with the
// "Mega" statistics in Analyze3.C.  We had surprisingly good results saving
// similar data.
//
// The goal is to look at the statistics and predict the next byte, just
// like Eight.C, so we can send some good statistics to the rANS encoder.

// Idea for the bytes that we can't find in our new HashedHistory.
// Start with a basic SymbolCounter to say how common each byte is in the
// input file so far.
// But then look at the SymbolCounter object.  We already have one of these
// because that was the object that said the current character was not
// available in the HashedHistory.
// Every byte that is available in HashedHistory gets removed from the
// symbol counter, its probability is set to 0.
// This simple combination should make good predictions.

class HashedHistory
{
private:
  const int _bytesOfHistory;
  const int _hashModulus;
  const int _entriesPerHash;
  const int _sizePerEntry;
  const int _sizePerHash;
  std::string _body;
  static size_t hash(char const *begin, char const *end)
  { // How expensive is this?  We could do a hash where each new byte that
    // we add to the left does one small operation and doesn't start fresh.
    // Then, if we're doing a hash of N bytes we get the hash for all the
    // smaller strings for free.
    //
    // TODO Change the hash so that a 1 byte string will always return that
    // byte.  So when we look at one byte strings, we always get that byte
    // back as the hash.  Otherwise our table with one byte of context
    // looks terrible.
    // OR There is a lot of wasted space it the table with one byte of
    // contect.  And there always will be for text files.  Maybe we take
    // the table of one byte context, and give it module of a prime near
    // 128.  Then raise the max number of entries per hash.  So we could
    // request the same amount of data, but hopefully a lot more of it will
    // be in use.  So we'd exect better compression at the cost of more CPU
    // time and more complicated code.  Intriguing.  This is worth a try
    // just see!
    return std::_Hash_impl::hash(begin, end - begin);
  }
  size_t hash(char const *end)
  {
    return hash(end - _bytesOfHistory, end);
  }
  char *startOfHash(size_t hash)
  {
    const size_t index = hash % _hashModulus;
    return &_body[index * _sizePerHash];
  }
						    
public:
  int bytesOfHistory() const { return _bytesOfHistory; }
  HashedHistory(int bytesOfHistory, int hashModulus, int entriesPerHash = 3) :
    _bytesOfHistory(bytesOfHistory),
    _hashModulus(hashModulus),
    _entriesPerHash(entriesPerHash),
    _sizePerEntry(bytesOfHistory + 1),
    _sizePerHash(_sizePerEntry * entriesPerHash + 1),
    _body(_sizePerHash * hashModulus, '\x00')
  { }
  void add(char const *newChar)
  {
    auto const hashCode = hash(newChar);
    auto const infoForHash = startOfHash(hashCode);
    char &entryCountForHash = *infoForHash;
    const int entryIndex = entryCountForHash % _entriesPerHash;
    memcpy(infoForHash + 1 + entryIndex * _sizePerEntry,
	   newChar - _bytesOfHistory,
	   _sizePerEntry);
    entryCountForHash++;
    if (entryCountForHash == 2 * _entriesPerHash)
    {
      entryCountForHash = _entriesPerHash;
    }
  }
  template< typename Callback >
  void findAll(char const *end, Callback callback)
  {
    auto const hashCode = hash(end);
    auto const infoForHash = startOfHash(hashCode);
    const int entryCountForHash = *infoForHash;
    const int endIndex = std::min<int>(entryCountForHash, _entriesPerHash);
    char const *const start = end - _bytesOfHistory;
    for (int i = 0; i < endIndex; i++)
    {
      char const *const entryStart = infoForHash + 1 + _sizePerHash * i;
      const int different = memcmp(start, entryStart, _bytesOfHistory);
      if (!different)
      {
	const char usedPreviously = entryStart[_bytesOfHistory];
	callback(usedPreviously);
      }
    }
  }
  void detailedDump(std::ostream &out)
  {
    out<<"=========="<<std::endl;
    std::map<int, int> bucketsWithThisEntryCount;
    for (int h = 0; h < _hashModulus; h++)
    {
      char const *const hashStart = &_body[h * _sizePerHash];
      const int entryCountForHash = *hashStart;
      const int endIndex = std::min<int>(entryCountForHash, _entriesPerHash);
      bucketsWithThisEntryCount[endIndex]++;
      out<<h<<": [";
      for (int entry = 0; entry < endIndex; entry++)
      {
	if (entry)
	{
	  out<<", ";
	}
	out<<"'";
	out.write(hashStart + 1 + entry * _sizePerEntry, _sizePerEntry);
	out<<"'";
      }
      out<<"]"<<std::endl;
    }
    for (auto const &kvp : bucketsWithThisEntryCount)
    {
      auto const entryCount = kvp.first;
      auto const numberOfBuckets = kvp.second;
      out<<numberOfBuckets<<" bucket"<<((numberOfBuckets==1)?"":"s")
	 <<" with "<<entryCount<<((entryCount==1)?" entry.":" entries.")
	 <<std::endl;
    }
  }
  ~HashedHistory()
  {
    detailedDump(std::cout);
  }

};

typedef std::map<char, int> CharCounter;

class AllHashedHistory
{
private:
  std::vector< HashedHistory > _data;
public:
  AllHashedHistory()
  {
    _data.reserve(8);
    _data.emplace_back(1, 256);
    _data.emplace_back(2, 257);
    _data.emplace_back(3, 263);
    _data.emplace_back(4, 269);
    _data.emplace_back(5, 271);
    _data.emplace_back(6, 277);
    _data.emplace_back(7, 281);
    _data.emplace_back(8, 283);
  }
  void add(File const &file, int index)
  {
    for (HashedHistory &h : _data)
    {
      if (index > h.bytesOfHistory())
      {
	h.add(file.begin() + index);
      }
    }
  }
  void getStats(File const &file, int index,
		CharCounter &charCounter, int &denominator)
  {
    charCounter.clear();
    denominator = 0;
    int weight = 1;
    for (HashedHistory &h : _data)
    {
      if (index > h.bytesOfHistory())
      {
        h.findAll(file.begin() + index, [&](char usedPreviously) {
	    charCounter[usedPreviously] += weight;
	    denominator += weight;
	  });
      }
      weight <<= 1;
    }

  }
};

void processFile(File &file)
{
  AllHashedHistory allHashedHistory;
  int totalFound = 0;
  double costInBits = 0;
  for (unsigned int i = 0; i < file.size(); i++)
  {
    CharCounter charCounter;
    int denominator;
    allHashedHistory.getStats(file, i, charCounter, denominator);
    const int numerator = charCounter[file.begin()[i]];
    if (numerator != 0)
    {
      totalFound++;
      costInBits += pCostInBits(numerator/(double)denominator);
    }
    allHashedHistory.add(file, i);
  }
  std::cout<<"Bytes encoded: "<<totalFound<<", afterEncoding: "
	   <<(costInBits/8)<<std::endl;
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
  {
    char const *const fileName = argv[i];
    std::cout<<"File name: "<<fileName<<std::endl;
    File file(fileName);
    processFile(file);
  }
}
