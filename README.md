# File Compression
Like zip or gzip.  But written from scratch.

Some people whittle.

## rANS
All of these attempts use rANS for the entropy encoding.

Originally I tried to use a Huffman tree.  
The Huffman tree was easy to encode, but sometimes expensive to write.
With rANS it got trickier, because you can specify things a lot more precisely.
If you try to write the complete table for a rANS encoded it would be way too long.

But if you're computing the values some other way, rANS can be nice.  
Analyze3.C, for example, starts with all the probabilities set equal.
We update the probabilities one item at a time, after we encode each item.
So we never have to explicitly write the table out to the compressed file.

It's nice that rANS can get so precise.  
The size of the resulting output is very close to the ideal size.
This is especially helpful with control decisions.
E.g. I have a 5% chance of applying option A, a 2% chance of applying option B, and a 93% chance applying option C.
Huffman did okay when the probabilities weren't so skewed.

The worst part of rANS is that you have to reverse the order of the items.
That pretty much forces us to encode things in blocks.
You can keep the items in memory until you have a decent number of them.
Then you send the items to the rANS encoder in reverse order.
Then you start the new block.
The reader doesn't know about reversing the order.

## Analyze3.C
This version of the code mostly copies bytes to the rANS encoder one at a time.
### Basic Context
Each time we want to encode a byte, we look at the previous two bytes, and ask which byte usually comes after those two bytes.
We use that, and related tricks, to set the probability of each byte.
### New Bytes
The first question is always is this a new byte.
All of our other logic will copy a byte from earlier in the stream.
If you ask for the probability of a byte we have not seen before, it will be 0.

We use a very straightforward algorithm.
We infer the probability of needing a new byte at any given place.
Then we send a yes/no to the rANS encoder saying that we do or do not need to insert a new byte.
The results are as small as I could imagine from any other scheme.

I looked at other ways of dealing with new bytes.
This worked out the best because I could easily come up with a good estimate of the probability for each thing I stored.
I looked at other alternatives (like setting the initial frequencies of all bytes to 1, rather than 0, when we first start) but these all had too many arbitrary guesses.
### Longer Strings
We have two ways of looking at longer strings.  
The first is to look for longer strings based on context.
For example, assume we recently saw "pizza pie" in the input file.
And we just finished sending "pizz" to the entropy encoded for the second time.
There is a very good chance that we are trying to say "pizza pie" again.
So there is a very good chance that the next byte will be "a".

We are looking at longer strings for context, but we are still sending one byte at a time to the entropy encoder.

The length of the context is important.
When we see "pizz" we're pretty sure we know what's next.
When we see "pizza pi" we're very sure we know what's next.
We keep statistics on how well this has been working for this file so far.
We use those statistics to set the probabilities.
### Whole Strings
There is also an option to copy an entire string from the past, instead of just copying a single byte at a time.

This is based on some of my previous projects.  
These were inspired by LZ78 algorithm.
When we see an *interesting* string in the input we tag it and put it into a table.
I can recall that entire string with a single number.

So far this hasn't produced strong results.
However, it is interesting how easily this can be grafted onto the rest of the program.
I think a cleaned up version of this algorithm could be very powerful.
