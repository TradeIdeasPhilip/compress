# File Compression
Like zip or gzip.  But written from scratch.

Some people whittle.

## rANS
All of these attempts use rANS for the entropy encoding.

Originally I tried to use a Huffman tree.
The Huffman tree was easy to encode, but sometimes expensive to write.
With rANS it got trickier, because you can specify things a lot more precisely.
If you try to write the complete table for a rANS encoder it would be way too long.

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
And we just finished sending "pizz" to the entropy encoder for the second time.
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

## LZMW.C

This was my first serious attempt at a complete compression program.
I learned some good things from here.
### LZ78
LZ78 and related schemes can do good work.
They pick out interesting strings that you can reference later very cheaply.
LZ77 would offer more strings, but then you have to use more bits to specify which one you want.
### Multi Pass
This program only works because the compressor makes multiple passes through the input file.
I use some simple variations of LZ78 to start the process of picking strings.
Then I see which ones are actually used.
When I finally send data to entropy encoder, I send special commands, like "save this string in the table."
Or "delete that string that you just used."

It's still cheap to use a back reference.
We are sending very simple messages, like copy or delete what you were just working with.
The position is obvious from context.
The message is basically "here" rather than "go back 122 bytes".

Deleting strings after their last use saved a lot of bytes.
Again, we are emitting such a simple instruction that costs very little.
So each time we look back at the table, it is smaller and a reference into it costs less.
### Control vs Data
Another amazing part was when I split up the control signals.

The compressed file is a collection of commands.
E.g. Save the last interesting string or copy a string from our list.
Originally any of these was valid at any time.

I quickly saw that some combinations made no sense.
If you just deleted a string from the list, you will never say to save the last interesting string.
You always do the saving before the deleting.

Just separating the print command from all the others did a lot.
(The print command copies a string from the table to the output.
It is by far the most command command.)
Eventually I made a complete state machine saying what was and wasn't legal at any time.
That helped even more.

### MRU
Another huge improvement case when I changed the list of strings into an MRU list.

Originally the first string we saved was in position 0, the second was in position 1, etc.
The assumption was that some strings would be used a lot, and others only once or twice, and the entropy encoder would eat that stuff up.

However, reordering the list made it much better for the entropy encoder.
Any time you add a new string, that gets pushed into index 0.
And time you use an existing string, it gets moved back to index 0.
Any time you delete a string, all the others move up.
Low numbered indexes were used so much more often than any specific string from before we added the MRU.

This should also help in a second way.
If you look at a histogram to say how often each index is used, it seems to follow Zipf's law.
Without the MRU the histogram was random and you had to encode the frequencies in the file very explicitly.
It seems like you could give a few numbers to describe the MRU's histogram, and thus encode it very easily.
I never actually got that step to work, for some reason.
I could never get my estimate good enough without writing a lot of meta data.

### Huffman Trees vs rANS
rANS is very easy to use and the results are very high quality.
It feels good that it is so precise.
And the results show.

When I first started tweaking the control data, I was still using Huffman trees.
That was a lot more work as I tried to do add all types of smarts.
And the Huffman tree would only take that so far as it's not very precise.
With rANS I don't have to treat the control logic as a special thing.
rANS works really well with a 1% chance of this, and a 33% chance of that...

### Memory Hog
The worst part of this program is that it uses so much memory.
I tried some tests to see what would happen if we didn't save things as long, or didn't save as many things.
Those changes always caused the compression to get worse.
I never found a good happy medium.

Clearly we need to break the file into blocks or something!
## LzStream.C
This was an attempt to make LZMW.C less of a memory hog.

The idea was to process the data in a single pass.
(I never got over the issue that the rANS required me to reorder everything.
If I ever tried to finish this project, I'd still have blocks or something to deal with that.)

I set a max size for the table of strings.
And I did no look ahead.
Each time we saw an interesting string, we pushed it onto the front of the table.
Each time we used a string, we moved it to the front of the table.
Each time the list got too long, we deleted the oldest entries.

While this was a step in the right direction, it never produced particularly good results.
The good parts were good.
But the program got too dumb.
The next obvious thought was a compromise:  limited look ahead.

## LzBlock.C
This was forked from LZMW.C but inspired by LzStream.C

I used the same basic idea as LZMW.
I identify interesting strings, to reduce the number of things we might try to reference.
I use limited look ahead to see which of those strings will definitely be used.
I record commands in the output file to say which strings should be saved.

Some strings will fall off the end.
I have a way to reorder to the list, in case I know that a string is about to be lost and I will need it soon.
At the end of the block we leave the string list in place.
Within a block we carefully curate the strings.
Between blocks we hope for the best, and typically do well.

This program had some interesting results.
Further research in this direction is warranted.

The details of the block structure are currently quite clunky.
Sometimes we get to a point where we probably should have ended the block sooner.
There's no obvious way to make that decision until its too late.
Perhaps I should have been more flexible with the table size.
Shoot for 2k entries, but if it gets a little longer, that's okay.

One thing was clear.
Short strings are important.
I love copying long strings.
And I don't want to stop copying long strings.
But we need an efficient way to copy short strings.
This was one of the big influences that made me start Analyze3.C.
That program is aware of longer strings, but heavily focused on 3 bytes at a time.

## Eight.C
Eight is in progress right now.
(12/22/2020)
This was inspired by Analyze3.C, and it takes some of these ideas to an extreme.
We *only* send one byte at a time to the entropy encoder.

For each byte we look at the previous N bytes to gather statistics.
For every byte we are looking at the previous **eight** bytes for context.
We chose that number because it's easy to do all at once with a 64 bit integer.

Consider the 9 letter string "Pizza Pie".
If we were about to encode the "e" at the end, we'd look at history and look for "Pizza Pi".
If you see "Pizza Pie" in history, that's a perfect match of **eight** bytes, and a very strong sign that an "e" is next.
If you see "Cherry Pie" in history, that is a match of three bytes: " Pi".
That also suggests that the next letter is "e", but this is a shorter match so it's a weaker hint.

If you see "Multiply by 2 Pi!" in history, that's a conflicting hint.
Sometimes the three bytes " Pi" are followed by "!".
However, this is a weak hint with only 3 letters of context.

Assume history contains one example of "Pizza Pie" and one example of "Multiply by 2 Pi!".
We might set the probability of the next byte being 97% "e" and 3% "!".
The first match was 5 bytes longer than the second match and 97% ≈ 2⁵ * 3%.
I need to work on the exact weightings, but that's where I started.
Of course, there is a finite chance that any other byte could come next.
In practice that's far less than 1%.

My initial results were promising.
I want to output more statistics and try more test files.
I have not tried any tweaking yet.

I suspect the problem is that I'm not weighting the longer strings enough.
We give them a high weight for being long.
But we also give a boost to any hint that is repeated.
An **eight** byte match is currently weighted 256× as much as a 0 byte match.
But we are probably getting more far more than 256 of these 0 byte matches.
Analyze3.C focuses almost entirely on the longest match, and only falls back on the second or third best match when the better historical matches never point to the right byte.
It did this in part because the long matches were so good.
