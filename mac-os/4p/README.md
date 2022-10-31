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

__My compression results__ with hashBufferSize=4093 and hashEntrySize=3 __were surprising close to gzip__.
When I compressed `4p.C` (the source code) my algorithm removed 62.29% of the size, compared to gzip removing 62.8%.
Which I compressed `4p` (the compiled executable) my algorithm removed 80.73% of the size, compared to gzip removing 82.9% of the size.
My algorithm did better as `hashBufferSize` got bigger and as `hashEntrySize` got smaller.

My totalCostInBytes is a slight overestimate, as described in the comments in the code.
I don't think that will make a huge difference, so I stopped my estimate there.

I don't actually expect to use hashEntrySize=3 in the final version.
The idea is that strings of length 3 will be covered by the algorithm described in Short Strings.

Test results:
```
philipsmolen@Philips-MacBook-Air 4p % g++ -o 4p -O3 -ggdb -std=c++0x -Wall 4p.C ../shared/File.C ../shared/Misc.C
philipsmolen@Philips-MacBook-Air 4p % ./4p 4p.C                                                                  
File name: 4p.C
processFile() fileSize=3665, hashBufferSize=257, hashEntrySize=3, hashEntries=721, individualBytes=1502, totalCostInBytes=1781
processFile() fileSize=3665, hashBufferSize=257, hashEntrySize=4, hashEntries=455, individualBytes=1845, totalCostInBytes=2134
processFile() fileSize=3665, hashBufferSize=257, hashEntrySize=5, hashEntries=331, individualBytes=2010, totalCostInBytes=2304
processFile() fileSize=3665, hashBufferSize=257, hashEntrySize=6, hashEntries=250, individualBytes=2165, totalCostInBytes=2468
processFile() fileSize=3665, hashBufferSize=257, hashEntrySize=7, hashEntries=197, individualBytes=2286, totalCostInBytes=2598
processFile() fileSize=3665, hashBufferSize=509, hashEntrySize=3, hashEntries=786, individualBytes=1307, totalCostInBytes=1570
processFile() fileSize=3665, hashBufferSize=509, hashEntrySize=4, hashEntries=505, individualBytes=1645, totalCostInBytes=1915
processFile() fileSize=3665, hashBufferSize=509, hashEntrySize=5, hashEntries=372, individualBytes=1805, totalCostInBytes=2079
processFile() fileSize=3665, hashBufferSize=509, hashEntrySize=6, hashEntries=278, individualBytes=1997, totalCostInBytes=2283
processFile() fileSize=3665, hashBufferSize=509, hashEntrySize=7, hashEntries=211, individualBytes=2188, totalCostInBytes=2489
processFile() fileSize=3665, hashBufferSize=1021, hashEntrySize=3, hashEntries=823, individualBytes=1196, totalCostInBytes=1450
processFile() fileSize=3665, hashBufferSize=1021, hashEntrySize=4, hashEntries=542, individualBytes=1497, totalCostInBytes=1754
processFile() fileSize=3665, hashBufferSize=1021, hashEntrySize=5, hashEntries=394, individualBytes=1695, totalCostInBytes=1958
processFile() fileSize=3665, hashBufferSize=1021, hashEntrySize=6, hashEntries=296, individualBytes=1889, totalCostInBytes=2164
processFile() fileSize=3665, hashBufferSize=1021, hashEntrySize=7, hashEntries=227, individualBytes=2076, totalCostInBytes=2366
processFile() fileSize=3665, hashBufferSize=2053, hashEntrySize=3, hashEntries=821, individualBytes=1202, totalCostInBytes=1457
processFile() fileSize=3665, hashBufferSize=2053, hashEntrySize=4, hashEntries=555, individualBytes=1445, totalCostInBytes=1697
processFile() fileSize=3665, hashBufferSize=2053, hashEntrySize=5, hashEntries=409, individualBytes=1620, totalCostInBytes=1875
processFile() fileSize=3665, hashBufferSize=2053, hashEntrySize=6, hashEntries=305, individualBytes=1835, totalCostInBytes=2104
processFile() fileSize=3665, hashBufferSize=2053, hashEntrySize=7, hashEntries=238, individualBytes=1999, totalCostInBytes=2280
processFile() fileSize=3665, hashBufferSize=4093, hashEntrySize=3, hashEntries=844, individualBytes=1133, totalCostInBytes=1382
processFile() fileSize=3665, hashBufferSize=4093, hashEntrySize=4, hashEntries=561, individualBytes=1421, totalCostInBytes=1670
processFile() fileSize=3665, hashBufferSize=4093, hashEntrySize=5, hashEntries=423, individualBytes=1550, totalCostInBytes=1798
processFile() fileSize=3665, hashBufferSize=4093, hashEntrySize=6, hashEntries=314, individualBytes=1781, totalCostInBytes=2045
processFile() fileSize=3665, hashBufferSize=4093, hashEntrySize=7, hashEntries=238, individualBytes=1999, totalCostInBytes=2280
philipsmolen@Philips-MacBook-Air 4p % gzip -v < 4p.C > /dev/null 
 62.8%
philipsmolen@Philips-MacBook-Air 4p % gzip -9v < 4p.C > /dev/null 
 62.8%
philipsmolen@Philips-MacBook-Air 4p % ./4p 4p                                                                    
File name: 4p
processFile() fileSize=44255, hashBufferSize=257, hashEntrySize=3, hashEntries=11907, individualBytes=8534, totalCostInBytes=11091
processFile() fileSize=44255, hashBufferSize=257, hashEntrySize=4, hashEntries=8458, individualBytes=10423, totalCostInBytes=12785
processFile() fileSize=44255, hashBufferSize=257, hashEntrySize=5, hashEntries=6668, individualBytes=10915, totalCostInBytes=13114
processFile() fileSize=44255, hashBufferSize=257, hashEntrySize=6, hashEntries=5445, individualBytes=11585, totalCostInBytes=13715
processFile() fileSize=44255, hashBufferSize=257, hashEntrySize=7, hashEntries=4606, individualBytes=12013, totalCostInBytes=14092
processFile() fileSize=44255, hashBufferSize=509, hashEntrySize=3, hashEntries=12277, individualBytes=7424, totalCostInBytes=9888
processFile() fileSize=44255, hashBufferSize=509, hashEntrySize=4, hashEntries=8647, individualBytes=9667, totalCostInBytes=11958
processFile() fileSize=44255, hashBufferSize=509, hashEntrySize=5, hashEntries=6840, individualBytes=10055, totalCostInBytes=12168
processFile() fileSize=44255, hashBufferSize=509, hashEntrySize=6, hashEntries=5569, individualBytes=10841, totalCostInBytes=12894
processFile() fileSize=44255, hashBufferSize=509, hashEntrySize=7, hashEntries=4750, individualBytes=11005, totalCostInBytes=12976
processFile() fileSize=44255, hashBufferSize=1021, hashEntrySize=3, hashEntries=12469, individualBytes=6848, totalCostInBytes=9264
processFile() fileSize=44255, hashBufferSize=1021, hashEntrySize=4, hashEntries=8866, individualBytes=8791, totalCostInBytes=11000
processFile() fileSize=44255, hashBufferSize=1021, hashEntrySize=5, hashEntries=6939, individualBytes=9560, totalCostInBytes=11624
processFile() fileSize=44255, hashBufferSize=1021, hashEntrySize=6, hashEntries=5650, individualBytes=10355, totalCostInBytes=12357
processFile() fileSize=44255, hashBufferSize=1021, hashEntrySize=7, hashEntries=4823, individualBytes=10494, totalCostInBytes=12410
processFile() fileSize=44255, hashBufferSize=2053, hashEntrySize=3, hashEntries=12635, individualBytes=6350, totalCostInBytes=8725
processFile() fileSize=44255, hashBufferSize=2053, hashEntrySize=4, hashEntries=8970, individualBytes=8375, totalCostInBytes=10545
processFile() fileSize=44255, hashBufferSize=2053, hashEntrySize=5, hashEntries=7044, individualBytes=9035, totalCostInBytes=11047
processFile() fileSize=44255, hashBufferSize=2053, hashEntrySize=6, hashEntries=5782, individualBytes=9563, totalCostInBytes=11483
processFile() fileSize=44255, hashBufferSize=2053, hashEntrySize=7, hashEntries=4874, individualBytes=10137, totalCostInBytes=12015
processFile() fileSize=44255, hashBufferSize=4093, hashEntrySize=3, hashEntries=12696, individualBytes=6167, totalCostInBytes=8527
processFile() fileSize=44255, hashBufferSize=4093, hashEntrySize=4, hashEntries=9045, individualBytes=8075, totalCostInBytes=10217
processFile() fileSize=44255, hashBufferSize=4093, hashEntrySize=5, hashEntries=7047, individualBytes=9020, totalCostInBytes=11030
processFile() fileSize=44255, hashBufferSize=4093, hashEntrySize=6, hashEntries=5781, individualBytes=9569, totalCostInBytes=11490
processFile() fileSize=44255, hashBufferSize=4093, hashEntrySize=7, hashEntries=4909, individualBytes=9892, totalCostInBytes=11744
philipsmolen@Philips-MacBook-Air 4p % gzip -v < 4p > /dev/null  
 82.9%
philipsmolen@Philips-MacBook-Air 4p % gzip -v9 < 4p > /dev/null                                                  
 82.9%
philipsmolen@Philips-MacBook-Air 4p % 
```
## Large Strings
I created a file which was two exact copies of my source file.
```
philipsmolen@Philips-MacBook-Air 4p % cat 4p.C 4p.C >double.txt
```
This test always gave a huge advantage to gzip in the past.
This is the main reason why I want to add the Long Strings algorithm to this program.
```
philipsmolen@Philips-MacBook-Air 4p % ./4p double.txt             
File name: double.txt
processFile() fileSize=7330, hashBufferSize=257, hashEntrySize=3, hashEntries=1480, individualBytes=2890, totalCostInBytes=3438
processFile() fileSize=7330, hashBufferSize=257, hashEntrySize=4, hashEntries=940, individualBytes=3570, totalCostInBytes=4135
processFile() fileSize=7330, hashBufferSize=257, hashEntrySize=5, hashEntries=678, individualBytes=3940, totalCostInBytes=4519
processFile() fileSize=7330, hashBufferSize=257, hashEntrySize=6, hashEntries=510, individualBytes=4270, totalCostInBytes=4869
processFile() fileSize=7330, hashBufferSize=257, hashEntrySize=7, hashEntries=398, individualBytes=4544, totalCostInBytes=5163
processFile() fileSize=7330, hashBufferSize=509, hashEntrySize=3, hashEntries=1698, individualBytes=2236, totalCostInBytes=2729
processFile() fileSize=7330, hashBufferSize=509, hashEntrySize=4, hashEntries=1100, individualBytes=2930, totalCostInBytes=3435
processFile() fileSize=7330, hashBufferSize=509, hashEntrySize=5, hashEntries=829, individualBytes=3185, totalCostInBytes=3688
processFile() fileSize=7330, hashBufferSize=509, hashEntrySize=6, hashEntries=612, individualBytes=3658, totalCostInBytes=4193
processFile() fileSize=7330, hashBufferSize=509, hashEntrySize=7, hashEntries=482, individualBytes=3956, totalCostInBytes=4512
processFile() fileSize=7330, hashBufferSize=1021, hashEntrySize=3, hashEntries=1807, individualBytes=1909, totalCostInBytes=2375
processFile() fileSize=7330, hashBufferSize=1021, hashEntrySize=4, hashEntries=1253, individualBytes=2318, totalCostInBytes=2766
processFile() fileSize=7330, hashBufferSize=1021, hashEntrySize=5, hashEntries=948, individualBytes=2590, totalCostInBytes=3034
processFile() fileSize=7330, hashBufferSize=1021, hashEntrySize=6, hashEntries=749, individualBytes=2836, totalCostInBytes=3286
processFile() fileSize=7330, hashBufferSize=1021, hashEntrySize=7, hashEntries=610, individualBytes=3060, totalCostInBytes=3520
processFile() fileSize=7330, hashBufferSize=2053, hashEntrySize=3, hashEntries=1886, individualBytes=1672, totalCostInBytes=2118
processFile() fileSize=7330, hashBufferSize=2053, hashEntrySize=4, hashEntries=1332, individualBytes=2002, totalCostInBytes=2420
processFile() fileSize=7330, hashBufferSize=2053, hashEntrySize=5, hashEntries=1021, individualBytes=2225, totalCostInBytes=2632
processFile() fileSize=7330, hashBufferSize=2053, hashEntrySize=6, hashEntries=803, individualBytes=2512, totalCostInBytes=2928
processFile() fileSize=7330, hashBufferSize=2053, hashEntrySize=7, hashEntries=673, individualBytes=2619, totalCostInBytes=3032
processFile() fileSize=7330, hashBufferSize=4093, hashEntrySize=3, hashEntries=1932, individualBytes=1534, totalCostInBytes=1969
processFile() fileSize=7330, hashBufferSize=4093, hashEntrySize=4, hashEntries=1362, individualBytes=1882, totalCostInBytes=2289
processFile() fileSize=7330, hashBufferSize=4093, hashEntrySize=5, hashEntries=1061, individualBytes=2025, totalCostInBytes=2413
processFile() fileSize=7330, hashBufferSize=4093, hashEntrySize=6, hashEntries=834, individualBytes=2326, totalCostInBytes=2723
processFile() fileSize=7330, hashBufferSize=4093, hashEntrySize=7, hashEntries=682, individualBytes=2556, totalCostInBytes=2963
philipsmolen@Philips-MacBook-Air 4p % gzip -v9 < double.txt > /dev/null  
 80.7%
philipsmolen@Philips-MacBook-Air 4p % 
```
In the best case my prototype removed 73.14% of the file size and gzip removed 80.7%.
This program is more competitive than my previous compression experiments, but it's still got room for improvement.

When I repeated that test with 10 copies of the source my program only removed 82.1% of the file size.
Clearly the current algorithm is close to maxing out.
gzip removed 95.4%.

```
philipsmolen@Philips-MacBook-Air 4p % cat double.txt double.txt double.txt double.txt double.txt > 10x.txt
philipsmolen@Philips-MacBook-Air 4p % ./4p 10x.txt                                                  
File name: 10x.txt
...
processFile() fileSize=36650, hashBufferSize=4093, hashEntrySize=3, hashEntries=10668, individualBytes=4646, totalCostInBytes=6562
...
philipsmolen@Philips-MacBook-Air 4p % gzip -v9 < 10x.txt > /dev/null                                
 95.4%
 ```