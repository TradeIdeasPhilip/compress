#include <iostream>
#include <bitset>
#include <stdint.h>

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
 * How do we weight the goodness of the match?  If we see a byte and no
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
 * we'd have a make a second pass through the file.  The decompressor needs to
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

int main(int, char **)
{
  int64_t items[] = {0, 1, 2, 4, 8, 16, 32, 0x10000L, 0x100000000L,
		     0x1FFFFFFFFL, 0x1FFFFFFFFFFFFFFFL, 0x5FFFFFFFFFFFFFFFL};
  for (const int64_t item : items)
  {
    std::cout<<__builtin_clzl(item)<<" "<<std::bitset<64>(item)<<" "<<item<<std::endl;
  }
  // When I set -O4 __builtin_clzl(0) returns 64.
  // When I set -O0 __builtin_clzl(0) returns 63.
  // 64 is the answer I want!
  // Seems like I read something like this once, but I can't find it now.
  // Like this is common, but not 100% portable‽
}
