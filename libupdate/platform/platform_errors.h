#ifndef PLATFORM_ERRORS_H
#define PLATFORM_ERRORS_H

/* Shared status codes for platform_process / platform_fs (0 = success). */
#define PLATFORM_OK 0

#define PLATFORM_ERR_INVALID_ARG -1
#define PLATFORM_ERR_IO -2
#define PLATFORM_ERR_NOT_FOUND -3
#define PLATFORM_ERR_EXISTS -4
#define PLATFORM_ERR_ACCESS -5
#define PLATFORM_ERR_NOT_EMPTY -6
#define PLATFORM_ERR_NO_MEM -7
#define PLATFORM_ERR_LIMIT -8
#define PLATFORM_ERR_STATE -9
#define PLATFORM_ERR_UNSUPPORTED -10
#define PLATFORM_ERR_UNKNOWN -99

#endif /* PLATFORM_ERRORS_H */
