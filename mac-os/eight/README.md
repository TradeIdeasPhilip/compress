# eight
A compression program compiled for the mac.

The original version of this (../../\*ght\*) did a good job guessing the next byte in the file, one byte at a time.
My plan is to add sliding buffer style compression in addition to this.

# count and uncount

The `count` and `uncount` programs are simple examples that use some of the same tools and libraries as most of the programs in this `compress` project.
`count` and `uncount` are __complete__.
They compress and decompress data.

## Reusable libraries

The most important parts of the `count` and `uncount` project are the reusable libraries.

### Entropy encoder

After some experimentation I've settled on the rANS entropy encoder.
Its results are as good as theoretical maximum.
And I've added a little code on top to make it very easy to use.

The input to an entropy encoder is a series of values, like the ones listed here in __bold__.
* The user id is __12345__.
* The session id is __8765__.
* __Yes__, the user wants to keep a backup file.
* The user chose the __GIF__ file format.
* The background color is __#ff0000__
* etc.

The output from an entropy encoder is a series of bits.
And it is efficient; it will try to use as few bits as possible. 

In short, the entropy encoder converts from a convenient form of the data to an efficient form of the same data.

### Entropy decoder

The decoder will take the output from the encoder and convert it back into the original values, e.g.
* 12345
* 8765
* Yes
* GIF
* #ff0000

Note that the compressed version of the data only contains the field values, e.g. 12345.
The field names, e.g. user id, are not stored.
The program doing the encoding and the program doing the decoding need to agree on these in advance.

For example, let's define the phil-fun-test file format (`*.pft`) to contain the following fields, in order.

| Field name | Possible values |
| ------------- | ------------------ | 
| user id              | 0 - 2,147,483,647 |
| session id              | 0 - 65,535 |
| keep backup              | yes, no |
| file format              | JPG, PNG, GIF, TIFF, BMP  |
| background color             | 0 - 16777215 |

Of course, the file format can be as simple or as complicated as you like.
The `count` and `uncount` programs look at a file as a sequence of bytes, without knowing or caring what those bytes mean.
### Optimizing based on frequency

We need one more thing to make the entropy encoder and decoder work.
We need more than the list of possible values for each item.
We need to tell the encoder and decoder exactly how often we expect to see each value.
For example:

| Field name | Possible values |
| ------------- | ------------------ | 
| user id              | 0 - 2,147,483,647, all equally likely |
| session id              | 0 - 65,535, 25% of the values are less than 16.  50% are less than 256. |
| keep backup              | yes (99.7% of the time), no (0.3% of the time) |
| file format              | JPG (20%), PNG (44%), GIF (22%), TIFF (4%), BMP (10%)  |
| background color             | 0 - 16777215, all equally likely |

The encoder will use fewer bits to encode more common values.
Less common values will require more bits to encode, but there are fewer of these, so they are less important.

Entropy encoding works best when some items are much more common than others.
In some programs I try to reorganize the data so the entropy encoder can do a better job.
Search this repository for "mru list" for some good examples.

### Convenience code

This project uses a lot of shared code.

One example is that the rANS encoder expects all frequency data to be expressed as a fraction where the denominator is a power of two.
I've found it much more common for the main program to use fractions with any denominator, so my library code does the conversion automatically.

If you say that you have and item with probability 1/3, the library will change that to something like  35791394<b>1</b>/1073741824 or 35791394<b>2</b>/1073741824.
And a corresponding library will tell the decoder to convert from those huge fractions back to 1/3.

Another example:  The encoder spits out a stream of bits.
That's not convenient.
So I wrote a wrapper around the encoder which will write the output to a file.
There were a lot of little details, but you don't have to worry about those.
I also wrote a corresponding library to read from that file and send the bits to the rANS decoder in a format that it likes.

### Main program

I wrote the simplest possible main program make use of the rANS encoder/decoder and my convenience libraries.

`count` starts by reading the entire input file.
It counts the number of times it sees each byte in the file.
For a super simple example, let's say the input file contains `abbcccdddd`.
So we'll create a table like this:
| Byte | Frequency |
| :----: | :---------: |
| a | 1/10 |
| b | 2/10 |
| c | 3/10 |
| d | 4/10 |
| everything else | 0/10

Then `count` will make another pass through the input file, one byte at a time.
It will send each byte to the encoder.
And it will use the table above to encode each byte.

There's one more thing.
`uncount` needs access to the frequency table that `count` created.
So, before sending the individual bytes to the encoder, `count` needs to send the contents of this table to the compressed file.
And that's it.

If you run `count` on a file of English text, it should compress that into a smaller file.
IF you run `count` on a file of random garbage, the output might actually be bigger than the input.
Either way, you can run `uncount` on the compressed file to restore the original file.

### Old code

This project only uses the newest and most practical version of each bit of shared code.
Some of my previous projects did things slightly differently.
Those typically lead to the new and improved versions demonstrated by this project.
(Written 11/17/2022)