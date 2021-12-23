#ifndef __LzBlockShared_h_
#define __LzBlockShared_h_

#include <unordered_map>
#include <unordered_set>

#include "RansHelper.h"


class WriteStats
{
private:
  static size_t group(size_t length) { return length; }
  std::unordered_map< size_t, BoolCounter > _counters;
public:
  void clear() { _counters.clear(); }
  void reduceOld()
  {
    for (auto &kvp : _counters)
      kvp.second.reduceOld();
  }
  RansRange getRange(size_t length, bool value)
  {
    return _counters[length].getRange(value);
  }
  bool readValue(size_t length, Rans64State* r, uint32_t** pptr)
  {
    return _counters[length].readValue(r, pptr);
  }
  void increment(size_t length, bool value)
  {
    _counters[length].increment(value);
  }
};

/* MruBase encapsulates a lot of the magic that is specific to this compression
 * format.  At its heart this is a list of strings.  They can be indexed from
 * 0, like an array or vector.  In fact, getAll() shows you a copy of the
 * underlying vector.  We constantly rearrange this list so the most recently
 * used items are closest to 0.  For most data, this means that lower indices
 * are much more common than higher ones, so if you have a list of indices
 * pointing into this table, you can compress that list with rANS, an entropy
 * encoder.
 *
 *                 --> Encode / Compress -->
 * Original file <--> MruList <--> rANS <--> compressed file.
 *                <-- Decode / Decompress <--
 *
 * At a high level compression looks like this.  1) Look at the next part of
 * the file you want to compress.  2) Find the longest string in this list
 * which is a prefix of what you want to compress.  3) Grab the index of that
 * string, and pass it to the rANS entropy encoder.
 *
 * Ideally, each time you do these steps you will grab a string that is several
 * bytes long.  You can represent all of those bytes with a single number,
 * typically between 0 and 2k, or thereabouts.  That's where we typically get
 * the bulk of our compression.  Then we use rANS to convert that number into
 * a series of bits.  Often the most common numbers will produce about 3 bits,
 * and a significant number of these numbers will produce less than a byte
 * each.  Worst case this table will match a single byte from the table, and
 * the rANS encoder will take more than 8 bits of output to encode a single
 * byte of input, but we always make progress.
 *
 * There are also options to add strings to the table and to delete strings
 * from the table.  These are also restricted to focus on the most recently
 * used items, so our list of instructions can easily be compressed by the
 * entropy encoder.  The main program looks ahead in the file to decide which
 * strings are best to add or delete.
 *
 * If you add strings faster than you delete them, eventually this table will
 * get full, and we will automatically delete the least recently used strings.
 * See the comments for addToFront() for more details.
 *
 * You can configure the size of this list.  If you make it too small, you
 * limit the amount of compression you can get from this stage.  If you make
 * this too big you get diminishing returns.  Operations on this list, and
 * this corresponding list of frequencies used by the rANS step, are often
 * O(n) where n is either the position of the string in the list, or the
 * maximum size of the list.  There are constant attempts to optimize these
 * data structures, but you can't completely get rid of that O(n), so you want
 * to keep the table small so n will be small. */
template < class T >
class MruBase 
{
private:
  const size_t _maxRecycle;
  size_t _size;
  std::vector< T > _items;

  // Invariant:  This table always contains all 256 one byte strings.  They
  // can never be deleted.  Unfortunately we need a little help from outside
  // to guarantee that.  In particular, we rely on the main program to insert
  // exactly the right items.  But we do check canDelete().  If the main
  // program explicitly asks us to delete an undeletable item, that fails an
  // assertion.  If this class tries to automatically delete an item, we
  // explicitly choose an item that we are allowed to delete.

  // These items all make a lot of assumptions about T.  I've been a little
  // vague on the specific assumptions, especially while the code is still
  // changing so fast.  At least I've listed out the interesting cases here.
  static bool canDelete(T const &string) { return string.length() > 1; }
  static bool equal(T const &a, T const &b) { return a == b; }
  typedef typename std::unordered_set< T > Set;

  // Move one item from index i to index 0.  All the items between 0 and i
  // get moved one index higher to make room.  Every index above i remains
  // unchanged.
  void promote(size_t i)
  { // This should never fail, even with a bad input file.  We tell rANS how
    // big a number we expect, based on the current size() of this table.
    assert(i < _size);
    std::rotate(_items.begin(), _items.begin() + i, _items.begin() + i + 1);
  }

  // If there are too many items in the recycle bin, remove the extras.
  void trimRecycleBin()
  {
    auto const recyclerSize = _items.size() - _size;
    if (recyclerSize > _maxRecycle)
    { 
      _items.resize(_size + _maxRecycle);
    }
  }
  
public:

  // Initially this list is empty.  The caller is responsible for adding the
  // initial contents.  This is required because the data type is a template
  // and we don't know how to create our own strings.  Remember the invariant,
  // we need all 256 one byte strings.  Potentially you could preload this
  // with other interesting strings, as long as the encoder and decoder start
  // with the same lists.
  MruBase(size_t maxRecycle) :
    _maxRecycle(maxRecycle), _size(0) { }

  // The number of items currently in the list.
  // Should be >= 256.
  size_t size() const { return _size; }

  // findAndPromote() tells us how to encode an item.  Other parts of the code
  // tell us which string we should try to encode.  (E.g. do we only grab the
  // first byte from the file, or the first 50 bytes?)  This will return a
  // number, which we will send to the rANS encoder and eventually to the
  // compressed file.  This will also move the string to the front of the list,
  // index 0.  If you call findAndPromote() twice in a row on the same string,
  // the second call will always return 0.
  //
  // NOT_FOUND means that the string you asked for wasn't in the table.
  static const size_t NOT_FOUND = (size_t)-1;
  size_t findAndPromote(T const &item)
  {
    for (size_t i = 0; i < _size; i++)
      if (equal(item, _items[i]))
      {
	promote(i);
	return i;
      }
    return NOT_FOUND;
  }

  // This is the item we just found with findAndPromote().  The encoder
  // sometimes prints this when doing verbose debugging.  The decoder uses this
  // to grab the actual bytes we need to copy to the output.
  T const &getFront() const { return _items[0]; }

  // This is used by the decoder.  When the encoder called findAndPromote() on
  // a string to get a number, then the decoder calls findAndPromote() on its
  // own copy of this table, that should convert the number back to the
  // original string.  Both versions of findAndPromote() will move the string
  // to the front of the list, so they stay synchronized with each other.
  T const &findAndPromote(size_t index)
  {
    promote(index);
    return getFront();
  }

  // This is aimed at a very specific case.  Normally you are not worried about
  // entering a duplicate string into this list.  If "Pi", "zza", and "Pizza"
  // were already in the list, and you want to write the compress the word
  // "Pizza", you'd grab that word as a whole from the list.  Clearly you
  // wouldn't try to add the word because you just saw that the word was
  // already in the list.  If you were in this position and you chose to split
  // the word pizza into two pieces, after you printed the second half, that's
  // the only way you could get into a position where you'd consider adding
  // the word back to the list.  But there's no reason you'd split up the word.
  // But there is one tricky case where it might look reasonable to add a
  // string to the list, but it's already there.
  //
  // Start fresh with only the minimum items in the table.  Try to encode the
  // string "AAA".
  // 1) Grab "A" from the list.  "A" moves to index 0.
  // 2) Grab "A" from the list.  "A" stays at index 0.
  // 3) Add "AA" to the list.  "AA" is at index 0, "A" is at index 1.
  // 4) Grab "A" from the list.  "A" moves to index 0, "AA" move to index 1.
  // 5) Add "AA" to the list.  "AA", "A", and "AA" at 0, 1, and 2.  BAD!
  // We can fix this by checking if (!mru.isRecentDuplicate("AA")) before
  // trying to add "AA".  Normally after each grab the encoder will use its
  // look ahead to decide whether or not to do the add, then it will send a
  // Boolean to the compressed file so the decoder knows if it should do the
  // add or not.  However, there are two cases where we don't need any
  // look ahead or anything added to the compressed file.  In both cases the
  // compressor and the decompressor already have the data to make this
  // decision, so we don't bother to send a copy of the decision.  Right after
  // the first grab it's the same as !isRecntDuplicate(), in both cases we
  // never add a string to the list.  In all other cases the encoder makes a
  // decision and prints it to the file.
  // 
  // Remember, we are going through this tortured logic to avoid always
  // checking every new addition to see if it's already somewhere in this list.
  // That could be expensive at run time.
  //
  // Also, we really don't want a duplicate in the table.  That would hurt
  // compression.  If the compressor sees a duplicate, what does it do?  Of
  // course it does NOT add the duplicate.  Normally at this point it would
  // write a Boolean value to the stream to tell the decompressor that it chose
  // not to add a new string at this time.  But since both the encoder and the
  // decoder have access to this information, we don't really have to send that
  // bit.
  //
  // Input to compress:  ABABAB
  // MRU, starting from 0: AB ...
  // 1) Grab AB. MRU state unchanged.
  // 2) Grab AB. MRU state unchanged.
  // 3) Add ABAB.  MRU: ABAB AB ...
  // 4) Grab AB. MRU: AB ABAB ... The possible duplicate is at index 1.
  // 5) Delete AB.  MRU:  ABAB ... The possible duplicate is at index 0.
  // 6) Think about adding ABAB, but notice that it would be a duplicate.
  // Unfortunately it's not always convenient to check this value right away.
  // The main program needs to do the delete before the add.  And the
  // compressed file will include the delete before the add.
  // isRecentDuplicate() refers to the add.  Set recentDelete to true if you
  // did step 5 after grab, before calling this.  Set recentDelete to true if
  // you just did the grab and you have not called delete since then.
  bool isRecentDuplicate(T const &item, bool recentDelete) const
  {
    return equal(item, _items[recentDelete?0:1]);
  }

  // Add a new string to the table.  Its index will initially be 0.  All other
  // entries in the table will have their index incremented by 1 to make room.
  //
  // Do not add any duplicates to the table!  We do not enforce that invariant
  // here because it would be way too inefficient.  However, any strategy that
  // led you to put duplicates in the table would not give you optimal
  // compression.  If there is a duplicate, that's a programming error.
  void addToFront(T const &toAdd)
  {
    _items.insert(_items.begin(), toAdd);
    _size++;
  }

  // Removes the item at index 0, the front of the list.  All other items
  // shift one position toward the front.
  //
  // You can only delete from the front of the list.  (And you can only add to
  // the front of the list.)  That makes the instructions cheap and easy to
  // encode.  Effectively is it saying "now" so it doesn't need any arguments.
  // The encoder uses the same look ahead process to say which strings we will
  // want to save for future use, and which strings have been used for the last
  // time so we don't need them any more.
  //
  // If there is space, deleted items are moved to a recycle bin.  The most
  // recently deleted item will be at the front of the recycle bin, and older
  // items will eventuall fall off as we run out of space.
  //
  // Typically we look ahead at a whole block at once.  We copy a string into
  // this table if we plan to use it a second time in the same block.  We
  // delete a string if we don't need it any more for this block.  There is no
  // reason to reach into the recycle bin in the middle of the block.  If we
  // knew we needed the string, we would not have deleted it in the first
  // place.
  //
  // However, the recycle bin can be useful for the next block.  Because we are
  // deleting items that are not used any more in this block, we'd expect to
  // end the block with only the required 256 entries, none from this file.
  // So we'd be starting almost from scratch for each block and a large file
  // wouldn't compress better than a series of small files.  So at the start
  // of each block we consider which values to restore from the previous
  // block's recycle bin.
  void deleteFront()
  {
    assert(canDelete(_items[0]));
    std::rotate(_items.begin(), _items.begin() + 1, _items.begin() + _size);
    _size--;
    trimRecycleBin();
  }

  void restoreAllFromRecycleBin()
  {
    Set present;
    std::vector< T > newList;
    auto const recycleStart = visibleEnd();

    // The items that are currently in the recycler will be moved to the
    // front of the new list.  These were used recently and are likely to be
    // used again soon.  The most recently deleted item will come first.
    for (auto it = recycleStart; it != _items.end(); it++)
    {
      const bool alreadyPresent = !present.insert(*it).second;
      if (!alreadyPresent)
      { // The recycle bin might contain duplicates.  Probably not, or at least
	// very rare.  (A lot of that depends on the main program and is beyond
	// the control of this class.)  But I don't want any duplicates in the
	// live data!
	newList.push_back(*it);
      }
    }

    // Next we copy the items that we are not allowed to delete.  We use these
    // a lot at the very beginning of the file, but not much anywhere else.
    for (auto it = _items.begin(); it < recycleStart; it++)
    {
      if (!canDelete(*it))
      {
	newList.push_back(*it);
      }
    }

    // Any remaining items in the original list were left over from the
    // previous call to restoreAllFromRecycleBin().  I.e. they were not used
    // in the previous block, but they were restored from the recycle bin
    // right before we started working on that block.  Add these last.
    for (auto it = _items.begin();
	 (it < recycleStart) && (present.size() < _maxRecycle);
	 it++)
    {
      if (canDelete(*it))
      {
	const bool alreadyPresent = !present.insert(*it).second;
	if (!alreadyPresent)
	{
	  newList.push_back(*it);
	}
      }
    }

    // Replace the old list with the new list.  Empty the recyle bin.
    _items = std::move(newList);
    _size = _items.size();
  }
  
  // The entire internal state.  getAll().begin() is the front of the list.
  // visibleEnd() is the end of the list and the beginning of the recycle bin.
  // getAll().end() is the end of the recycle bin.
  typename std::vector< T > const &getAll() { return _items; }
  typename std::vector< T >::const_iterator visibleEnd() const
  { return _items.begin() + _size; }
};

#endif
