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

An extreme but common example is encoding a Boolean.
E.g. Do we save or abandon the last item we looked at?
The best Huffman could do is 1 bit for each Boolean.
I.e. no compression at all.
rANS can make a Boolean very small, if the probability of true is a lot different from the probability of false.

The worst part of rANS is that you have to reverse the order of the items.
That pretty much forces us to encode things in blocks.
You can keep the items in memory until you have a decent number of them.
Then you send the items to the rANS encoder in reverse order.
Then you start the new block.
The reader doesn't know about reversing the order.

### Style

You can add an entropy encoder to a lot of data processing.
Any time you get output from a program that doesn't divide nicely into bytes, e.g. a long list of numbers that go from 0 to 2, or from 0 to 259, an entropy encoder can help.

Also, if you know the probability of each value, it's easy to send those values to an entropy encoder.
The more extreme the values, the better results you will get.
I.e. if you sent English text to an entropy encoder you'll get a lot of compression because `e` is common, and `z` is rare and `ñ` is very rare.

But I'm mostly working in the other direction.
_I know I'm going to have an entropy encoder, so I start thinking about how to predict the next item._
The better I am at predicting things, the better my compression will be.
Several of these programs will look at all possible bytes, and count how many of each we've seen so far, to to guess which byte will come next.
`Analyze3.C` goes further and looks at the previous byte or two of context to make even better guesses.
E.g. if the last byte was `q` there's a very good chance the next byte will be `u`.
`Eight.C` and `HashDown.C` do the same basic thing but more of it.

And I can try to reorganize the data.
I know the entropy encoder does best when some items have a very high probability.
`LZMW.C`, `LzBlock.C` and `LzStream.C` all reorganize the data into an MRU list.
Asking for the most recently used item, or the second most recently used item, will happen much more often than asking for any specific item.

`HashDown.C` goes looking for cases where it can make a high quality prediction.

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
When we see "pizz" we're *pretty sure* we know what's next.
When we see "pizza pi" we're *very sure* we know what's next.
We keep statistics on how well this has been working for this file so far.
We use those statistics to set the probabilities.
### Whole Strings
There is also an option to copy an entire string from the past, instead of just copying a single byte at a time.

This is based on some of my previous projects.
These were inspired by the LZ78 algorithm.
When we see an *interesting* string in the input we tag it and put it into a table.
The decoder can recall that entire string with a single number.

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
When I finally send data to the entropy encoder, I send special commands, like "save this string in the table."
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
It is by far the most common command.)
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
That was a lot more work as I tried to add all types of smarts.
And the Huffman tree would only take that so far as it's not very precise.
With rANS I don't have to treat the control logic as a special thing.
rANS works really well with a 1% chance of this, a 33% chance of that, and ...

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
I have a way to reorder the list, in case I know that a string is about to be lost and I will need it soon.
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
The first match was 5 bytes longer than the second match and 97% ≈ 2⁵ × 3%.
Of course, there is a finite chance that any other byte could come next.
The exact weightings are a little more complicated, but this is where I started.

My initial results are promising.
As of 12/29/2020 I'm typically getting a percent or two better than gzip on various types of text files.
I'm in the process of writing a complete version of the compress and decompress programs.
The decompress program needs a lot of details to keep up with the compress program.
So I can't fake as much; I need to fill in more details of the compress program or the decompress program will never work.

## HashDown.C

This is an interesting twist on the basic ideas behind `Eight.C`.
I'm still looking for the last time we saw this same context.
But instead of looking through the last n bytes of the file,
I'm storing the most recent hit in a hash table.
The hash table was intended to make the lookup must faster.

The hashes allowed us to store a lot of possible context values in a fixed amount of space.
If two contexts share the same hash, one might kick the other out of the table.
It's arbitrary but it's simple.

On the down side, the performance never really materialized.
It seem like it will be faster just to search the last n bytes of the file.
Like `Eight.C`.

On the good side, I am seeing some interesting results.
Almost every time the new hash tables find something, it's unique and it's a match.
I'm still optimizing things, but the compression is competitive.

### Further research

I have the ability to keep multiple items per hash
(optimized for small numbers like 3)
but these usually all point to the same next byte.
This gets more true as the length of the context goes up.
I want to do some tests, but I bet that memory would be better served with just one entry per hash, but 3× as many hashes.

### Back to Eight.C

I'm tempted to roll some of these ideas back into `Eight.C`.

We still walk through the entire sliding window, rather than trying to build a hash table describing recent occurrences.
But maybe, instead of looking at all of the times when we had the same n bytes of context, we only look at the last time.
Recent results suggest that we can get the right answer most of the time with just the most recent result.
While going backwards through the file we would have the option to stop short after finding a good match.

Also, this gives a simple, high probability answer.
If we find a context match with 7 bytes, then we check if the next byte also matches.
We send a Boolean to the stream to say yes or no.
This should be skewed way far from 50% - 50%, so it should be cheap.
And it's usually accurate, so we're done with this byte and we can move on, probably after sending less than one bit to the stream.

We don't have to do 8 of these in a row.
Maybe we couldn't find an 8 byte context match.
Both sides know this, so it's not even a question.
Then we send a no, the 7 byte context match was not helpful.
We know that there must be a 6 byte context match.
But if it points to the same next byte as the 7 byte context, it will be useless, so we skip it.

Somewhere around 2 bytes of context we actually start looking for multiple answers instead of just the most recent one.

#### A Different Perspective

We have a magic oracle named eightBytesOfContext.
Some times the oracle is silent.
He says nothing.
There is no cost in the compressed file, but nothing was gained, either.
Other times the oracle will say there is a very high chance this one particular byte will be next.
Values vary from 65% for source code to around 95% for some really redundant log files.

This is a powerful guess.
There are 255 other bytes that could be in this place.
When the oracle chooses to make a bet, it's usually way up there, above 50%.
When the oracle is wrong, so what, we still know how to weight the remaining possibilities.
And we remove this byte from contention.
There is no redundancy.
There were no gimmicks or trade offs.
I found a byte that I had a really good feeling about, and the statistics say it's legit.
When I'm right I can write this entire byte using less than one bit.
Take that Huffman coding!

I separate this method from any other ways of setting the probability for this number.
In the past I tried to look at a lot of different sources, and give them all appropriate weights, and average them all out.
Now it appears that this is such a strong signal that we don't care about anything else.
This oracle gives us probabilities like 65-95%.
For comparison, if you're looking at English text with no punctuation or spaces, all caps, `E` is the most common letter with only 11% probability.
My point is that the rest is too small to care about compared to the suggestion of the oracle.

We actually have multiple oracles.
If the 8 byte context oracle doesn't answer our question, maybe the 7 byte context oracle can step in.
Same exact rules.
If none of the oracles jumps in, then we switch to more traditional methods, like counting the total number of times we've seen each byte, and adding 1 to make sure nothing has a probability of 0.

We keep a running tab of how well each oracle does for this file.
We use that to set the probabilities.

### And Copy Big Blocks

I was trying to avoid looking through the entire sliding window.
But it seems like that's going to be part of the algorithm.

In some cases gzip/deflate always wins.
It's good when there are large blocks of identical data.
My alternatives could never compete with deflate in those cases.

Here's a twist.
Do deflate-style compression, but only when the copied string is at least 8 bytes long.
For shorter strings, we use the byte by byte guessing that this program and `Eight.C` do so well.
The best of both worlds.