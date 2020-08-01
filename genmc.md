# GenMC notes

- memcpy / memset - not supported

- Unions and in general mixed-size accesses don't work. As a consequence - all need to be allocated on stack / as global variable (i.e. I can't malloc sizeof(some_struct) and then interpret it as some_struct).

- Intrinsics (`llvm.objectsize.i64.p0i8`, `llvm.umul.with.overflow.i64`)

- mmap - not supported. Can be replaced with malloc though.

- Malloc - very inefficient (per-byte for loop on allocation)

- I think error traces can be more understandable. Now it's a bunch of source code lines representing memory accesses, and sometimes one line is repeated several times (just sequential calls?). Not intuitive for me. And with only one line per step it's hard to understand where exactly in the code the function was called - I'd expect to see not just one source code line representing access, but a stacktrace of this access (e.g. if A calls C and B calls C, in trace I see only line with C and I can't understand who is the caller - A or B).

- When you accidentally type non-existing file to check with genmc, it goes crazy and shows you random symbols in infinite loop.

- When a violation is found, sometimes genmc, as I understand, tries to replay the graph and it gets stuck in an infinite cycle.

- Endless loops - any way to detect them? At least terminate on timeout? (there is unroll, but still when I don't use this option it's unclear why model checking is stuck - good to know if it is because long loop)

- I disabled `LLVM_HAS_GLOBALOBJECT_GET_METADATA` - because it was unable to get size info about some type (`Assertion failed: (Ty->isSized() && "Cannot getTypeInfo() on a type that is unsized!"), function getTypeSizeInBits, file /usr/local/Cellar/llvm@9/9.0.1_2/include/llvm/IR/DataLayout.h, line 602.`)

- `__attribute__((constructor))` - supported or not?

- `strlen` not supported

- `pthread_key_create` not supported?
