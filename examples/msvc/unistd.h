/*
 * A <unistd.h> stand in for the nmake builds. Only the NMakefile files add
 * this directory to the include path, so the other builds still use the
 * system header.
 */

#ifndef LIBCAT_EXAMPLE_MSVC_UNISTD_H_
#define LIBCAT_EXAMPLE_MSVC_UNISTD_H_

#if !defined(_MSC_VER)

#include_next <unistd.h>

#else

#include <time.h>

#include "cat.h"

#include <windows.h>

#define _SC_NPROCESSORS_ONLN 0
#define sysconf(name) ((long) GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif
#define clock_gettime(clock_id, tp) cat_clock_gettime_monotonic(tp)

#define __builtin_bswap32(value) _byteswap_ulong(value)

#endif /* _MSC_VER */

#endif /* LIBCAT_EXAMPLE_MSVC_UNISTD_H_ */
