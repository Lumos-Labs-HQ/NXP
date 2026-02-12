/*
 * NXP Logging System
 *
 * Compile-time log level filtering with C23 __VA_OPT__.
 */
#ifndef NXP_LOG_H
#define NXP_LOG_H

#include <stdarg.h>
#include <stdio.h>

typedef enum nxp_log_level {
    NXP_LOG_TRACE = 0,
    NXP_LOG_DEBUG = 1,
    NXP_LOG_INFO  = 2,
    NXP_LOG_WARN  = 3,
    NXP_LOG_ERROR = 4,
    NXP_LOG_FATAL = 5,
    NXP_LOG_NONE  = 6,
} nxp_log_level;

/* Compile-time minimum level (everything below is stripped) */
#ifndef NXP_LOG_MIN_LEVEL
    #ifdef NXP_DEBUG
        #define NXP_LOG_MIN_LEVEL NXP_LOG_TRACE
    #else
        #define NXP_LOG_MIN_LEVEL NXP_LOG_INFO
    #endif
#endif

/* User-installable log callback */
typedef void (*nxp_log_fn)(nxp_log_level level, const char *file, int line,
                           const char *fmt, va_list args);

void nxp_log_set_callback(nxp_log_fn fn);
void nxp_log_set_level(nxp_log_level level);

/* Internal log function - do not call directly */
void nxp_log_impl(nxp_log_level level, const char *file, int line,
                  const char *fmt, ...);

/* Log macros with C23 __VA_OPT__ for clean zero-arg support */
#define NXP_LOG(level, fmt, ...) \
    do { \
        if ((level) >= NXP_LOG_MIN_LEVEL) { \
            nxp_log_impl((level), __FILE__, __LINE__, \
                         fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)

#define NXP_TRACE(fmt, ...) NXP_LOG(NXP_LOG_TRACE, fmt __VA_OPT__(,) __VA_ARGS__)
#define NXP_LOG_DBG(fmt, ...) NXP_LOG(NXP_LOG_DEBUG, fmt __VA_OPT__(,) __VA_ARGS__)
#define NXP_INFO(fmt, ...)  NXP_LOG(NXP_LOG_INFO,  fmt __VA_OPT__(,) __VA_ARGS__)
#define NXP_WARN(fmt, ...)  NXP_LOG(NXP_LOG_WARN,  fmt __VA_OPT__(,) __VA_ARGS__)
#define NXP_ERR(fmt, ...)   NXP_LOG(NXP_LOG_ERROR, fmt __VA_OPT__(,) __VA_ARGS__)
#define NXP_FATAL(fmt, ...) NXP_LOG(NXP_LOG_FATAL, fmt __VA_OPT__(,) __VA_ARGS__)

#endif /* NXP_LOG_H */
