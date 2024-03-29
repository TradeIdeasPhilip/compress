#include <iostream>
#include <fstream>

#include "../shared/RansBlockReader.h"
#include "EightShared.h"


// TODO / bug:  In some cases a bad input file can cause this program to dump
// core.  That's not right.  It should always throw an exception if there is
// a problem.
// See related TODO items in RansBlockReader.C.  We could add more checks
// and I've marked the places.

int main(int argc, char **argv)
{ // See notes in Eight.C regarding isIntelByteOrder().
  assert(isIntelByteOrder());

  if ((argc < 2) || (argc > 3))
  {
    std::cerr<<"syntax:  "<<argv[0]<<" input_file [output_file]"<<std::endl;
    return 1;
  }

  const std::string inputFileName = argv[1];
  const std::string outputFileName =
    (argc >= 3)?argv[2]:(inputFileName + ".re");

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

  try
  {
    std::string buffer = preloadContents;
    TopLevel topLevel;
    while (!inFile.eof())
    {
      char ch = topLevel.decode(&*buffer.begin(), &*buffer.end(), inFile);
      outFile<<ch;
      buffer += ch;
      // TODO What's wrong with the code below?  When I uncomment it the input
      // file appears to be corrupted.  Commenting these lines out is a
      // temporary hack and not acceptable long run.
      //if (buffer.length() >= (size_t)maxBufferSize * 2)
      //buffer.erase(buffer.begin(), buffer.end() - maxBufferSize);
    }
  }
  catch (std::exception &ex)
  {
    std::cout<<"Exception:  "<<ex.what()<<std::endl;
    return 8;
  }
}
