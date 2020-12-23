#include <assert.h>
#include <iostream>
#include <iomanip>
#include <stdint.h>
#include <cmath>

#include "File.h"

// g++ -o eight -O4 -ggdb -std=c++0x -Wall Eight.C File.C


/* This was inspired by Analyze3.C, but this is simpler and more brute-forcy.
 * 
 * We are not creating any tables filled with strings.  More like LZ77 we
 * are going to walk through a recent section of the file looking for matching
 * strings.
 *
 * Keeping with the spirit of Analyze3.C we are outputting one byte at a time
 * to the rANS encoder.  To encode a byte, we start by looking at the last few
 * bytes before that byte.  And then we ask if we've seen these same sequence
 * of bytes before in recent memory.  If so, what byte followed that sequence
 * of bytes?  Use this history to make predictions about what byte is coming
 * next for the rANS encoder.
 *
 * Of course, we might be looking at a byte that just doesn't exist in our
 * recent history.  We need a special code to say not found, and then to give
 * us the value directly.  We'll keep track of how often we need this, only
 * in this file, weighted more for more recent data, and use that to feed
 * the rANS encoder.  
 *
 * How tricky should we get describing that byte?  Let's say the next byte is
 * a Q.  And lets say we're looking back at the last 10,000 bytes.  And we
 * don't see a Q anywhere in those bytes.  We send the special code to say
 * new byte.  Then, do we send all 8 bits in the obvious way to describe that
 * Q?  We've already accumulated a lot of data.  We know that L, A, P, 9, and
 * a bunch of other bytes WERE available in that 10k buffer.  So we know that
 * none of those would appear here.  So we could encode the Q more efficiently.
 * But that means that the decoder will also have to check the last 10,000
 * bytes to see what's missing.  Maybe faster if we see the LITERAL command
 * and we skip looking at history.  Of course, most of the time we will have
 * to look at history, so maybe that's not out of scope.  Either way, I don't
 * expect this to happen a lot, and the first few times there isn't much you
 * could gain, so maybe I'm over-thinking this.
 *
 * We will look at the last 8 bytes of context because that's fast and easy to
 * do.  If the next character we want to encode is at location ch, then look
 * at all of the characters starting at ch-8 and ending before ch for context.
 * walk through memory one byte at a time.
 *
 * Start by loading *(int64_t *)(ch-8) into a register.  Then walk through
 * memory one byte at a time comparing older history to this most recent
 * context.  Use __builtin_clzl() / 8 to see how many bytes of history match.
 * Have an array with one number for each value between 0 and 255, initialized
 * to all 0s.  Each time you see a byte in our history, update that entry in
 * the array.  Add a small number to that array entry based on how good the
 * match is.
 * 
 * How do we weight the quality of the match?  If we see a byte and no
 * context matches, we should still add at least 1.  If we see a byte and all
 * of the context matches, we should add a value much greater than 1.  I'm not
 * sure how to weight that.  weight = bytes of context + 1?
 * weight = 2 ^ (bytes of context)?
 * weight = (bytes of context) * 5 + 1?
 * Analyze3 looked at similar statistics and found in some files the longer
 * matches were much more important than the short matches, but in other files
 * the difference wasn't as big.  It's tempting to look at recent history
 * to help us, but I'm not sure how.
 *
 * The basic statistics are easy to capture.  An array from 0 to 8 of pairs
 * of numbers.  For each number of bytes of context, how many times did we
 * match exactly this number of bytes, and how many of those times did the
 * next character match.  Prime them all to say 1 of 2 or something like
 * that.  For the compressor this is very little effort.  For the decompressor
 * we'd have to make a second pass through the file.  The decompressor needs to
 * review all of the possible matches before it knows the next value.
 *
 * Possible optimization:  You don't have to walk through the history one
 * byte at a time.  I can't remember the name of the algorithm, but there are
 * tricks to jump back further, faster.
 *
 * For example, lets say the immediate context is "12345678".  We do some
 * one time calculations here, before walking through memory.  At some point
 * we see a match of length 7.  So we were looking at "?2345678" where ? is
 * something other than a "1".  There is no point in backing up one byte.
 * That would have to return 0 because we will be looking at "??234567" and
 * the "7" at the end of this chunk of history could never match the "8" we
 * are looking for.  The table we create at the beginning would say how far
 * to jump back based on how good the last match was.
 *
 * Hmmm.  That's a clever way to skip a lot of things where there were 0
 * bytes in common.  But the notes above say we still want to look at those
 * cases.  If we don't we might miss some bytes and have to emit a raw byte.
 * Maybe that's not terrible.  The byte was used, but not in this context.
 * That will definitely happen, especially at the beginning.  But will it
 * happen enough that I should care?
 * 
 * We probably won't even skip over all of the no context items.  For example
 * if you had a match of 0, you would back up one space and try again.  Or if
 * you had a match of 8, and the context was "12345678", you would back up 8
 * spaces and try again.  Either way you have no idea what comes next, and it
 * might be a match of 0.  So we're just choosing to skip some of these.  As
 * long and the encoder and decoder are consistent, it will work.
 *
 * Optimization:  When compressing the data, we don't really need an array
 * of 256 numbers.  We only need 3 numbers.  What's the weighted total of all
 * bytes before the one we want to encode?  What's the weighed total for the
 * byte we intend to encode?  What's the weighted total for the bytes after
 * the one we want to encode?  The decompressor will need the full array.
 */

// The input is the probability of something happening.  The output is the
// cost in bits to represent this with an ideal entropy encoder.  We use this
// all over for prototyping.  Just ask for the cost, don't actually bother to
// do the encoding.
double pCostInBits(double ratio)
{
  return -std::log2(ratio);
}

int simpleCopyCount = 0;

// The first few always get copied as is.  Until our engine gets primed.
// These should cost 8 bits each.
void simpleCopy(char ch)
{
  simpleCopyCount++;
}

int addNewByteCount = 0;

// The next byte is not available by referencing a recent byte.  So we send
// a status command saying that this is a new byte, followed by the byte
// itself.
void addNewByte(char ch)
{
  addNewByteCount++;
}

int addReferenedByteCount = 0;

// This is the price of storing which byte we will send.  Measured in bits.
double addReferencedByteCost = 0.0;

// Ideally these would come in as integers.  That avoids round off errors
// and ensures consistency.  However, for testing purposes, this is good
// enough.
void addReferencedByte(double before, double match, double after)
{
  addReferenedByteCount++;
  const double probabilityOfMatch = match / (before + match + after);
  addReferencedByteCost += pCostInBits(probabilityOfMatch);
}

// This is the alternative to addNewByte().  We send a control signal to say
// we are referencing something we've seen recently.  before, match, and
// after correspond to the probability of selecting a byte before the
// actual byte, the probability of selecting the actual byte, and the
// probability of selecting a byte after the actual byte.  We divide by
// the total to put this on a scale from 0 to 1.  This is exactly what we
// need to send this byte to the entropy encoder.
void addReferencedByte(uint32_t before, uint32_t match, uint32_t after)
{
  addReferencedByte((double)before, (double)match, (double)after);
}

int matchingByteCount(int64_t a, int64_t b)
{
  int64_t difference = a ^ b;
  if (difference == 0)
    // What does clzl(0) return?  It depends if you have optimization on or
    // off!  This is the best possible case, a perfect match.
    return 8;
  return __builtin_clzl(difference) / 8;
}

// The index is the number of bytes of context.  These tell us how common and
// how accurate each of our predictions are.
int64_t contextMatchCount[9];
int64_t predictionMatchCount[9];

// These are a subset of the previous two lines.  Consider each byte we are
// trying to predict seperataly.  For each byte, what was the longest
// context match we got?  Now only store the statistics for that match.
int64_t contextMatchCountBest[9];
int64_t predictionMatchCountBest[9];

bool isIntelByteOrder()
{
  const uint64_t number = 0x0102030405060708lu;
  char const *asByte = reinterpret_cast< char const * >(&number);
  return (asByte[0] == 8) && (asByte[7] == 1);
}

int main(int argc, char **argv)
{ // For simplicity just assume this.  It shouldn't be hard to fix if the
  // byte order changes.  Instead of counting leading zeros we would count
  // trailing zeros.  See __builtin_clzl().
  assert(isIntelByteOrder());
  
  if (argc != 2)
  {
    std::cerr<<"syntax:  "<<argv[0]<<" file_to_compress"<<std::endl;
    return 1;
  }

  // This should be tunable.  And probably we need to record the value in
  // to file so the reader will be able to run the identical algorithm.
  const int maxBufferSize = 5000;

  File file(argv[1]);
  if (!file.valid())
  {
    std::cerr<<file.errorMessage()<<std::endl;
    return 2;
  }

  // We often point back 8 bytes.  But what do we do at the beginning of the
  // file?  We'd be pointing to something random, possibly causing a
  // segmentation violation.  So we have a copy of the first 8 bytes of the
  // file and we padded them with 8 bytes of 0's.
  // Why all 0's?  As long as we have this, it's tempting to fill it with
  // the 8 most popular english letters.  Or "#include".  As long as we are
  // consistent it should not matter.
  std::string earlyReferences(8, '\0'); // 8 0's.
  earlyReferences.append(file.begin(), std::min(file.size(), 8lu));

  if (file.size() > 0)
    simpleCopy(*file.begin());
  for (char const *toEncode = file.begin() + 1;
       toEncode < file.end();
       toEncode++)
  {
    const auto getContext = [&file, &earlyReferences](char const *ptr) {
      char const *context;
      const intptr_t index = ptr - file.begin();
      if (index >= 8)
	// Try to go back 8 bytes in the file to get some context.
	context = ptr - 8;
      else
	// We made a copy of the first 8 bytes in the file, and we padded them
	// with 8 null bytes, so we'd always have context.
	// getContext(file.begin()) should return all 0's.
	// getContext(file.begin()+1) shoud return 7 0's followed by
	// *file.begin().
	context = (&earlyReferences[0]) + index;
      return *reinterpret_cast< int64_t const * >(context);
    };
    const int64_t initialContext = getContext(toEncode);
    const auto index = toEncode - file.begin();
    char const *const start =
      // Go back maxBufferSize bytes.  If that's past the beginning of the
      // file, then go to the beginning of the file.  Be careful with the
      // unsigned arithmetic!
      (index >= maxBufferSize)?(toEncode - maxBufferSize):file.begin();
    uint32_t scoreBefore = 0;
    uint32_t scoreMatch = 0;
    uint32_t scoreAfter = 0;
    int longestContextMatch = 0;
    uint32_t tiedForLongest = 0;
    uint32_t successForLongest = 0;
    for (char const *compareTo = start; compareTo < toEncode; compareTo++)
    {
      auto const count =
	matchingByteCount(initialContext, getContext(compareTo));
      auto const score = 1 << count;
      if (*compareTo < *toEncode)
	scoreBefore += score;
      else if (*compareTo == *toEncode)
	scoreMatch += score;
      else
	scoreAfter += score;
      contextMatchCount[count]++;
      if (*compareTo == *toEncode)      
	predictionMatchCount[count]++;
      if (count > longestContextMatch)
      {
	longestContextMatch = count;
	tiedForLongest = 0;
	successForLongest = 0;
      }
      if (count == longestContextMatch)
      {
	tiedForLongest++;
	if (*compareTo == *toEncode)
	  successForLongest++;
      }
    }
    contextMatchCountBest[longestContextMatch] += tiedForLongest;
    predictionMatchCountBest[longestContextMatch] += successForLongest;
    if (successForLongest == 0)
      // Not found!
      addNewByte(*toEncode);
    else
      addReferencedByte(/*scoreBefore*/ (tiedForLongest - successForLongest),
			/*scoreMatch*/ successForLongest, /*scoreAfter*/ 0);
  }
  
  std::cout<<"Filename:  "<<argv[1]<<std::endl
	   <<"File size:  "<<file.size()<<std::endl
	   <<"simpleCopyCount:  "<<simpleCopyCount<<std::endl
	   <<"addNewByteCount:  "<<addNewByteCount<<std::endl
	   <<"addReferenedByteCount:  "<<addReferenedByteCount<<std::endl
	   <<"addReferencedByteCost:  "<<(addReferencedByteCost/8)<<" bytes"
	   <<std::endl;
  const double newByteCost =
    pCostInBits(addNewByteCount /
		(double)(addNewByteCount + addReferenedByteCount)) / 8;
  const double referenceByteCost =
    pCostInBits(addReferenedByteCount /
		(double)(addNewByteCount + addReferenedByteCount)) / 8;
  const double byteTypeCost =
    addNewByteCount * newByteCost + addReferenedByteCount * referenceByteCost;
  std::cout<<"byte type cost:  "<<addNewByteCount<<" * "<<newByteCost<<" + "
	   <<addReferenedByteCount<<" * "<<referenceByteCost<<" = "
	   <<byteTypeCost<<std::endl;
  const double outputFileSize =
    simpleCopyCount + addNewByteCount + byteTypeCost
    + (addReferencedByteCost/8);
  std::cout<<"output file size:  "<<simpleCopyCount<<" + "<<addNewByteCount
	   <<" + "<<byteTypeCost<<" + "<<(addReferencedByteCost/8)
	   <<" = "<<outputFileSize<<std::endl
	   <<"savings:  "
	   <<((file.size() - outputFileSize) * 100 / file.size())
	   <<"%"<<std::endl;

  std::cout<<std::endl
	   <<"Context      Context      Prediction     Success    Best ctxt    Success"
	   <<std::endl
	   <<"length     match count    match count     rate      match cnt     rate"
	   <<std::endl;
  for (int count = 0; count <= 8; count++)
  {
    std::cout<<std::setw(7)<<count
	     <<std::setw(15)<<contextMatchCount[count]
	     <<std::setw(15)<<predictionMatchCount[count];
    if (contextMatchCount[count])
      std::cout<<std::setw(10)
	       <<(predictionMatchCount[count]*100.0/contextMatchCount[count])
	       <<'%';
    else
      std::cout<<std::string(11, ' ');
    std::cout<<std::setw(13)<<contextMatchCountBest[count];
    if (contextMatchCountBest[count])
      std::cout<<std::setw(10)
	       <<(predictionMatchCountBest[count]*100.0
		  /contextMatchCountBest[count])
	       <<'%';
    std::cout<<std::endl;
  }
  /* Sample output below.
   * As expected, we are getting really good results from long matches.  But
   * the shorter matches are so numerous as to overwhelm the long ones.
   * We need to give more weight to the later matches even when they are
   * rare.
   * [phil@joey-mousepad ~/compress]$ ./eight test_data/c*.js.map
   * Filename:  test_data/cvs_nq_update.js.map
   * File size:  18940
   * simpleCopyCount:  1
   * addNewByteCount:  4943
   * addReferenedByteCount:  22564
   * addReferencedByteCost:  1245.27 bytes
   * byte type cost:  4943 * 0.309542 + 22564 * 0.035722 = 2336.1
   * output file size:  1 + 4943 + 2336.1 + 1245.27 = 8525.37
   * savings:  69.0077%
   * 
   * Context      Context      Prediction     Success    Best ctxt    Success
   * length     match count    match count     rate      match cnt     rate
   *       0       67263713       10907542   16.2161%       185915   11.3858%
   *       1       10907542        2580669   23.6595%        52670   30.5886%
   *       2        2580669         369020   14.2994%        33315   43.5269%
   *       3         369020         327533   88.7575%        17690   80.1639%
   *       4         327533         318096   97.1188%        14361   90.5647%
   *       5         318096         211356   66.4441%        13264   78.4831%
   *       6         211356          39136   18.5166%        12434   32.2664%
   *       7          39136          34030   86.9532%         7719    80.101%
   *       8         180435         146405     81.14%       180435     81.14%
   * [phil@joey-mousepad ~/compress]$ ./eight test_data/c*.ts
   * Filename:  test_data/cvs_nq_update.ts
   * File size:  27508
   * simpleCopyCount:  1
   * addNewByteCount:  4943
   * addReferenedByteCount:  22564
   * addReferencedByteCost:  1245.27 bytes
   * byte type cost:  4943 * 0.309542 + 22564 * 0.035722 = 2336.1
   * output file size:  1 + 4943 + 2336.1 + 1245.27 = 8525.37
   * savings:  69.0077%
   * 
   * Context      Context      Prediction     Success    Best ctxt    Success
   * length     match count    match count     rate      match cnt     rate
   *       0      116532017        6227058   5.34365%       288104   4.31025%
   *       1        6227058         741731   11.9114%       152112   11.5829%
   *       2         741684         383240   51.6716%        48138   27.0846%
   *       3         383221         297063   77.5174%        16956   52.4357%
   *       4         297013         225238   75.8344%         9809   69.0386%
   *       5         225238         182114    80.854%         7360   70.4755%
   *       6         182114         133555   73.3359%         5484   73.6506%
   *       7         133555         102413   76.6823%         4280   80.5841%
   *       8         315600         213187   67.5497%       315600   67.5497%
   * [phil@joey-mousepad ~/compress]$ ./eight test_data/Analyze3.C
   * Filename:  test_data/Analyze3.C
   * File size:  10873
   * simpleCopyCount:  1
   * addNewByteCount:  2521
   * addReferenedByteCount:  8351
   * addReferencedByteCost:  561.922 bytes
   * byte type cost:  2521 * 0.263569 + 8351 * 0.0475746 = 1061.75
   * output file size:  1 + 2521 + 1061.75 + 561.922 = 4145.67
   * savings:  61.8719%
   * 
   * Context      Context      Prediction     Success    Best ctxt    Success
   * length     match count    match count     rate      match cnt     rate
   *       0       39508462        1958528   4.95724%       116731   2.78504%
   *       1        1958372         219250   11.1955%        55438   12.5942%
   *       2         219234          75954   34.6452%        17662   28.4962%
   *       3          75953          36680    48.293%         6837    56.282%
   *       4          36680          20108   54.8201%         4584    65.096%
   *       5          20108           9673   48.1052%         3289   65.6126%
   *       6           9673           5909   61.0876%         2352   65.9014%
   *       7           5909           3736   63.2256%         1691   68.0071%
   *       8          28109          24373   86.7089%        28109   86.7089%
   * [phil@joey-mousepad ~/compress]$
   */
}
