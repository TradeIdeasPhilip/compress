#include <iostream>
#include <fstream>

#include "RansBlockReader.h"

// g++ -o uneight -O4 -ggdb -std=c++0x -Wall Uneight.C File.C RansBlockReader.C

int main(int argc, char **argv)
{ // See notes in Eight.C regarding isIntelByteOrder().
  assert(isIntelByteOrder());

  if ((argc < 2) || (argc > 3))
  {
    std::cerr<<"syntax:  "<<argv[0]<<" [input_file] output_file"<<std::endl;
    return 1;
  }

  const std::string outputFileName = argv[argc-1];
  const std::string inputFileName =
    (argc == 3)?argv[1]:(outputFileName + ".μ8");

  RansBlockReader inFile(inputFileName.c_str());
  
  std::ofstream outFile(outputFileName);
  if (!outFile)
  {
    std::cerr<<"Unable to open output file:  "
	     <<outputFileName<<std::endl;
    return 1;
  }
  outFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

  
} 
