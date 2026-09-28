# Design Log — Project 2

## Growth factor and amortized cost

Conversation owns a dynamic array. data_ points to the array, size_ counts used
slots, and capacity_ counts available slots. The first append allocates one
slot. When full, append doubles the capacity, transfers the messages, and deletes
the old array.

Doubling gives capacities of 1, 2, 4, 8, and so on. Suppose there are n appends.
The number of existing messages transferred during growth is a sum like
1 + 2 + 4 + ... + 2^k, with 2^k less than n. This geometric sum is less than
2n. Adding the n assignments for the newly appended messages gives fewer than
3n assignments. Creating empty slots and destroying old slots also produces
geometric sums, so the total array work is O(n). Dividing by n gives O(1)
amortized work per append. A single append can still take O(n) when growth
is needed.

This counts message operations. Copying the text of a message can take extra
time depending on its length. During growth, std::move transfers each message's
string storage instead of copying all its characters. The enum and standard
string members support nonthrowing move assignment. Doubling can leave unused
slots, but it avoids allocating a new array on every insertion. A size limit
check prevents the doubling calculation from overflowing.

## Rule of Five evidence

The destructor uses delete[] to release the array. The copy constructor creates
a separate array and copies the messages with a loop. Copy assignment does the
same thing, but must also release the destination's previous array. It checks
for self-assignment first. Both functions use try and catch around the copy
loop because copying a string can fail. If that happens, the replacement array
is deleted and the error is passed along. Copy assignment does not delete its
old array until the replacement is ready.

The move constructor copies the pointer, size, and capacity into the destination,
then resets all three source fields. This transfers ownership without copying
individual messages. Move assignment also deletes the destination's old array.
Resetting the source pointer prevents two destructors from deleting the same
array. A self-move check avoids accidentally deleting an object's own contents.

The remaining tests check separate addresses after copy construction and the
same transferred address after move construction. They also check moved-from
reuse. The dedicated assignment and self-assignment tests were removed when
the suite was reduced from 21 to 12 tests; those behaviors are implemented,
but are no longer separately tested.
append receives its Message by value, so appending an existing element remains
safe if growth replaces the array. at throws std::out_of_range for an invalid
index. There is no removal operation: the system message that Harness inserts
first remains first, even when storage grows.

## Sentinel scanner: bounded pending_ proof

The scanner follows the method in the specification. It joins pending_ with the
next chunk in a temporary string called text, then searches text for the marker.
If found, it returns only the text before the marker and remembers that the
conversation has ended. Later chunks are ignored. If not found, it returns the
safe beginning and saves a short ending for the next call.

Let m be the marker length. An empty marker is rejected, so m is at least one.
The number of saved characters is min(text.size(), m - 1). Therefore pending_
always contains at most m - 1 characters after an update. A marker crossing the
next chunk boundary can have at most m - 1 characters in the current chunk;
otherwise the entire marker would already have been found. Saving this ending
therefore preserves every possible crossing match. It may hold back ordinary
text too, which is harmless. flush releases the remaining text at stream end.

Persistent scanner storage is O(m), independent of total conversation length.
The temporary string and returned output use O(chunk length + m) space per call;
this implementation does not claim constant temporary space for arbitrary large
chunks. The four-mebibyte bytewise test checks every pending-buffer length and
compares every emitted character. Other tests cover every split point, ordinary
text, false alarms, and flushing unfinished markers. Harness tests cover the
turn limit, a stop marker streamed one byte at a time, EOF, and transcript
replay. Growth and ordering share one test, which also checks the system message
stays first. Separate tests for custom overlapping markers and one-character
markers were removed.

## What I would change differently

This scanner can delay up to m - 1 ordinary characters. A later version could
save only characters that actually match the marker's beginning, reducing that
delay. For now, keeping a fixed maximum tail makes the boundary argument easier
to follow. 