# 4p Compression

4 phases or 4 possibilities.

We have different ways of looking at the data.  They all focus on looking back for similar strings in the preceding parts of the file.

## Long Strings
We start by looking for a string of 8 or more matching bytes.
We are limited to a sliding buffer, otherwise things get very slow very fast.
Imagine looking at the last 64k of bytes by default, fewer if the command line requests speed over compression ratio.

Why 8 or more?  zlib has a minimum size of 3 or 4 bytes, or something like that; it’s just not worth it to try to reuse a small string.  
We can do even better; we already know from other programs how to compress short strings well.
And if we can find a long string we have the potential for a huge gain, even with the simplest encoding scheme.
We need to worry about the really long strings if we ever hope to compete with zlib.

Why 8 in particular?
Because I can check if 8 bytes in a row match in a single instruction on a 64 bit machine!
So I can make the search as fast as possible.

## Medium Strings
If that doesn’t work we check if we’ve seen the next string of 4 bytes recently.
(For some reasonable value of 4!)

I had good luck with the algorithm in HashDown.C, but it clearly worked better is some cases than others.
That’s why I want to mix that hashing idea with other types of compression in this program.

I want to collect some data here, see how often this works well, and try to tune the parameters.
I have a maximum number of recent strings to save, as a parameter.
Think 257, 511, or something in that general range as suggested values.

I have a key type which includes exactly the numbers between 0 and the max value minus 1.
I have a map from the key type to a string of 4 bytes.
Some keys point to nothing.

Each time we process a byte, we look at the last four bytes, compute a hash of that and use modulo division to make the hash into a valid key.
And store the 4 byte string in the map, associated with the string’s key, possibly overwriting a previous 4 byre string.
If we are encoding and step 1 failed, then look at the next 4 bytes in the file and see if they exist in array.
We write something to the file to say if we found a match at this stage or not.
If we did find a match, write the key to the stream.

## Short Strings
If the first two phases failed, then we reuse an old trick.
We’ve got a few versions of this in other programs already.
Keep statistics like “After a ‘q’ there’s a 99.7% chance of seeing a ‘u’.”  We can easily extend that to include the chance of seeing any specific byte based on the previous two bytes.

## Any Strings
The previous phase might or might not be able to cover every possible byte.
If it doesn’t cover every byte, then we have a simple last resort.
We just spit out the byte that we want to see.

In the past we’ve sometimes optimized this part.
But in other programs, we just print the desired byte without any further processing.
This case is relatively rare and it would be better to have a simple catchall routine here which makes no assumptions about the current file or about the details of the previous three phases.  

# Current status

I have a prototype that looks at a file and does only the hashing step described in Medium Strings, along with part of the sliding window step described in Long Strings.

The compression results are not currently accurate.  
Some parts of the code only present notes to the output and don't record the cost yet.

The VS Code debugger works.

## Running it

Open the `mac-os` folder in VS Code, not the `4p` folder.
(That should change!)

Make sure you run from the debug menu on the left of the screen, not the debug icon you see in the top right corner when viewing a *.C file.


* Use the TERMINAL tab to view the result of the build step.
* Use the DEBUG CONSOLE tab to view the output of the program and the debugger.
* The OUTPUT tab is always empty.
* Ignore the PROBLEMS tab for now.  __TODO__: I need to fix the syntax checker.

## Conclusions

Interesting.
But I'm going to let this one sit.


I originally got excited because of some of the mega counter stuff and some of the claims in the early hashing from the previous project.
The comments said that these long "mega" strings were very rare.
Often you'd have no choice because you never seen anything with this hash code before.
Often there was only one byte matching the current hash code, and I was considering whether I needed or even wanted to keep more than one byte per hash in this table.
But when I looked at actual results, `4p.C` was usually filling the hash table.
There were some missing entries but not many.

And some with only one byte in them. 
But that's not even what we want. 
I was looking for hashes with at least two bytes, and the bytes were all the same.
There was definitely some of that, more than you'd expect just from randomness, but it wasn't outstanding.

A big part of the problem is that `eight.C` would only look back a certain amount. 
These hash algorithms were made to look back through the whole file. 
So that may be part of the reason why the results are so much different.
This hashing alternative is very useful because we can very precisely control the amount of memory that we use.
But it does lose that magic from the mega counters that gave those incredible results.

This leaves a couple of possibilities.
We can go back to the `eight.C` style where we're only looking at recent history.
Another thought would be to keep track of everything but to give a higher waiting to the last byte or to the most recent bytes.
It should be easy to run a test like that.
Show side by side what is the chance of it winning when there's only one byte or only one unique byte and it's the last byte.
Versus is the chance of winning when we look at all of the values.
So probably giving a heavy weight to the last one but let's give it a shot and see what happens.

The medium length string hashing algorithm was an interesting idea.
It gave very _consistent_ results for all the compression it could do. We could never take advantage of the entropy encoder.
We can never say these numbers are really cheap so we can repeat this one entry over and over really cheaply.
I never actually check the cost of turning the switch to say yes we should use the results of this test or no we should go on to the short length strings test.
I suspect that still would have shown some compression.
But I'm not convinced that it will be impressive considering it will be taking from the other types of compression. They're all aimed at the same entropy.

I've decided to take another look at `eight.C`.
The 4P project brought up some interesting ideas but maybe too many at once.
The end of the hash down project suggests some very specific things to go back into `eight.C`. `eight.C` was close to production. It makes more sense to start from there and merge in the long string support from 4P into that project.











