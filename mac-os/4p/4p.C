#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include "../shared/File.h"


// g++ -o 4p -O3 -ggdb -std=c++0x -Wall 4p.C ../shared/File.C ../shared/Misc.C

void processFile(File &file, int hashBufferSize, int hashEntrySize)
{
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
  // Assume 1 bit for simplicity.  The entropy encoded can probably do better, but not by much.
  const auto decisionCostInBits = (hashEntries + individualBytes);
  // For simplicity just look at the maximum size of the buffer.  in fact many of the entries will
  // be written when the buffer is mostly empty, so it will take less space to write this value 
  // than shown here.
  // For simplicity assume that all indexes are just as likely.  At some point it's worth checking
  // to see if this is try.  If some index values come up a lot more than others, then the
  // entropy encoder can help us even more.
  const auto hashCodeCostInBits = std::log(hashBufferSize - hashBufferFree)/ std::log(2);
  // Assume that each of these will be written out literally.
  // In fact there is a plan to compress bytes that aren't in the buffer.
  // At the moment we're only testing the hashing part of the algorithm.
  const auto individualByteCostInBits = individualBytes * 8;
  const double totalCostInBytes = std::ceil((decisionCostInBits + hashCodeCostInBits + individualByteCostInBits) / 8);
  std::cout<<"processFile() fileSize="<<(file.end() - file.begin())
  <<", hashBufferSize="<<hashBufferSize
  //<<", hashBufferFree="<<hashBufferFree
  <<", hashEntrySize="<<hashEntrySize
  <<", hashEntries="<<hashEntries
  <<", individualBytes="<<individualBytes
  <<", totalCostInBytes="<<totalCostInBytes
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
      for (int hashEntrySize = 3; hashEntrySize < 8; hashEntrySize++)
      {
        processFile(file, bufferSize, hashEntrySize);
      }
    }
  }
}
