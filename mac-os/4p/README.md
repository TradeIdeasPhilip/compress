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

I have a prototype that looks at a file and does only the hashing step described in Medium Strings.
It cycles over different values for `hashBufferSize`, the number of hashed entries to save, and `hashEntrySize`, the size of each string that we save.

My compression results are not great, but they are impressive for something with such a small scope.
The question is how do these results compare to some of the other algorithms I've tried, with respect to strings of length 4-6 bytes.

I'm getting the best results with a buffer size near 4k and a collection of all 4, 5, and 6 byte strings all in the same buffer.

When I compressed `4p.C` (the source code) my algorithm removed 54.8295% of the size, compared to gzip removing 76.7% of the size.
```
processFileRange() fileSize=10146, hashBufferSize=4105, hashEntrySize=4-6, hashEntries=1491, simpleHashCodeCostInBits=17105, betterHashCodeCostInBits=14960.7, individualBytes=2245, totalCostInBytes=4583, savings=54.8295%
```

When I compressed `4p` (the compiled executable) my algorithm removed 61.3914% of the file size, compared to gzip removing 76.4%.

```
processFileRange() fileSize=45135, hashBufferSize=4105, hashEntrySize=4-6, hashEntries=6060, simpleHashCodeCostInBits=72720, betterHashCodeCostInBits=36609.1, individualBytes=10748, totalCostInBytes=17426, savings=61.3914%
```

If I only look at the parts of the file that were actually compressed, the source file gave me 70.41% savings and the object file gave me 80.58% savings.
The idea is that other parts of the file will use other compression methods.
If those methods are similarly effective, then we could get that type of savings for the entire file.

Notice that the betterHashCodeCostInBits (compressing the index numbers) saves over 50% in the binary file, but only about 12½% for the text file.