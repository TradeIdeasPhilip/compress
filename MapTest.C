#include <assert.h>
#include <string>
#include <map>
#include <bitset>
#include <iostream>

// g++ -o map_test -O4 -ggdb -std=c++14 MapTest.C

class BigString
{
private:
private:
  char const *_begin;
  char const *_end;
public:
  BigString(char const *begin, char const *end) : _begin(begin), _end(end)
  {
    assert(begin <= end);
  }
  BigString(std::string const &source) :
    BigString(source.c_str(), source.c_str() + source.length()) { }
  explicit operator std::string() const
  {
    std::cerr<<"Warning, copying "<<size()<<" bytes."<<std::endl;
    return std::string(_begin, _end);
  }
  size_t size() const
  {
    return _end - _begin;
  }
};

std::map< std::string, int > allPossible;
std::map< std::string, int > searchInHere;

int main(int, char **)
{
  for (int i = 0; i < 16; i++)
  {
    allPossible[std::bitset<4>(i).to_string()] = i;
    if ((i > 1) && (i < 14) && (i != 8))
    {
      searchInHere[std::bitset<4>(i).to_string()] = i;
    }    
  }

  int notFoundCount = 0;
  int equalCount = 0;
  int differentCount = 0;
  
  for (auto const &possible : allPossible)
  {
    const std::string keyAsStdString = possible.first;
    const BigString keyAsBigString = keyAsStdString;
    auto fromStdString = searchInHere.lower_bound(keyAsStdString);
    auto fromBigString = searchInHere.lower_bound(keyAsBigString);
    if (fromStdString != fromBigString)
    { // I don't want to spend a lot of time documenting this thing that should
      // never happen.  Instead wake the debugger or leave a core dump.
      abort();
    }
    if (fromStdString == searchInHere.end())
    {
      notFoundCount++;
    }
    else if (fromStdString->second == possible.second)
    {
      equalCount++;
    }
    else
    {
      differentCount++;
    }
  }

  // Just enough that we know the tests ran.  On success you see nothing else.
  std::cout<<"notFoundCount = "<<notFoundCount<<std::endl
	   <<"equalCount ="<<equalCount<<std::endl
	   <<"differentCount ="<<differentCount<<std::endl
	   <<"allPossible.count() = "<<allPossible.count()<<std::endl
	   <<"searchInHere.count() = "<<searchInHere.count()<<std::endl;
}
