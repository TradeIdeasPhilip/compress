#include <string.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include "File.h"
#include "RansHelper.h"


// g++ -o hash-down -O4 -ggdb -std=c++0x -Wall -lexplain HashDown.C File.C Misc.C

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

// Idea for the bytes that we can't find in our new HashedHistory:
// Start with a basic SymbolCounter to say how common each byte is in the
// input file so far.
// But then look at the CharCounter object.  We already have one of these
// because that was the object that said the current character was not
// available in the HashedHistory.
// Every byte that is available in CharCounter gets removed from the
// SymbolCounter, its probability is set to 0.
// This simple combination should make good predictions.

// Interesting.  My test files show typically only find around 2 to 3
// characters each time we check AllHashedHistory.  See "Average size() of
// CharCounter" in this file and the output for more details.
//
// The stuff we can compress goes down to about 3-4% of its original size.
// That's promising.
//
// Typically I see 67% of the bytes being handled by this.  A big log file
// got up around 95%.  That's promising as part of the solution.  But we need
// more than the minimum to fill in the gaps right now.

// Most of the time the table with a context of 1 byte has a whole lot of
// empty slots in the hash table.  Which means that the other slots are getting
// hammered.  Entries are getting MRU'ed out of the table way to quickly to
// be useful.  In short, let's jettison this part.  Replace it with something
// better.
//
// Proposal:  When looking at one byte of context, do not use a HashedHistory.
// Instead use something that's worked well in another project.  Use a map to
// store the count for *every* pair of bytes that we've seen.  That can mean
// up to 64k entries.  In this case brute force seems to work, but the memory
// grows exponentially and even 2 bytes of context would probably be too much.
// Trim the table as needed by diving everything by 2.  1 will go to 0; we
// plan to go a good job at the next level, so it's okay if something falls
// off of this table.
//
// We also need to add a third option.  *Any* byte should be available from
// this option at any time.  Simple enough.  Use a SymbolCounter to track the
// frequency of each byte.  That class automatically ensures that every byte
// has a weight that is greater than 0.
//
// Output format:  Each byte generates two calls to the rANS encoder.  The
// first says which algorithm to use.  Then use that algorithm to encode the
// data.  The output from different algorithms will use different scales, and
// in these cases I find it best to break the work up into two calls to the
// encoder, rather than trying to massage the data all into one scale.
//
// We have three different ways to process the data:
// 1) Look at n bytes of context with a HashedHistory.  We know how to weight
//    the data associated with different values of n, so we combine all the
//    results in a single AllHashedHistory object.
// 2) Look at the full data set for one byte of context.  I.e. not a hash
//    table.  Ideally no data lost, although we might have to divide the
//    counts each by two, and throw out the remainder sometimes.
// 3) Use a SymbolCounter to track the frequency of each byte regardless of
//    context.
//
// A byte might be available from more than one of those algorithms.
// Ignoring that would not be good for compression.  We give the highest
// priority to algorithm #1, and the lowest to algorithm #3.  If a byte is
// available from algorithm #1 and from algorithm #2, algorithm #2 must delete
// the byte from its tables.  Algorithm #2 will now associate a probability
// of 0 with that byte.

// Speed is surprisingly terrible compared to gzip -9.  I'm mildly curious why,
// but I don't plan to do anything about it.


class HashedHistory
{
private:
  const int _bytesOfHistory;
  const int _hashModulus;
  const int _entriesPerHash;
  const int _sizePerEntry;
  const int _sizePerHash;
  std::string _body;
  static uint64_t hash(char const *begin, char const *end)
  { // How expensive is this?  We could do a hash where each new byte that
    // we add to the left does one small operation and doesn't start fresh.
    // Then, if we're doing a hash of N bytes we get the hash for all the
    // smaller strings for free.
    if (end - begin == 1)
    { // Change the hash so that a 1 byte string will always return that
      // byte.  So when we look at one byte strings, we always get that byte
      // back as the hash.  Otherwise our table with one byte of context
      // looks terrible.
      return *(uint8_t const *)begin;
      // OR There is a lot of wasted space it the table with one byte of
      // context.  And there always will be for text files.  Maybe we take
      // the table of one byte context, and give it module of a prime near
      // 128.  Then raise the max number of entries per hash.  So we could
      // request the same amount of data, but hopefully a lot more of it will
      // be in use.  So we'd expect better compression at the cost of more CPU
      // time and more complicated code.  Intriguing.  This is worth a try
      // just see!
    }
    else
    { // TODO This should be something that can be repeated on different
      // computers.  As far as I know std::_Hash_impl::hash() is undocumented
      // and the C++ standard does not specify what a hash function should be.
      //
      // https://stackoverflow.com/questions/299304/why-does-javas-hashcode-in-string-use-31-as-a-multiplier
      // The Java string hash function is very simple.
      return std::_Hash_impl::hash(begin, end - begin);
    }
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
      /*
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
      */
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
    //detailedDump(std::cout);
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
    _data.reserve(7);
    _data.emplace_back(2, 751, 1);
    _data.emplace_back(3, 757, 1);
    _data.emplace_back(4, 761, 1);
    _data.emplace_back(5, 769, 1);
    _data.emplace_back(6, 773, 1);
    _data.emplace_back(7, 787, 1);
    _data.emplace_back(8, 797, 1);
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


class MicroProfiler
{
private:
  int64_t _time;
  int _count = 0;
public:
  MicroProfiler() : _time(0), _count(0) { }
  int64_t getTime() const { return _time; }
  int getCount() const { return _count; }
  class Run
  {
  private:
    MicroProfiler &_owner;
  public:
    Run(MicroProfiler &owner) :
      _owner(owner)
    {
      _owner._time -= getMicroTime();
    }
    ~Run()
    {
      _owner._time += getMicroTime();
      _owner._count++;
    }
  };
  void report(std::ostream &out, std::string const &name)
  {
    out<<name<<":  "<<(_time/1000000.0)<<"s / "<<_count<<" = "
       <<(_time / (double)_count)<<"μs each."
       <<std::endl;
  }
};


class OneByteContext
{
private:
  class Range
  {
  public:
    const uint16_t start;
    const uint16_t max;
    Range(char const *end) :
      start(((uint16_t)*(end-1))<<8),
      max(start | 0xff)
    { }
  };
  
  std::map<uint16_t, uint16_t> _counters;
  int _overflowCount;
  MicroProfiler _profiler;
  
public:
  int bytesOfHistory() const { return 1; }
  OneByteContext() : _overflowCount(0) { }
  void add(char const *newChar)
  {
    const uint8_t context = *(newChar - 1);
    const uint8_t suggestion = *newChar;
    const uint16_t key = (context<<8) | suggestion;
    MicroProfiler::Run mp(_profiler);
    const uint16_t counter = ++_counters[key];
    if (counter == 0xffff)
    { 
      _overflowCount++;
      Range range(newChar);
      for (auto it = _counters.lower_bound(range.start);
	   (it != _counters.end()) && (it->first <= range.max);
	   )
      {
	const uint16_t after = (it->second /= 2);
	std::cout<<"Reduced "<<((char)(it->first>>8))<<" "<<((char)(it->first))
		 <<" "<<after<<std::endl;
	if (after)
	{ // Leave what's left and move to the next one.
	  it++;
	}
	else
	{ // Remove the pointer to 0, and move to the next one.
	  it = _counters.erase(it);
	}
      }
    }
  }

  // The expected likelihood of each byte that we might want to place at
  // *end based on the context.
  std::map<char, int> getCounts(char const *end)
  {
    std::map<char, int> result;
    Range range(end);
    for (auto it = _counters.lower_bound(range.start);
	 (it != _counters.end()) && (it->first <= range.max);
	 it++)
    {
      result[it->first] = it->second; 
    }
    return result;
  }
  void superDetailedDump(std::ostream &out)
  {
    for (auto const &kvp : _counters)
    {
      const auto key = kvp.first;
      const char context = key>>8;
      const char suggestion = key;
      const auto count = kvp.second;
      out<<context<<' '<<suggestion<<" => "<<count<<std::endl;
    }
  }
  void shortDump(std::ostream &out)
  {
    if (_overflowCount)
    {
      out<<"One byte of context:  _overflowCount = "<<_overflowCount
	 <<std::endl;
    }
    out<<"A total of "<<_counters.size()
       <<" entries in OneByteContext::_counters"<<std::endl;
    _profiler.report(out, "OneByteContext table access");
  }
  
  void detailedDump(std::ostream &out)
  {
    out<<"_______________ One byte of context ____________________"<<std::endl
       <<"_overflowCount = "<<_overflowCount<<std::endl;
    
    // number of possibilites for a context -> number of contexts with this
    // many possibilities.
    std::map<int, int> accumulator;
    int totalNumberOfContexts = 0;
    int lastContext = 0x12345678;
    int possibilitiesForThisContext = 0;
    auto const processContextEnd = [&]() {
      if (possibilitiesForThisContext)
      {
	accumulator[possibilitiesForThisContext]++;
	totalNumberOfContexts++;
	possibilitiesForThisContext = 0;
      }
    };
    for (auto const &kvp : _counters)
    {
      const auto key = kvp.first;
      const char context = key >>8;
      if (context != lastContext)
      {
	processContextEnd();
	lastContext = context;
      }
      possibilitiesForThisContext++;
    }
    processContextEnd();
    for (auto const &kvp : accumulator)
    {
      auto const numberOfPossibilities = kvp.first;
      auto const numberOfContexts = kvp.second;
      out<<numberOfContexts<<" context"<<((numberOfContexts!=1)?"s":"")
	 <<" with "<<numberOfPossibilities<<" possibilities."<<std::endl;
    }
    out<<"For a total of "<<totalNumberOfContexts<<" context"
       <<((totalNumberOfContexts!=1)?"s":"")<<" with at least one possibility."
       <<std::endl;
    out<<"A total of "<<_counters.size()
       <<" entries in OneByteContext::_counters"<<std::endl;      
  }
  ~OneByteContext()
  {
    //detailedDump(std::cout);
    shortDump(std::cout);
  }
};

class ZeroByteContext
{
private:
  SymbolCounter _symbolCounter;
  int _bytesProcessedSinceLastReset;
public:
  int bytesOfHistory() const { return 0; }
  ZeroByteContext() : _bytesProcessedSinceLastReset(0) { }
  void add(char const *newChar)
  { // TODO move the MAX logic into SymbolCounter::increment() in RansHelper.h.
    // I'd do it now, but the algorithm is still being tested and SymbolCounter
    // is already being used in more mature programs.
    static const int MAX = RansRange::SCALE_END>>2;
    if (_bytesProcessedSinceLastReset >= MAX)
    {
      _bytesProcessedSinceLastReset -= MAX;
      _symbolCounter.reduceOld();
    }
    _symbolCounter.increment(*newChar);
    _bytesProcessedSinceLastReset++;
  }
  double getCostInBits(std::set<char> const &exclude, char toEncode)
  {
    uint64_t denominator = 0;
    for (int i = 0; i < 256; i++)
    {
      denominator += _symbolCounter.freq(i);
    }
    assert(denominator < RansRange::SCALE_END);
    const int numerator = _symbolCounter.freq((unsigned char)toEncode);
    return pCostInBits(numerator / (double)denominator);
  }
};

void processFile(File &file)
{ // We have three different algorithms for compressing the data.
  // The first one we try doesn't work in some contexts (so both the reader
  // and the writer know this) but when it does work, it works really well.
  // The last algorithm does the least compression but is always available.
  // The middle algorithm is half way between.
  //
  // The basic idea is to try the algoriths in order.  Each time, start with
  // the parts of the code that could be done on encoder and the decoder.
  // Based on this we might choose to completely skip the algorithm.  Both
  // sides agree so nothing has to be written to the file.
  //
  // If we try an algorithm, then we send a yes or no to the file to say if
  // this algorithm actually worked!
  //
  // If we try and algorithm and it works, we are done.  The other algorithms
  // are given a chance to review the latest data and update their tables.
  // But they will not print anything to the file this time.
  //
  // If we skip an algorithm, or we try it and it fails, we go to the next
  // algorithm.
  
  AllHashedHistory allHashedHistory;
  // Number of times this algorithm said yes.
  int hashedHistoryFound = 0;
  // Number of times this algorithm said yes or no.
  int hashedHistoryPossible = 0;
  // If the answer is yes we send more details to the file.  This is the cost
  // of that next write.  This value is typically very small.
  double hashedHistoryCostInBits = 0;
  // How many items were we choosing from?  Most of the time it's just one
  // item, which is why hashedHistoryCostInBits is typically so low.  There
  // is a proposal in README.md to tweak the algorithm so this algorithm can
  // only recommend one byte.  Either the next byte matches our prediction or
  // it does not.  So hashedHistoryCostInBits would be exactly 0 and
  // totalInCharCounter would be exactly hashedHistoryPossible.
  int64_t totalInCharCounter = 0;
  
  OneByteContext oneByteContext;
  int oneByteContextFound = 0;
  int oneByteContextPossible = 0;
  double oneByteContextCostInBits = 0;

  for (unsigned int i = 0; i < file.size(); i++)
  {
    bool encoded = false;
    std::set< uint8_t > exclude;
    auto const nextBytePtr = file.begin() + i;
    {
      CharCounter charCounter;
      int denominator;
      allHashedHistory.getStats(file, i, charCounter, denominator);
      if (denominator > 0)
      { // This is the part that the encoder and decoder share, so it must be
	// done first. (hashedHistoryFound / hashedHistoryPossible) is what
	// we have to send to the entropy encoder to say that we did or did
	// not plan to use this algorithm.  If denominator was 0 then both
	// sides would know that we would *never* use the first algorithm,
	// so there's no decision to record here.  hashedHistoryPossible is
	// the number of decisions we had to make and record.
	hashedHistoryPossible++;
      }
      const int numerator = charCounter[*nextBytePtr];
      if (numerator != 0)
      {
	totalInCharCounter += charCounter.size();
	hashedHistoryFound++;
	hashedHistoryCostInBits += pCostInBits(numerator/(double)denominator);
	encoded = true;
      }
      else
      {
	for (auto const &kvp : charCounter)
	{
	  const char byte = kvp.first;
	  const int count = kvp.second;
	  if (count)
	  {
	    exclude.insert(byte);
	  }
	}
      }
      allHashedHistory.add(file, i);
    }
    if (!encoded)
    { // TODO the compression that we're getting for the bytes that we
      // attempt doesn't work as well as I'd expect.  Take a closer look.
      if (i > 0)
      {
	uint64_t denominator = 0;
	std::map<char, int> counts = oneByteContext.getCounts(nextBytePtr);
	for (auto const &kvp : counts)
	{
	  if (!exclude.count(kvp.first))
	  { // Skip everything in exclude!
	    denominator += kvp.second;
	  }
	}
	if (denominator > 0)
	{ // At this point both sides know that we have the option of using
	  // the OneByteContext or the ZeroByteContext.  So the encoder will
	  // have to make a decision and record that decision in the output
	  // file.
	  oneByteContextPossible++;
	}
	const int numerator = counts[*nextBytePtr];
	if (numerator == 0)
	{
	  for (auto const &kvp : counts)
	  {
	    const int count = kvp.second;
	    if (count > 0)
	    {
	      exclude.insert(kvp.first);
	    }
	  }
	}
	else
	{
	  encoded = true;
	  assert(denominator < RansRange::SCALE_END);
	  const double cost = pCostInBits(numerator / (double)denominator);
	  oneByteContextFound++;
	  oneByteContextCostInBits += cost;
	}
      }
    }
    if (i > 0)
    {
      oneByteContext.add(nextBytePtr);
    }
	
    if (!encoded)
    {
      // TODO
      // For the estimated cost I'm just assuming that I'll send a normal 8
      // bit byte.  I'm sure I could do better, but there aren't a lot of
      // these so I don't want to optimize here.
    }
    
  }
  std::cout<<"AllHashedHistory bytes encoded: "<<hashedHistoryFound
	   <<", afterEncoding: "
	   <<(hashedHistoryCostInBits/8)<<std::endl;
  std::cout<<"OneByteContext bytes encoded: "<<oneByteContextFound
	   <<", afterEncoding: "
	   <<(oneByteContextCostInBits/8)<<std::endl;
  const int bytesSkipped =
    file.size() - hashedHistoryFound - oneByteContextFound;
  std::cout<<"Bytes skipped (TODO!): "
	   <<bytesSkipped
	   <<std::endl;
  std::cout<<"Average size() of CharCounter (AllHashedHistory):"
	   <<(totalInCharCounter/(double)hashedHistoryFound)<<std::endl;
  const double allHashedHistoryRatio =
    hashedHistoryFound / (double)hashedHistoryPossible;
  const double allHashedHistoryQuestionCost =
    booleanCostInBits(allHashedHistoryRatio) * hashedHistoryPossible;
  std::cout<<"AllHashedHistory decisions: "<<hashedHistoryFound<<" / "
	   <<hashedHistoryPossible<<" = "
	   <<allHashedHistoryRatio
	   <<", cost = "<<allHashedHistoryQuestionCost<<" bits"
	   <<std::endl;
  const double oneByteHistoryRatio =
    oneByteContextFound / (double)oneByteContextPossible;
  const double oneByteHistoryQuestionCost =
    booleanCostInBits(oneByteHistoryRatio) * oneByteContextPossible;
  std::cout<<"OneByteHistory decisions: "<<oneByteContextFound<<" / "
	   <<oneByteContextPossible<<" = "
           <<oneByteHistoryRatio
	   <<", cost = "<<oneByteHistoryQuestionCost<<" bits"
	   <<std::endl;
  const double totalCost = ceil((allHashedHistoryQuestionCost+hashedHistoryCostInBits+oneByteHistoryQuestionCost+oneByteContextCostInBits)/8+bytesSkipped);
  std::cout<<"Total cost = ("<<allHashedHistoryQuestionCost<<" / 8) + "
	   <<(hashedHistoryCostInBits/8)<<" + ("
	   <<oneByteHistoryQuestionCost<<" / 8) + "
	   <<(oneByteContextCostInBits/8)<<" + "<<bytesSkipped<<" = "
	   <<totalCost<<std::endl
	   <<"Total savings = "<<((1-totalCost/file.size())*100)<<"%"
	   <<std::endl;
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
