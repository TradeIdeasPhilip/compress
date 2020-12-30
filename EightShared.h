#ifndef __EightShared_h_
#define __EightShared_h_

#include <string>


// When looking back at context, pretend like this data came at the very
// beginning of the file, before the first actual byte of the file.
// We do this mostly because it keeps the algorithm simple.
// Don't actually try to encode compress or store this data!
extern const std::string preloadContents;


#endif
