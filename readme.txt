     This serves as a lightweight store, backed by disk.  It can store arbitrary objects, provided those obejects provide a serialize function to convert
the object to a character array and size, and a constructor that takes that character array and size to reconstruct the original object.  Compression is
optional; this should be chosen based on the actual data to store.  For now, a oy "compression" routine is included for test as a placeholder (just
reverse the order of the bytes).
     The store does reads and writes under lock, insuring that writes are entered into the store and persisted to file before they are available for read.
Thus, all threads get the data at the same time.  When the store is started, it will load itself from the most recent persisted file, or from the file
passed if one is given.  hile dat in the store is compressed, data stored to the file is not: this makes it easier to switch compression algorithms,
or use a persisted file from a version with an unsupported compression algorithm.
     The persisted file is a log -- whenever a new write is done, the key and value are appended to the file.  While this is simple and fast, it leads
to persisted files that are much larger than they need to be if the same keys are repeatedly written.  This could be mitigated somewhat if we expect to
be writing the same value to a key repeatedly -- just don't persist if the value hasn't changed -- but this usage pattern does not seem to be frequent
enough (outside test) to make doing so worthwhile.  As it is, the file will be large.  Potentially very large.  When the store starts up, it begins a new
persisted file with just the data it has read from the previous file; the new file will only contain the values in the store initially as a consequence.
This idea could be used to prune the persisted file from time to time: flush the store contents to a new persisted file, replace the old with an empty file
as a placeholder (so the system can determine the latest version -- it just looks for files titled "Persisted.#" and counts until it can't open one) and
moving on with the new persisted file.  Up side: much smaller file inthe case of multiple writes to the same keys.  Down side: flushing the data takes
up resources (need to pause writes until the flush is complete), and we lose the write history inherent in the persisted file chain.  The flushing could
even be done automatically, if desired: for instance, keep track of how many writes to the file have been done; when this number reaches some preset
multiple of the store size (10, perhaps), deem the file "too large" and flush.