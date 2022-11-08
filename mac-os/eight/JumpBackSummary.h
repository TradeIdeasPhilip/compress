#ifndef __JumpBackSummary_h_
#define __JumpBackSummary_h_

#include <stdint.h>


/* This is an optimization meant to work with EightShared.[Ch].
 *
 * Currently the encoder and decoder each have 2 main loops.  The outer loop
 * iterates one byte at a time through the entire file, encoding or decoding
 * that byte.  We always start by saving the context immediately before that
 * byte.  Then we run an inner loop.  The inner loop looks at history one
 * byte at a time and compares that to the context.
 *
 * The idea is to save some work.  This is based on an old search algorithm.
 * (I'm trying to remember the name.)  The idea of the original algorithm is
 * to do a little more work when you first see the search pattern, then skip
 * ahead quickly through the file rather than comparing every single byte to
 * the pattern.
 *
 * Example:  The search pattern is "Hi my name is Philip".  The file starts
 * with "Hi my name is 𝐧𝐨𝐭 Philip".  We start the outer loop by pointing to
 * the beginning of the file.  We start the inner loop by pointing to the
 * beginning of the search string.  We find the first 14 characters matched
 * perfectly, then "𝐧𝐨𝐭 Philip" did not match "Philip".  We jump back to the
 * top of the outer loop.  Naïvely we would add 1 to the file pointer and
 * try to compare "i my name is 𝐧𝐨𝐭 Philip" to the search string.
 *
 * But we can be smarter than that.  We know we made it 14 letters into the
 * search string.  Letters 2 - 14 of the search string do not match letter
 * one of the search string.  So with a little smarts we can add 14 to the
 * pointer, rather than 1.  When we first start the search we build a small
 * array.  Any time we try to match the search string to a part of the file,
 * we know how far we got before the first mismatch.  We use that as an index
 * into the array so we know how far to advance the file pointer.
 *
 * Note:  This search algorithm can actually get faster as you make the
 * search string longer!
 *
 * This is not a perfect match for what we are doing.  We're not looking for
 * just perfect matches.  However, what if we could quickly skip over all
 * 0 length matches?  We might not want to throw those away.  But it would
 * be easy to keep track of frequency of individual bytes (with no context) in
 * a different manner.
 *
 * What if we have a match of 2 bytes and we know that advancing the file
 * pointer by 1 will mean that the next match will be a 1 byte match?  In
 * the original search algorithm a 1 byte match is a failure so we'd definitely
 * skip it.  However, in our compression program we are looking at all
 * of history and just weighting it by the length of the match.  Should we
 * change that?  What if any time we get to that case we just ignore the
 * shorter match?  We always want to give more weight to the larger matches
 * than to the smaller ones.  As long as we are consistent between the
 * compressor and the decompressor.  The question is how much this would
 * affect the quality and speed of our algorithm.
 */
class JumpBackSummary
{
private:
  uint8_t _howFar[9];
public:
  // p points to the end of the buffer.  So we are looking at the 8 bytes
  // before p.  We do not look at *p.  This is consistent with
  // HistorySummary::getContext().
  JumpBackSummary(char const *p);
  uint8_t howFar(int matchLength) const { return _howFar[matchLength]; }
};


#endif
