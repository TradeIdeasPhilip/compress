#include <string.h>

#include "JumpBackSummary.h"

#ifdef __UNIT_TEST_JumpBackSummary__
#include <iostream>
#endif


// g++ -D__UNIT_TEST_JumpBackSummary__ -Wall -O4 JumpBackSummary.C
// g++ -ggdb -D__UNIT_TEST_JumpBackSummary__ -Wall -O0 JumpBackSummary.C

static int matchLength(char const *a, char const *b, int max)
{
  for (int i = 0; ; i++)
  {
    if (i >= max)
      return i;
    if (a[i] != b[i])
      return i;
  }
}

inline bool matchedLength(char const *a, char const *b, int max)
{
  return matchLength(a, b, max) >= max;
}

JumpBackSummary::JumpBackSummary(char const *p)
{
  char bytes[8];
  for (int i = 0; i < 8; i++)
    bytes[i] = p[-(1+i)];

  for (int recentMatchLength = 0; recentMatchLength <= 8; recentMatchLength++)
  {
    for (int jump = 1; ; jump++)
    {
#ifdef __UNIT_TEST_JumpBackSummary__
      std::cout<<std::string(recentMatchLength, '=');
      if (recentMatchLength < 8)
	std::cout<<"≠"<<std::string(7-recentMatchLength, '?');  // ✗ ✘
      std::cout<<" recentMatchLength="
	       <<recentMatchLength<<std::endl;
      std::cout.write(bytes, 8);
      std::cout<<std::endl<<std::string(jump, ' ');
      std::cout.write(bytes, 8);
      std::cout<<" jump="<<jump<<std::endl;
#endif      
      bool stopHere = false;
      const int knownExactlyCount = recentMatchLength - jump;
      if (knownExactlyCount < 0)
      { // We don't have any information about the file.  We can't even say
	// that one of the bytes ≠ something.
	stopHere = true;
#ifdef __UNIT_TEST_JumpBackSummary__
	std::cout<<std::string(jump, ' ')<<"????????"<<std::endl;
#endif
      }
      else if ((recentMatchLength < 8)
	       && (bytes[recentMatchLength] == bytes[knownExactlyCount]))
      { // This is the position where the match failed.  We don't know for
	// certain what is in the file here.  All we know for certain is
	// one value that isn't at that position in the file.  After jumping
	// forward jump positions, that byte of the file will still be compared
	// the the same byte.  The same byte exists in both places in the
	// search pattern, so if it failed before the jump it will fail again.
	// In short, we will fail at the same byte of the file (or sooner)
	// but we've advanced the file pointer, so the best possible match
	// would be even shorter than this one.  Don't even bother to...
	stopHere = false;
#ifdef __UNIT_TEST_JumpBackSummary__
	std::cout<<std::string(recentMatchLength, ' ')<<"≠"<<std::endl;
#endif
      }
      else
      { // We know what the next N bytes in the file are.  If they match our
	// pattern, assume that more bytes might match at run time.  This might
	// be the start of an interesting pattern, so start checking from here.
	stopHere = !memcmp(bytes, bytes + jump, knownExactlyCount);
#ifdef __UNIT_TEST_JumpBackSummary__
	if (!stopHere)
	{
	  for (int i = 0; i < recentMatchLength; i++)
	    if (i < jump)
	      std::cout<<' ';
	    else if (bytes[i] == bytes[i - jump])
	      std::cout<<'.';
	    else
	      std::cout<<"≠";
	  std::cout<<std::endl;
	}
#endif
      }
      if (stopHere)
      {
#ifdef __UNIT_TEST_JumpBackSummary__
        std::cout<<"winner:   _howFar["<<recentMatchLength<<"] = "<<jump
		 <<std::endl;
#endif
	_howFar[recentMatchLength] = jump;
	break;
      }
    }
  }
}


#ifdef __UNIT_TEST_JumpBackSummary__

bool simpleAscii(std::string const &s)
{
  for (char c : s)
    if ((c <= ' ') && (c >= 127))
      return false;
  return true;
}

int main(int argc, char **argv)
{
  if (argc == 1)
  {
    std::cerr<<"Syntax:  "<<argv[0]<<" test_string [test_string...]"
	     <<std::endl;
    return 2;
  }
  for (argv++; *argv; argv++)
  {
    std::string base(*argv);
    if (!simpleAscii(base))
      std::cerr<<"Skipping invalid string:  "<<base<<std::endl;
    if (base.length() < 8)
      base.insert(0, 8-base.length(), '?');
    for (size_t offset = 8; offset <= base.length(); offset++)
      // TODO Should we REALLY be pointing just past the end instead of
      // pointing to the beginning?
      JumpBackSummary(base.c_str()+offset);
  }
  
}

#endif
