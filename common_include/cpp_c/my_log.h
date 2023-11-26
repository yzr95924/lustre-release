/**
 * @file my_log.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief my log implement
 * @version 0.1
 * @date 2023-05-14
 *
 * @copyright Copyright (c) 2023
 *
 */

#ifndef MY_LOG_H
#define MY_LOG_H

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef enum {
    NO_LOG_LEVEL = 0,
    ERROR_LOG_LEVEL = 1,
    INFO_LOG_LEVEL = 2,
    DEBUG_LOG_LEVEL = 3
} LOG_LEVEL_TYPE_T;

#ifndef LOG_LEVEL
#define LOG_LEVEL DEBUG_LOG_LEVEL
#endif

#define LOG_TIME_BUF_SIZE (64)

#define _FILE strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__
#define LOG_FMT "[%s][%s][%s][%s:%d:%s]: %s"
#define LOG_ARGS(TIME_BUF, LOG_TAG, MODULE_NAME, FILE_NAME, CUR_LINE_NUM, FUNC_NAME, MSG) \
    TIME_BUF, LOG_TAG, MODULE_NAME, FILE_NAME, CUR_LINE_NUM, FUNC_NAME, MSG
#define ERROR_LOG_TAG "ERROR"
#define INFO_LOG_TAG "INFO"
#define DEBUG_LOG_TAG "DEBUG"

#define ZUORU_LOGGING(log_type, message, args...) \
    Logging(log_type, MODULE_ID, _FILE, __LINE__, __FUNCTION__, \
    message, ##args);

static inline void TimeNow(char *input_buf)
{
    time_t raw_time;
    struct tm *time_info;
    struct tm tmp_time;

    time(&raw_time);
    time_info = localtime_r(&raw_time, &tmp_time);

    strftime(input_buf, LOG_TIME_BUF_SIZE, "%Y-%m-%d %H:%M:%S", time_info);
    return;
}

static inline void Logging(LOG_LEVEL_TYPE_T log_type, const char *log_module,
    const char *file_name, int cur_line_num, const char* func_name,
    const char *fmt, ...)
{
    char input_time_buf[LOG_TIME_BUF_SIZE] = { 0 };
    char msg_buf[BUFSIZ] = { 0 };
    if (log_type > LOG_LEVEL) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, BUFSIZ, fmt, ap);
    va_end(ap);

    TimeNow(input_time_buf);
    switch (log_type) {
        case NO_LOG_LEVEL:
            return;
        case ERROR_LOG_LEVEL:
            fprintf(stderr, LOG_FMT, LOG_ARGS(ERROR_LOG_TAG, input_time_buf, log_module,
                file_name, cur_line_num, func_name, msg_buf));
            break;
        case INFO_LOG_LEVEL:
            fprintf(stderr, LOG_FMT, LOG_ARGS(INFO_LOG_TAG, input_time_buf, log_module,
                file_name, cur_line_num, func_name, msg_buf));
            break;
        case DEBUG_LOG_LEVEL:
            fprintf(stderr, LOG_FMT, LOG_ARGS(DEBUG_LOG_TAG, input_time_buf, log_module,
                file_name, cur_line_num, func_name, msg_buf));
            break;
    }
    return;
}

#endif
