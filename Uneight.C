#include <iostream>
#include <fstream>

#include "RansBlockReader.h"
#include "EightShared.h"


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

  // TODO this should probably be binary.
  std::ofstream outFile(outputFileName);
  if (!outFile)
  {
    std::cerr<<"Unable to open output file:  "
	     <<outputFileName<<std::endl;
    return 1;
  }
  outFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

  std::string buffer = preloadContents;
  TopLevel topLevel;
  while (!inFile.eof())
  {
    char ch = topLevel.decode(&*buffer.begin(), &*buffer.end(), inFile);
    outFile<<ch;
    buffer += ch;
    if (buffer.length() >= (size_t)maxBufferSize * 2)
      buffer.erase(buffer.begin(), buffer.end() - maxBufferSize);
  }
} 
