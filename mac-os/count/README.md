# count and uncount

The `count` and `uncount` programs are simple examples that use some of the same tools and libraries as most of the programs in this `compress` project.
`count` and `uncount` are __complete__.
They compress and decompress data.

The most important parts of the `count` and `uncount` project are the reusable libraries.
These programs demonstrate code used in more complicated programs.

## Entropy encoder

The input to an entropy encoder is a series of values, like the ones listed here in __bold__.
* The user id is __12,345__.
* The session id is __8,765__.
* __Yes__, the user wants to keep a backup file.
* The user chose the __GIF__ file format.
* The background color is __#ff0000__
* etc.

The output from an entropy encoder is a series of bits.
And it is efficient; it will try to use as few bits as possible. 

In short, the entropy encoder converts from a convenient form of the data to an efficient form of the same data.

After some experimentation I've settled on the [rANS](https://github.com/TradeIdeasPhilip/compress/blob/master/rans64.h) entropy encoder.
Its results are as good as theoretical maximum.
And I've added a little code on top to make it very easy to use.

## Entropy decoder

The decoder will take the output from the encoder and convert it back into the original values, e.g.
* 12,345
* 8,765
* Yes
* GIF
* #ff0000

Note that the compressed version of the data only contains the field values, e.g. 12,345.
The field names, e.g. user id, are not stored.
The program doing the encoding and the program doing the decoding need to agree on these in advance.

For example, let's define the phil-fun-test file format (`*.pft`) to contain the following fields, in order.

| Field name | Possible values |
| ------------- | ------------------ | 
| user id              | 0 - 2,147,483,647 |
| session id              | 0 - 65,535 |
| keep backup              | yes, no |
| file format              | JPG, PNG, GIF, TIFF, BMP  |
| background color             | 0 - 16,777,215 |

Of course, the file format can be as simple or as complicated as you like.
The `count` and `uncount` programs look at a file as a sequence of bytes, without knowing or caring what those bytes mean.

## Optimizing based on frequency

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

## Convenience code

I've written a lot of code that will help multiple compression programs.
Among other things, I've made the rANS library easier to use.

One example is that the rANS encoder expects all frequency data to be expressed as a fraction where the denominator is a power of two.
I've found it much more convenient for the main program to use fractions with any denominator, so my library code does the conversion automatically.

If you say that you have and item with probability 1/3, the library will change that to something like  357,913,94<b>1</b>/1,073,741,824 or 357,913,94<b>2</b>/1,073,741,824.
The libraries on the encoder side and the decoder side will do this consistently.

Another example:  The rANS encoder spits out a stream of bits.
That's not convenient.
So I wrote a wrapper around the encoder which will write the output to a file.
I also wrote a corresponding library to read from that file and send the bits to the rANS decoder in a format that it likes.
Those two libraries share a lot of little details but you don't have to worry about that.

## Main program

I wrote the simplest possible main program make use of the rANS encoder/decoder and my convenience libraries.

`count` starts by reading the entire input file.
It counts the number of times it sees each byte in the file.
For a super simple example, let's say the input file contains "abbcccdddd" and nothing else.
So we'll create a table like this:
| Byte | Frequency |
| :----: | :---------: |
| a | 1/10 |
| b | 2/10 |
| c | 3/10 |
| d | 4/10 |
| everything else | 0/10

Then `count` will make a second pass through the input file, one byte at a time.
It will send each byte to the encoder.
And it will use the table above to encode each byte.

There's one more thing.
`uncount` needs access to the frequency table that `count` created.
So, before sending the individual bytes to the encoder, `count` needs to send the contents of this table to the compressed file.
And that's all there is!

If you run `count` on a file of English text, it should compress that into a smaller file.
If you run `count` on a file of random garbage, the output might actually be bigger than the input.
Either way, you can run `uncount` on the compressed file to restore the original file.

## Actual file format

This is the actual data that `count` writes to the rANS encoder and `uncount` reads from the rANS decoder.
I.e. this is what's in the compressed file.

| Field name | Possible values |
| ------------- | ------------------ | 
| file length   | 0 - 100,000,000, all equally likely |
| number of times we saw byte 0 in the file  | 0 - the number of bytes in the file, all equally likely |
| number of times we saw byte 1 in the file   | 0 - the number of bytes _remaining_ in the file, all equally likely |
| number of times we saw byte n in the file, for n = 2 to 254 | 0 - the number of bytes _remaining_ in the file, all equally likely |
| number of times we saw byte 255 in the file | 0 - the number of bytes _remaining_ in the file, all equally likely |
| the value of the first byte in the file | 0 - 255, use the header to determine the frequencies. |
| the value of the second byte in the file | 0 - 255, use the header to determine the frequencies. |
| ... | ... |
| the value of the last byte in the file | 0 - 255, use the header to determine the frequencies. |

Notes
* I set the maximum file length to 100,000,000 somewhat arbitrarily.  It was convenient and this program is just an example.
* The length of the file will become the denominator when encoding the body of the file.
* The number of times we saw byte n will become the numerator each time try to encode a byte with value n.
* Many of "number of times we saw..." fields will have a range of 0 to 0.  This costs 0 bits to encode.


## Old code

This project only uses the newest and best version of each bit of shared code.
Some of my previous projects did things slightly differently.
Those typically lead to the new and improved versions demonstrated by this project.
(Written 11/17/2022.)

## File names
Type `./count my_file.txt` to compress your file.
This will create a new file named `my_file.txt.C↓`.
Type `./uncount my_file.txt.C↓` to decompress your file.
That will create a new file named `my_file.txt.C↓.##`.  Type `diff my_file.txt my_file.txt.C↓.##` to verify that the final result is the same as the original.