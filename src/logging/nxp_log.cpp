/*
 * NXP Logging - C++ Implementation using Quill
 */
#include "nxp_log.h"
#include <quill/Quill.h>
#include <cstdarg>
#include <cstdio>

static quill::Logger* g_logger = nullptr;
static nxp_log_level g_min_level = NXP_LOG_INFO;

extern "C" {

void nxp_log_init(const char *log_file, nxp_log_level min_level) {
    g_min_level = min_level;
    
    // Start Quill backend thread
    quill::start();
    
    // Always use console logger for live logging
    g_logger = quill::get_logger();
    
    // Set log level
    switch (min_level) {
        case NXP_LOG_TRACE: g_logger->set_log_level(quill::LogLevel::TraceL3); break;
        case NXP_LOG_DEBUG: g_logger->set_log_level(quill::LogLevel::Debug); break;
        case NXP_LOG_INFO:  g_logger->set_log_level(quill::LogLevel::Info); break;
        case NXP_LOG_WARN:  g_logger->set_log_level(quill::LogLevel::Warning); break;
        case NXP_LOG_ERROR: g_logger->set_log_level(quill::LogLevel::Error); break;
        case NXP_LOG_FATAL: g_logger->set_log_level(quill::LogLevel::Critical); break;
    }
}
void nxp_log_shutdown(void) {
    if (g_logger) {
        quill::flush();
        g_logger = nullptr;
    }
}

static void log_message(nxp_log_level level, const char *fmt, va_list args) {
    if (!g_logger || level < g_min_level) return;
    
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    
    switch (level) {
        case NXP_LOG_TRACE: LOG_TRACE_L3(g_logger, "{}", buffer); break;
        case NXP_LOG_DEBUG: LOG_DEBUG(g_logger, "{}", buffer); break;
        case NXP_LOG_INFO:  LOG_INFO(g_logger, "{}", buffer); break;
        case NXP_LOG_WARN:  LOG_WARNING(g_logger, "{}", buffer); break;
        case NXP_LOG_ERROR: LOG_ERROR(g_logger, "{}", buffer); break;
        case NXP_LOG_FATAL: LOG_CRITICAL(g_logger, "{}", buffer); break;
    }
}

void nxp_log_trace(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_TRACE, fmt, args);
    va_end(args);
}

void nxp_log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_DEBUG, fmt, args);
    va_end(args);
}

void nxp_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_INFO, fmt, args);
    va_end(args);
}

void nxp_log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_WARN, fmt, args);
    va_end(args);
}

void nxp_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_ERROR, fmt, args);
    va_end(args);
}

void nxp_log_fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(NXP_LOG_FATAL, fmt, args);
    va_end(args);
}

} // extern "C"
