/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG logTag

#include "LogHelper.h"
#include <utils/threads.h>

namespace android {

KeyedVector<unsigned long, size_t> LogHelper::tracePosMap;

LogHelper::LogHelper(short level, const char* tag, const char* entry) {
    logLevel = level;
    strncpy(entryName, entry, sizeof(entryName));
    strncpy(logTag, tag, sizeof(logTag));
    GetThreadId();
    DoLog("[%s]%s> %s", sTid, ForwardSpaces(), entryName);
    // Advance the indent with 1 position for inside function entries
    size_t tracePos = GetTracePos();
    spaces[tracePos] = SPACE_TRACE_CHAR;
    spaces[tracePos + 1] = '\0';
}

LogHelper::~LogHelper() {
    DoLog("[%s]%s< %s", sTid, BackSpaces(), entryName);
}

char* LogHelper::GetThreadId() {
    memset(sTid, 0, sizeof(sTid));
    android_thread_id_t tid = androidGetThreadId();
    this->tid = (unsigned long)tid;
    unsigned char *ptc = (unsigned char*)tid;
    char* s = &sTid[0];
    for (size_t i = 0; i < sizeof(tid); i++) {
        sprintf(s++, "%02x", (unsigned)(ptc[i]));
    }
    return &sTid[0];
}

size_t LogHelper::GetTracePos() {
    ssize_t idx = tracePosMap.indexOfKey(tid);
    size_t tracePos = 0;
    if (idx >= 0) {
        tracePos = tracePosMap.valueFor(tid);
    } else {
        tracePosMap.add(tid, tracePos);
    }
     return tracePos;
}

char* LogHelper::ForwardSpaces() {
    memset(spaces, SPACE_TRACE_CHAR, sizeof(spaces));
    size_t tracePos = GetTracePos();
    spaces[tracePos++] = '\0';
    if (tracePos > sizeof(spaces) - 1)
        tracePos = sizeof(spaces) - 1;
    tracePosMap.replaceValueFor(tid, tracePos);
    return &spaces[0];
}

char* LogHelper::BackSpaces() {
    unsigned long tid = (unsigned long)androidGetThreadId();
    memset(spaces, SPACE_TRACE_CHAR, sizeof(spaces));
    size_t tracePos = GetTracePos();
    if (tracePos < 1)
        tracePos = 1;
    spaces[--tracePos] = '\0';
    tracePosMap.replaceValueFor(tid, tracePos);
    return &spaces[0];
}

void LogHelper::DoLog(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    if (logLevel > 1) {
        LOG2("%s", tmpMsg);
    } else {
        LOG1("%s", tmpMsg);
    }
}

void LogHelper::Details(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    DoLog("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

void LogHelper::Details1(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    LOG1("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

void LogHelper::Details2(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    LOG2("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

void LogHelper::Error(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    LOGE("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

void LogHelper::Information(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    LOGI("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

void LogHelper::Warning(const char* format, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int len = strlen(format) + 2048;
    char* tmpMsg = (char*)calloc(len, sizeof(char));
    vsnprintf(tmpMsg, len, format, arg_ptr);
    LOGW("[%s]%s @%s: %s", sTid, spaces, entryName, tmpMsg);
    free(tmpMsg);
}

}; // namespace android
