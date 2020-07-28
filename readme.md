# Model checking mimalloc with GenMC

## Current issues

- [test/race_next](test/race_next) Data race on `block->next` variable because [cas in mimalloc](https://github.com/microsoft/mimalloc/blob/71160e2bac443c0dd35c7ee13993466efcee57b2/include/mimalloc-atomic.h#L204) has only release mo.

## Potential issues

- Non-trivial algorithm in multithread freeing (using `delayed` flags) due to full pages logic.

- Abandoned pages / segments

- Concurrent bitmap, fixed ABA
