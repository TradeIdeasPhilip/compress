#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include "../shared/File.h"
#include "../shared/RansHelper.h"


// g++ -o 4p -O3 -ggdb -std=c++0x -Wall 4p.C ../shared/File.C ../shared/Misc.C

class HashListCounter
{
private:
  double _costInBits;
  double _totalCount;
  std::map< int, int > _counts;
public:
  HashListCounter() : _costInBits(0.0), _totalCount(0.0) {}
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
      //std::cout<<"save("<<index<<")"<<std::endl;
    }
  }
  void use(int index)
  {
    int &count =_counts[index];
    const double newCost = pCostInBits(count/_totalCount);
    /*
    std::cout<<"use("<<index<<") count="<<count
    <<", _totalCount="<<_totalCount<<", newCost="<<newCost
    <<", _costInBits="<<_costInBits<<std::endl;
    */
    _costInBits += newCost;
    count+=1;
    _totalCount+=1;
  }
  void dump(std::ostream &out) const
  {
    std::map< int, int > binCounter;
    for (auto const &kvp : _counts)
    {
      const int useCount = kvp.second;
      binCounter[useCount]++;
    }
    for (auto const &kvp : binCounter)
    {
      const int useCount = kvp.first;
      const int indexCount = kvp.second;
      out<<indexCount<<" indicies were each used "<<(useCount - 1)<<" times."<<std::endl;
    }
  }
  double getCostInBits() const { return _costInBits; }
};

void processFile(File &file, int hashBufferSize, int hashEntrySize)
{
    HashListCounter hashListCounter;
  //std::cout<<"processFile()"<<std::endl;
  size_t individualBytes = 0;
  size_t hashEntries = 0;
  std::vector<std::string> hashBuffer(hashBufferSize);
  auto current = file.begin();
  auto const recordNewHash = [&]() {
    if ((current - file.begin()) >= hashEntrySize)
    {
      const std::string newEntry(current - hashEntrySize, hashEntrySize);
      const int index = simpleHash(newEntry) % hashBufferSize;
      hashBuffer[index] = newEntry;
        hashListCounter.save(index);
    }
  };
  while (current < file.end())
  {
    bool madeProgress = false;
    if ((file.end() - current) >= hashEntrySize)
    {
      //std::cout<<"AAAAA"<<std::endl;
      const std::string possibleEntry(current, hashEntrySize);
      //std::cout<<"possibleEntry="<<possibleEntry<<std::endl;
      const int index = simpleHash(possibleEntry) % hashBufferSize;
      //std::cout<<"possibleEntry="<<possibleEntry<<", index="<<index<<""
      if (hashBuffer[index] == possibleEntry)
      {
        madeProgress = true;
        current += hashEntrySize;
        hashEntries++;
        //std::cout<<"hash entry:  "<<possibleEntry<<std::endl;
        hashListCounter.use(index);
      }
    }
    if (!madeProgress)
    {
      //std::cout<<"individual byte:  "<<*current<<std::endl;
      current++;
      recordNewHash();
      individualBytes++;
    }
  }
  int hashBufferFree = 0;
  //std::cout<<"hashBuffer:";
  for (std::string const &hashEntry : hashBuffer)
  {
    if (hashEntry == "")
    {
      hashBufferFree++;
    }
    else 
    {
      //std::cout<<" ❝"<<hashEntry<<"❞";
    }
  }
  //std::cout<<std::endl;
  //hashListCounter.dump(std::cout);
  // Assume 1 bit for simplicity.  The entropy encoded can probably do better, but not by much.
  const auto decisionCostInBits = (hashEntries + individualBytes);
  // For simplicity just look at the maximum size of the buffer.  in fact many of the entries will
  // be written when the buffer is mostly empty, so it will take less space to write this value 
  // than shown here.
  // For simplicity assume that all indexes are just as likely.  At some point it's worth checking
  // to see if this is try.  If some index values come up a lot more than others, then the
  // entropy encoder can help us even more.
  const auto simpleHashCodeCostInBits = std::log(hashBufferSize - hashBufferFree)/ std::log(2)*hashEntries;
  const auto betterHashCodeCostInBits = hashListCounter.getCostInBits();
  // Assume that each of these will be written out literally.
  // In fact there is a plan to compress bytes that aren't in the buffer.
  // At the moment we're only testing the hashing part of the algorithm.
  const auto individualByteCostInBits = individualBytes * 8;
  const double totalCostInBytes = std::ceil((decisionCostInBits + betterHashCodeCostInBits + individualByteCostInBits) / 8);
  auto const fileSize=file.end() - file.begin();
  std::cout<<"processFile() fileSize="<<fileSize
  <<", hashBufferSize="<<hashBufferSize
  <<", hashBufferFree="<<hashBufferFree
  <<", hashEntrySize="<<hashEntrySize
  <<", hashEntries="<<hashEntries
  <<", simpleHashCodeCostInBits="<<simpleHashCodeCostInBits
  <<", betterHashCodeCostInBits="<<betterHashCodeCostInBits
  <<", individualBytes="<<individualBytes
  <<", totalCostInBytes="<<totalCostInBytes
  <<", savings="<<((fileSize-totalCostInBytes)*100.0/fileSize)<<'%'
  <<std::endl;
}

void processFileRange(File &file, int hashBufferSize, int minHashEntrySize, int maxHashEntrySize)
{
  HashListCounter hashListCounter;
  //std::cout<<"processFile()"<<std::endl;
  size_t individualBytes = 0;
  size_t hashEntries = 0;
  std::vector<std::string> hashBuffer(hashBufferSize);
  auto current = file.begin();
  auto const recordNewHash = [&]() {
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
  while (current < file.end())
  {
    bool madeProgress = false;
    const int64_t bytesRemaining = file.end() - current;
    const int min = std::min((int64_t)minHashEntrySize, bytesRemaining);
    for (int hashEntrySize = maxHashEntrySize; hashEntrySize >= min; hashEntrySize--)
    {
      //std::cout<<"AAAAA"<<std::endl;
      const std::string possibleEntry(current, hashEntrySize);
      //std::cout<<"possibleEntry="<<possibleEntry<<std::endl;
      const int index = simpleHash(possibleEntry) % hashBufferSize;
      //std::cout<<"possibleEntry="<<possibleEntry<<", index="<<index<<""
      if (hashBuffer[index] == possibleEntry)
      {
        madeProgress = true;
        current += hashEntrySize;
        hashEntries++;
        //std::cout<<"hash entry:  "<<possibleEntry<<std::endl;
        hashListCounter.use(index);
        break;
      }
    }
    if (!madeProgress)
    {
      //std::cout<<"individual byte:  "<<*current<<std::endl;
      current++;
      recordNewHash();
      individualBytes++;
    }
  }
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
      std::cout<<"Free";
    }
    else
    {
      std::cout<<length<< "bytes";
    }
    std::cout<<": count="<<count<<std::endl;
  }
  //hashListCounter.dump(std::cout);
  auto const hashBufferFree = hashBufferLengths[0];
  // Assume 1 bit for simplicity.  The entropy encoded can probably do better, but not by much.
  const auto decisionCostInBits = (hashEntries + individualBytes);
  // For simplicity just look at the maximum size of the buffer.  in fact many of the entries will
  // be written when the buffer is mostly empty, so it will take less space to write this value 
  // than shown here.
  // For simplicity assume that all indexes are just as likely.  At some point it's worth checking
  // to see if this is try.  If some index values come up a lot more than others, then the
  // entropy encoder can help us even more.
  const auto simpleHashCodeCostInBits = std::log(hashBufferSize - hashBufferFree)/ std::log(2)*hashEntries;
  const auto betterHashCodeCostInBits = hashListCounter.getCostInBits();
  // Assume that each of these will be written out literally.
  // In fact there is a plan to compress bytes that aren't in the buffer.
  // At the moment we're only testing the hashing part of the algorithm.
  const auto individualByteCostInBits = individualBytes * 8;
  const double totalCostInBytes = std::ceil((decisionCostInBits + betterHashCodeCostInBits + individualByteCostInBits) / 8);
  auto const fileSize=file.end() - file.begin();
  std::cout<<"processFileRange() fileSize="<<fileSize
  <<", hashBufferSize="<<hashBufferSize
  //<<", hashBufferFree="<<hashBufferFree
  <<", hashEntrySize="<<minHashEntrySize<<'-'<<maxHashEntrySize
  <<", hashEntries="<<hashEntries
  <<", simpleHashCodeCostInBits="<<simpleHashCodeCostInBits
  <<", betterHashCodeCostInBits="<<betterHashCodeCostInBits
  <<", individualBytes="<<individualBytes
  <<", totalCostInBytes="<<totalCostInBytes
  <<", savings="<<((fileSize-totalCostInBytes)*100.0/fileSize)<<'%'
  <<std::endl;
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
  {
    char const *const fileName = argv[i];
    std::cout<<"File name: "<<fileName<<std::endl;
    File file(fileName);
    constexpr int bufferSizes[]{ 257, 509, 1021, 2053, 4093 };
    for (int bufferSize : bufferSizes)
    {
        std::cout<<std::endl<<"  -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"<<std::endl<<std::endl;
      for (int hashEntrySize = 3; hashEntrySize < 8; hashEntrySize++)
      {
        processFile(file, bufferSize, hashEntrySize);
        std::cout<<std::endl;
      }
      processFileRange(file, bufferSize*2-1, 4, 5);
      std::cout<<std::endl;
      processFileRange(file, bufferSize*2-1, 4, 6);
    }
  }
}
