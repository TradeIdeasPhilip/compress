#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <stdint.h>

#include "../shared/RansBlockReader.h"

// clang++ -o uncount -g -std=c++17 Uncount.C ../shared/RansBlockReader.C ../shared/File.C ../shared/Misc.C

const int MAX_INPUT_FILE_SIZE = 100000000;

void uncompress(std::string const &inputFileName)
{
  RansBlockReader inputFile(inputFileName.c_str());

  const auto fileSize = inputFile.get(MAX_INPUT_FILE_SIZE + 1);
  inputFile.advance(RansRange(fileSize, 1, MAX_INPUT_FILE_SIZE + 1));
  std::cout << "fileSize=" << fileSize << std::endl;

  auto remaining = fileSize;
  auto nextStart = fileSize - fileSize;

  struct Stats
  {
    uint32_t start;
    uint32_t count;
    char output;
  };
  std::vector<Stats> allStats;
  while (remaining)
  {
    Stats stats;
    stats.start = nextStart;
    stats.count = inputFile.get(remaining + 1);
    inputFile.advance(RansRange(stats.count, 1, remaining + 1));
    stats.output = allStats.size();
    assert(allStats.size() < 256);
    allStats.push_back(stats);
    remaining -= stats.count;
    nextStart += stats.count;
  }
  /*
  for (Stats const &stats : allStats)
  {
    const int output = (unsigned char)stats.output;
    if (output > ' ' && output < 128)
    {
      std::cout<<"'"<<stats.output<<"'";
    }
    else
    {
      std::cout<<output;
    }
    std::cout<<"\t"<<stats.start<<"\t"<<stats.count<<std::endl;
  }
   */
  std::map< uint32_t, Stats > statsByStart;
  for (Stats const &stats : allStats)
  {
    if (stats.count > 0)
    {
      statsByStart[stats.start] = stats;
    }
  }
  const std::string outputFileName = inputFileName + ".##";
  std::ofstream outputFile(outputFileName, std::ios_base::trunc);
  int bytesRead = 0;
  while (!inputFile.eof())
  {
    auto const got = inputFile.get(fileSize);
    auto it = statsByStart.upper_bound(got);
    it--;
    Stats const &stats = it->second;
    outputFile<<stats.output;
    inputFile.advance(RansRange(stats.start, stats.count, fileSize));
    bytesRead++;
  }
}

int main(int argc, char **argv)
{
  //std::cout<<"Cost of a 99% vs 1% decision"<<booleanCostInBits(0.99)<<std::endl;
  for (int i = 1; i < argc; i++)
  {
    uncompress(argv[i]);
  }
}