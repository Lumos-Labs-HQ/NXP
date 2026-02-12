/*
 * NXP Logging System - Implementation
 */
#include "log.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

static nxp_log_fn   g_log_callback = nullptr;
static nxp_log_level g_log_level   = NXP_LOG_MIN_LEVEL;

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

void nxp_log_set_callback(nxp_log_fn fn) {
    g_log_callback = fn;
}

void nxp_log_set_level(nxp_log_level level) {
    g_log_level = level;
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *last = slash > bslash ? slash : bslash;
    return last != nullptr ? last + 1 : path;
}

void nxp_log_impl(nxp_log_level level, const char *file, int line,
                  const char *fmt, ...) {
    if (level < g_log_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    if (g_log_callback != nullptr) {
        g_log_callback(level, file, line, fmt, args);
    } else {
        /* Default: print to stderr */
        const char *name = basename_of(file);
        int idx = (int)level;
        if (idx < 0 || idx > 5) idx = 5;

        fprintf(stderr, "[NXP %s] %s:%d: ", level_strings[idx], name, line);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    va_end(args);
}
