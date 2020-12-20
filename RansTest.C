#include <assert.h>
#include <iostream>
#include <string>
#include <map>

#include "rans64.h"

// g++ -ggdb -std=c++0x RansTest.C

// This is a simple proof of concept / hello world to show we can use rans64.h.

const int bufferSize = 4096;
uint32_t buffer[bufferSize];
uint32_t *ptr, *lastReportedPtr;
Rans64State state;

int symbolCount;

struct SymbolInfo
{
  uint32_t start;
  uint32_t freq;
};

std::map< char, SymbolInfo > allSymbols =
  { {'A', {0, 10}}, {'B', {10, 3}}, {'C', {13, 2}}, {'D', {15, 1}}};

uint32_t scale_bits = 4; // 0 - 15

void reset()
{
  symbolCount = 0;
  lastReportedPtr = ptr = buffer+bufferSize;
  Rans64EncInit(&state);
  std::cout<<"Resetting."<<std::endl;
}

void showPtr()
{
  if (lastReportedPtr != ptr)
  {
    std::cout<<"ptr moved ";
    if (lastReportedPtr < ptr)
      std::cout<<(ptr-lastReportedPtr)<<" to the left.";
    else
      std::cout<<(lastReportedPtr-ptr)<<" to the right.";
    std::cout<<"  "<<(buffer + bufferSize - ptr)<< " words used."<<std::endl;
    lastReportedPtr = ptr;
  }
}

void add(SymbolInfo const &symbolInfo)
{
  Rans64EncPut(&state, &ptr, symbolInfo.start, symbolInfo.freq, scale_bits);
  showPtr();
  symbolCount++;
}

void add(char ch)
{
  const auto it = allSymbols.find(ch);
  if (it != allSymbols.end())
  {
    std::cout<<"Adding "<<ch<<std::endl;
    add(it->second);
  }
}

void add(std::string const &str)
{
  for (char ch : str)
    add(ch);
}

void decode()
{
  Rans64EncFlush(&state, &ptr);
  std::cout<<"Flush."<<std::endl;
  showPtr();
  Rans64DecInit(&state, &ptr);
  std::cout<<"Init decoder."<<std::endl;
  showPtr();
  for (; symbolCount > 0; symbolCount--)
  {
    uint32_t freq = Rans64DecGet(&state, scale_bits);
    bool found = false;
    for (auto &kvp : allSymbols)
    {
      SymbolInfo &symbolInfo = kvp.second;
      if ((freq >= symbolInfo.start)
	  && (freq < symbolInfo.start + symbolInfo.freq))
      {
	std::cout<<"Found:  "<<kvp.first<<std::endl;
	Rans64DecAdvance(&state, &ptr, symbolInfo.start, symbolInfo.freq,
			 scale_bits);
	showPtr();
	found = true;
	break;
      }
    }
    assert(found);
  }
  reset();
}

int main(int, char**)
{
  reset();
  while (true)
  {
    std::cout<<"?  ";
    std::string line;
    std::getline(std::cin, line);
    if (line.empty() || !std::cin) return 0;
    if (line == "reset")
      reset();
    else if (line == "decode")
      decode();
    else
      add(line);
  }
}
