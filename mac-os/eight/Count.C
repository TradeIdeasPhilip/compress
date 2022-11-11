#include <string>
#include <iostream>
#include <map>

#include "../shared/File.h"
#include "../shared/RansBlockWriter.h"

// clang++ -o count -O3 -ggdb -std=c++0x -Wall Count.C ../shared/RansBlockWriter.C ../shared/File.C ../shared/Misc.C

const int MAX_INPUT_FILE_SIZE = 100000000;

void compress(std::string const &inputFileName, bool headerOnly = false)
{
  File inputFile(inputFileName);
  if (!inputFile.valid())
  {
    std::cerr << inputFileName << ": " << inputFile.errorMessage() << std::endl;
  }
  else if (inputFile.size() > MAX_INPUT_FILE_SIZE)
  {
    std::cerr << inputFileName << ": file too big: MAX_INPUT_FILE_SIZE=" << MAX_INPUT_FILE_SIZE << ", size=" << inputFile.size() << std::endl;
  }
  else
  {
    const std::string outputFileName = inputFileName + std::string(".C↓");
    RansBlockWriter outputFile(outputFileName);
    outputFile.write(RansRange(inputFile.size(), 1, MAX_INPUT_FILE_SIZE + 1));
    std::vector<int> byteCount(256);
    for (char const *p = inputFile.begin();
         p < inputFile.end();
         p++)
    {
      // std::cout << "i=" << (p - inputFile.begin()) << ", *p=" << (*p) << ", #" << (uint32_t)(uint8_t)*p << std::endl;
      byteCount[(uint8_t)*p]++;
    }
    auto remaining = inputFile.size();
    for (int count : byteCount)
    {
      if (remaining < 1)
        break;
      assert(count <= remaining);
      outputFile.write(RansRange(count, 1, remaining + 1));
      remaining -= count;
    }
    assert(remaining == 0);
    std::vector<RansRange> descriptionOfBytes;
    for (int count : byteCount)
    {
      descriptionOfBytes.emplace_back(remaining, count, inputFile.size());
      remaining += count;
    }
    assert(remaining == inputFile.size());
    if (!headerOnly)
    {
      for (char const *p = inputFile.begin(); p < inputFile.end(); p++)
      {
        outputFile.write(descriptionOfBytes[(uint8_t)*p]);
      }
    }
  }
}

int main(int argc, char **argv)
{
  bool headerOnly = false;
  for (int i = 1; i < argc; i++)
  {
    const std::string arg = argv[i];
    if (arg == "--header-only")
    {
      headerOnly = true;
    }
    else if (arg == "--no-header-only")
    {
      headerOnly = false;
    }
    else
    {
      compress(arg, headerOnly);
    }
  }
}