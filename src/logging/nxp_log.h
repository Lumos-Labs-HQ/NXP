/*
 * NXP Logging - C API Header
 * Wrapper around Quill C++ logging library
 */
#ifndef NXP_LOG_H
#define NXP_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NXP_LOG_TRACE = 0,
    NXP_LOG_DEBUG = 1,
    NXP_LOG_INFO  = 2,
    NXP_LOG_WARN  = 3,
    NXP_LOG_ERROR = 4,
    NXP_LOG_FATAL = 5
} nxp_log_level;

/* Initialize logging system - log_file parameter ignored (console only) */
void nxp_log_init(const char *log_file, nxp_log_level min_level);

/* Shutdown logging */
void nxp_log_shutdown(void);

/* Log functions */
void nxp_log_trace(const char *fmt, ...);
void nxp_log_debug(const char *fmt, ...);
void nxp_log_info(const char *fmt, ...);
void nxp_log_warn(const char *fmt, ...);
void nxp_log_error(const char *fmt, ...);
void nxp_log_fatal(const char *fmt, ...);

/* Conditional logging macros */
#ifdef NXP_ENABLE_LOGGING
    #define NXP_LOG_TRACE(...) nxp_log_trace(__VA_ARGS__)
    #define NXP_LOG_DEBUG(...) nxp_log_debug(__VA_ARGS__)
    #define NXP_LOG_INFO(...)  nxp_log_info(__VA_ARGS__)
    #define NXP_LOG_WARN(...)  nxp_log_warn(__VA_ARGS__)
    #define NXP_LOG_ERROR(...) nxp_log_error(__VA_ARGS__)
    #define NXP_LOG_FATAL(...) nxp_log_fatal(__VA_ARGS__)
#else
    #define NXP_LOG_TRACE(...) ((void)0)
    #define NXP_LOG_DEBUG(...) ((void)0)
    #define NXP_LOG_INFO(...)  ((void)0)
    #define NXP_LOG_WARN(...)  ((void)0)
    #define NXP_LOG_ERROR(...) ((void)0)
    #define NXP_LOG_FATAL(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* NXP_LOG_H */
