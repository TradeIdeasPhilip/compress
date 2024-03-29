#include <assert.h>
#include <iostream>
#include <iomanip>
#include <stdint.h>
#include <cmath>
#include <string.h>

#include "../shared/File.h"
#include "../shared/RansBlockWriter.h"
#include "EightShared.h"


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

// For simplicity make no assumptions about ch.  It's just a number between
// 0 and 255.
void simpleCopy(unsigned char ch, RansBlockWriter &writer)
{
  writer.write(RansRange(ch, 1, 256));
}

BoolCounter algorithmCounter;

void recordAlgorithm(bool byReference, RansBlockWriter &writer)
{
  writer.write(algorithmCounter.getRange(byReference));
  algorithmCounter.increment(byReference);
}

int addNewByteCount = 0;

// The next byte is not available by referencing a recent byte.  So we send
// a status command saying that this is a new byte, followed by the byte
// itself.
void addNewByte(char ch, RansBlockWriter &writer)
{
  addNewByteCount++;
  recordAlgorithm(false, writer);
  simpleCopy(ch, writer);  
}

int addReferencedByteCount = 0;

// This is the price of storing which byte we will send.  Measured in bits.
double addReferencedByteCost = 0.0;

// This is the alternative to addNewByte().  We send a control signal to say
// we are referencing something we've seen recently.  Then we send range,
// which describes which byte we are referencing.
void addReferencedByte(RansRange range, RansBlockWriter &writer)
{
  recordAlgorithm(true, writer);
  writer.write(range);
  addReferencedByteCount++;
  addReferencedByteCost += range.idealCost();
}

// The index is the number of bytes of context.  These tell us how common and
// how accurate each of our predictions are.
int64_t contextMatchCount[9];
int64_t predictionMatchCount[9];

int main(int argc, char **argv)
{ // For simplicity just assume this.  It shouldn't be hard to fix if the
  // byte order changes.  Instead of counting leading zeros we would count
  // trailing zeros.  See __builtin_clzl().
  //
  // What about the rANS library?  I think we'd want to just swap every pair of
  // bytes.  That library and our custom code *always* read and write 32 bits
  // at a time.
  assert(isIntelByteOrder());
  
  if (argc != 2)
  {
    std::cerr<<"syntax:  "<<argv[0]<<" file_to_compress"<<std::endl;
    return 1;
  }

  // Longer would also work, but I know my intent was exactly 8 bytes.
  assert(preloadContents.length() == 8);
  
  File file(argv[1], preloadContents);
  if (!file.valid())
  {
    std::cerr<<file.errorMessage()<<std::endl;
    return 2;
  }

  RansBlockWriter writer(argv[1] + std::string(".μ8"));

  TopLevel topLevel;
  
  for (char const *toEncode = file.begin();
       toEncode < file.end();
       toEncode++)
  {
    topLevel.encode(*toEncode,
		    HistorySummary(file.preambleBegin(), toEncode),
		    writer);
  }
  return 0;

  /*
  if (file.size() > 0)
    simpleCopy(*file.begin(), writer);
  for (char const *toEncode = file.begin() + 1;
       toEncode < file.end();
       toEncode++)
  {
    const int64_t initialContext = getContext(toEncode);
    const auto index = toEncode - file.begin();
    char const *const start =
      // Go back maxBufferSize bytes.  If that's past the beginning of the
      // file, then go to the beginning of the file.  Be careful with the
      // unsigned arithmetic!
      (index >= maxBufferSize)?(toEncode - maxBufferSize):file.begin();

    // contextMatchCount and predictionMatchCount give us a historical
    // sense of how good each level is.  Those are globals and we can report
    // a small number of statistics on that subject.
    //
    // Now we are storing similar data, but only for this current byte.
    // We are taking a lot of liberties here.  The encoder would need at least
    // 3 bins for each level:  before the match, same as the match, and after
    // the match, to work with addReferencedByte().  (The decoder will need
    // 256 bins per level!)  But we're still just experimenting, and this will
    // give us the statistics we need.
    int64_t currentContextMatchCount[9];
    int64_t currentPredictionMatchCount[9];
    memset(currentContextMatchCount, 0, sizeof(currentContextMatchCount)); 
    memset(currentPredictionMatchCount, 0,
	   sizeof(currentPredictionMatchCount)); 
    for (char const *compareTo = start; compareTo < toEncode; compareTo++)
    {
      auto const count =
	matchingByteCount(initialContext, getContext(compareTo));
      contextMatchCount[count]++;
      currentContextMatchCount[count]++;
      if (*compareTo == *toEncode)
      {
	predictionMatchCount[count]++;
	currentPredictionMatchCount[count]++;
      }
    }

    // We're taking more liberties here.  The math should really be done in
    // integers to be repeatable and portable.  But for our statistics doubles
    // work perfectly and are more convenient.
    //
    // We are not currently using contextMatchCount or predictionMatchCount.
    // If this algorithm works We should circle back and use those to tweak
    // our weights.  We print those for statistics, but only for us to read.
    //
    // Each of the 9 levels has a unique weight.  0 context has a total weight
    // of 2^0 = 1, 1 bytes of context has a weight of 2^2 = 2, 8 bytes of
    // context has a weight of 2^8 = 256.
    //
    // If level has 0 entries in it, then we change its weight to 0.
    //
    // Within a level each match gets the same weight.
    //
    // The previous attempt gave 100% of the weight to the highest level
    // with at least one entry.  The attempt before that gave equal weight to
    // every entry, regardless of the level.  The current algorithm is a
    // compromise between those extremes.
    double currentWinningScore = 0;
    double currentPossibleScore = 0;
    for (int level = 0; level <= 8; level++)
    {
      if (currentContextMatchCount[level])
      {
	const double weight = 1<<level;
	currentPossibleScore += weight;
	currentWinningScore += currentPredictionMatchCount[level] * weight
	  / currentContextMatchCount[level];
      }
    }
    if (currentWinningScore == 0)
      // Not found!
      addNewByte(*toEncode, writer);
    else
    { // chanceOfSuccess says the probability that we assigned to the actual
      // byte we are trying to encode, before we actually saw this byte.
      //
      // If we've seen nothing but 0's so far, and we see another 0, then
      // chanceOfSuccess will be 100%.  (1.0)  Once we say that this is a
      // reference to a previous byte, it will take exactly 0 bits to say
      // which byte we are talking about.
      //
      // A more realistic example:  All of the 8 byte context matches
      // pointed to the same next byte, and that was the correct byte.
      // Now chanceOfSuccess will be at least 50%.  So it will take 1 bit
      // or less to say which byte we are talking about.
      //
      // If we never saw this byte before chanceOfSuccess would be 0.  We
      // filtered that case out before we got here.  Otherwise it would take
      // infinitely many bits to represent this byte!
      const double chanceOfSuccess = currentWinningScore/currentPossibleScore;
      const int denominator = 1<<25;  // This is a hack!
      // TODO do this right!  This is enough to make sure the file size is
      // about what it should be, but not enough to actually decode the file.
      const RansRange range(0, (int)std::floor(denominator * chanceOfSuccess),
			    denominator);
      addReferencedByte(range, writer);
    }
  }

  const int simpleCopyCount = 1;
  std::cout<<"Filename:  "<<argv[1]<<std::endl
	   <<"File size:  "<<file.size()<<std::endl
         //<<"simpleCopyCount:  "<<simpleCopyCount<<std::endl
	   <<"addNewByteCount:  "<<addNewByteCount<<std::endl
	   <<"addReferencedByteCount:  "<<addReferencedByteCount<<std::endl
	   <<"addReferencedByteCost:  "<<(addReferencedByteCost/8)<<" bytes"
	   <<std::endl;
  const double newByteCost =
    // I'm effectively adding one yes and one no just to make sure we never
    // get a divided by zero.  However, that trick didn't do much.  When I
    // try to compress a large file of all zeros, I see
    //   byte type cost:  0 * 2.08048 + 102399 * -0 = 0
    //   output file size:  1 + 0 + 0 + 0 = 1
    //   savings:  99.999%
    // So take this estimate with a grain of salt.
    pCostInBits((addNewByteCount + 1) /
		(double)(addNewByteCount + addReferencedByteCount + 2)) / 8;
  const double referenceByteCost =
    pCostInBits(addReferencedByteCount /
		(double)(addNewByteCount + addReferencedByteCount)) / 8;
  const double byteTypeCost =
    addNewByteCount * newByteCost + addReferencedByteCount * referenceByteCost;
  std::cout<<"byte type cost:  "<<addNewByteCount<<" * "<<newByteCost<<" + "
	   <<addReferencedByteCount<<" * "<<referenceByteCost<<" = "
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
	   <<"Context      Context      Prediction     Success"
	   <<std::endl
	   <<"length     match count    match count     rate"
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
    std::cout<<std::endl;
  }
   */
}
