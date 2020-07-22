#pragma once

#if defined(GENMC_LOG)
#define genmc_log(...) printf(__VA_ARGS__)
#else
#define genmc_log(...)
#endif
