# My Malloc
In this project, a set of memory allocation functions is implemented:

1. `void *malloc(size_t size)`
2. `void free(void *ptr)`
3. `void *calloc(size_t nitems, size_t size)`
4. `void *realloc(void *ptr, size_t size)`

## Usage
After `make`, a shared library called `libmymalloc.so` will be generated. The library should be preloaded in existing applications to perform the custom memory management operations above.

## Features

1. The payload pointers returned by `malloc` is 8-bytes aligned.
2. The implemented allocation strategy can reuse the already freed blocks and split or merge the free blocks, when necessary.
3. The allocator is thread-safe and be able to scale as the number of application threads that perform memory operations increases.

## References:
1. [Valgrind](https://valgrind.org/)
2. [AddressSanitizer: A Fast Address Sanity Checker](https://www.usenix.org/system/files/conference/atc12/atc12-final39.pdf)
3. [Hoard: A Scalable Memory Allocator for Multithreaded Applications](https://people.cs.umass.edu/~emery/pubs/berger-asplos2000.pdf)
4. [MemorySanitizer: fast detector of C uninitialized memory use in C++](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/43308.pdf)
5. [ThreadSanitizer – data race detection in practice](https://static.googleusercontent.com/media/research.google.com/el//pubs/archive/35604.pdf)
6. [Memory Allocation for Long-Running Server Applications](https://citeseerx.ist.psu.edu/pdf/e41ad0406628edf82712d16cf4c6d7e486f26f9f)