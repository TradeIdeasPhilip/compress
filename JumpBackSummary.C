#include "JumpBackSummary.h"


JumpBackSummary::JumpBackSummary(char const *p)
{
  uint8_t bytes[8];
  for (int i = 0; i < 8; i++)
    bytes[i] = p[-(1+i)];
  
  int matchLength = 0;
  // just finished examining:  ¿¿¿¿¿¿?a
  //         initial context:  .......Z
  // The only thing we know for certain is that Z does not equal a.  But a
  // is about to disappear never to be seen again.  We know nothing.  Go
  // back one byte and compare Z against the ?.
  _howFar[matchLength] = 1;
  
  matchLength = 1;
  if (bytes[0] == bytes[1])
  { // just finished examining:  ¿¿¿¿¿?aZ
    //         initial context:  ......ZZ
    // If we just pulled back by one we know we'd get a match of 0 next time
    // because we'd be comparing a to Z, and we just did that.
    _howFar[matchLength] = 2;
  }
  else 
  { // just finished examining:  ¿¿¿¿¿¿?Z
    //         initial context:  ......YZ
    // All we know is ? ≠ Y and Y ≠ Z.  So we know nothing.  Compare ? to Z.
    _howFar[matchLength] = 1;    
  }
  
  matchLength = 2;
  if (bytes[0] == bytes[1])
  {
    if (bytes[0] == bytes[2])
    { // just finished examining:  ¿¿¿¿?aZZ
      //         initial context:  .....ZZZ
      // If we go back 1 we know that we'll have a match of length 1 next
      // time.  The original algorithm was only looking for exact matches.
      // My algorithm is looking for matches of length 1 or greater.
      // We we have to try the match one byte back.  We could save a few
      // instructions by saying we know the answer will be 1, but that's
      // probably not worth the effort.
      _howFar[matchLength] = 1;
      // CONSIDER returning 3.  That will skip a match length of 0 and a
      // match length of 1, both of which are less than the current match
      // length.
    }
    else
    { // just finished examining:  ¿¿¿¿?aZZ
      //         initial context:  .....YZZ
      // If we go back 1 we will have a match length of at least 1.
      // But it might be bigger.  All we know is a ≠ Y and Y ≠ Z.
      // However, a might = z, and ? might = Y, and the match could keep
      // going.
      _howFar[matchLength] = 1;
    }
  }
  else
  {
    if (bytes[1] == bytes[2])
    { // just finished examining:  ¿¿¿¿?aYZ
      //         initial context:  .....YYZ
      // Y ≠ Z so we know going back 1 would lead to a match length of 0.
      // a might = Z.
      _howFar[matchLength] = 2;
    }
    else if (bytes[0] == bytes[2])
    { // just finished examining:  ¿¿¿¿?aYZ
      //         initial context:  .....ZYZ
      // Y ≠ Z so we know going back 1 would lead to a match length of 0.
      // a ≠ Z so we know going back 2 would lead to a match length of 0.
      // Go back 3 so we can compare the final Z in the initial context to ?.
      _howFar[matchLength] = 3;
    }
    else
    { // just finished examining:  ¿¿¿¿?aYZ
      //         initial context:  .....XYZ
      // Y ≠ Z so we know going back 1 would lead to a match length of 0.
      // a might = Z.
      _howFar[matchLength] = 2;
    }
  }
  // TODO We need to deal with match lengths 3 - 8.  I started listing all
  // possibilities out by hand.  I'm hoping to find a pattern so I can write
  // a shorter algorithm.
}
