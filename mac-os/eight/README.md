# eight
A compression program compiled for the mac.

The original version of this (../../\*ght\*) did a good job guessing the next byte in the file, one byte at a time.
My plan is to add sliding buffer style compression in addition to this.

# count and uncount

The count and uncount programs are simple examples that use some of the same tools and libraries as most of the programs in this `compress` project.
`count` and `uncount` are __complete__.
They compress and decompress data.

Like most of these compression programs, `count` has three major parts.
* `count` starts by looking at the input file in detail, trying to find patterns. I call that the main program.
* An entropy encoder takes a series of number from the first part of the process, and outputs a series of bits.
* `RansBlockWriter` takes the output from the entropy encoder and sends that to a file.

The main program is the only part that seriously changes from one program to the next.
The other items are libraries that work well and get reused a lot.

`uncount` works in the reverse order with `RansBlockReader` reading the file and feeding bits to the entropy decoder, the entropy decoder converts the bits into numbers, and finally the program converts those numbers into the bytes of the decompressed file.

## Entropy Encoder / Decoder

This is a very powerful part of every program.
It allows you to focus on integers when writing your program.

For example, maybe the main program keeps a list of of interesting strings.
If you are compressing a document describing a pizza party, you might use the words "pizza" and "party" a lot.
Maybe the main program has a list of these words, and it just sends a 2 every time it sees "pizza" and a 4 every time it sees "party".
This is a very simplified version of what LZ78, LZW, etc. actually do.

The question is how to output numbers like {0, 1, 2, 3, 4} efficiently.
If you tried to use simple binary encoding, it would take 3 bits to send send any of those 5 numbers.

| Internal value | Bits written to the file |
| -------------: | ------------------: | 
| 0              | 000 |
| 1              | 001 |
| 2              | 010 |
| 3              | 011 |
| 4              | 100 |
| unused         | 101 |
| unused         | 110 |
| unused         | 111 |

An entropy encoder can do better.
At a bare minimum we can notice that if the first bit is a 1, the internal value must have been a 4.
So we can trim some unused bit patterns to make the encoding of 4 much shorter.
For example:
| Internal value | Bits written to the file |
| -------------: | ------------------: | 
| 0              | 000 |
| 1              | 001 |
| 2              | 010 |
| 3              | 011 |
| 4              | 1 |

Now it will take only a single bit to encode the value 4.
Is that good?
If 4 is a common value, it's very good.
And we don't have to guess.
The main program doesn't just tell the entropy encoder what values to encode, it says how common each value is.
So the entropy encoder can reserve shorter codes for more common values and use longer codes for values that are less common.

### Specific entropy encoders

Any entropy encoder will look at how often each of the values are used, and will pick smaller encodings for the more popular items.

Huffman encoder is a popular version of entropy encoding.
It's very simple.
A second year college student might build one from scratch for homework.
It can do everything I've described here and a little more.

The biggest limitation of Huffman encoding is its precision.

If you are encoding a series of yes/no questions, each question will add one bit to the output.
In short, Huffman can't do anything for yes/no questions.

What if you have a series of questions with three answers?
Huffman will reduce each answer to `0`, `10`, or `10`.
That would be perfect if the first value was found 50% of the time and the other two values were used 25% of the time each.
But other encoders can do better in most circumstances.

I always use a rANS encoder instead.
I can say things like "99% of the answers will be yes and 1% will be no."
And the results will cost around 0.0807931 bits per answer.
That's more than 12 answers per bit, more than 12 times as efficient as Huffman.

### Fractions

When you give a probability to the rANS encoder, it needs to be written as a fraction.
The basic rans encoder needs the denominator to be a power of 2.
In practice my denominator is rarely a power of 2 so I created a library that lets you use any number as a denominator.

For example, assume the main program is working with 3 values, 0, 1 and 2.
Assume there is a ⅓ chance of seeing any one of them.
I.e. each of the three values occur just as often as the others.
My library would automatically change that to say that there is a
35791394<b>1</b>/1073741824 chance of seeing the first item or the second item, but a 35791394<b>2</b>/1073741824 of seeing the third item.
These fractions are so close to ⅓ that you can't measure a difference.

Most important of all, my encoder and decoder are _consistent_, so the encoder and decoder will always see the same thing. 
There is a lot of rounding in that code.
It's not hard, but it would be easy to make a mistake.

## Block Reader / Writer

There are a lot more little details involved in using an entropy encoder.
Things like rounding off to the nearest byte, detecting end of file, and just making sure the items come out in the same order as they went in.

These details are all hidden in the `RansBlockReader` and `RansBlockWriter` classes.

## Main Program

The main compression program will read from the input file, look for patterns, and describe the file in a format that works well for the rANS encoder.

The main part of `compress` is very simple on purpose.
It reads the entire input file before it sends any data to the output file.
It looks at the bytes of the file and counts how often each byte appears.
If the input file is English text, bytes like `a` and `e` will be very common, while bytes like `z` and `q` will be far less common.
A lot of bytes will not appear in the file at all because they are only used for non-English characters.

The main program assumes this pattern will hold.
As long as some bytes are very common, the entropy encoder will find an efficient ways to encode the bytes.
If the input file is random, the entropy encoder will spend about one byte of output to encode each byte of input, for no real change.

But there is a cost!
The compressor knows the frequency of each byte of the original file because it has complete access to that file.
But how does the decoder know the frequencies of the bytes?
When you ask the decoder to give you the next value from the compressed file, you also need to give it a list of probabilities for each possible answer.
This needs to match the list used to encode the value.

How do we do this?
_First_ `count` writes the probabilities of all 256 possible bytes at the beginning of the output file.
_After that_ it copies the input file into the rANS encoder one byte at a time.
This header will take up space, and there are other sources of overhead, so the output file might be larger than the input.

This program is helpful on some input files.
However the main program is just a placeholder.
This program shows off the rANS encoder and my libraries that make the encoder easier to use.