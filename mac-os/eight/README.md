# eight
A compression program compiled for the mac.

The original version of this ([../../\*ght\*](https://github.com/TradeIdeasPhilip/compress/search?q=filename%3Aeight)) did a good job guessing the next byte in the file, one byte at a time.
My plan is to add sliding buffer style compression in addition to this.

According to my notes, `uneight` will crash sometimes.
That is not yet repeatable.

Currently `eight` does a decent job of compressing files, often better than `gzip -9`.

# count and uncount

The `count` and `uncount` programs have been moved to their own folder, `../count`.