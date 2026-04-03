#ifndef LIBUPDATE_TEST_LOG_H
#define LIBUPDATE_TEST_LOG_H

#include <stdio.h>

#if defined(LIBUPDATE_TEST_VERBOSE)
#define LIBUPDATE_TEST_LOG(...) fprintf(stderr, "[libupdate_test] " __VA_ARGS__)
#else
#define LIBUPDATE_TEST_LOG(...) ((void)0)
#endif

#endif
