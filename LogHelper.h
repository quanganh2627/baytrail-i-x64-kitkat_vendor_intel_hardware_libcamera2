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

#ifndef ANDROID_LOG_HELPER_H
#define ANDROID_LOG_HELPER_H

#define DEBUG_HELPER 0

#include <utils/KeyedVector.h>
#include <cutils/atomic.h>

static int32_t gLogLevel = 1;

static void setLogLevel(int level) {
    android_atomic_write(level, &gLogLevel);
}

#define LOG1(...) LOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) LOGD_IF(gLogLevel >= 2, __VA_ARGS__);
#define LOG_FUNCTION LogEntry(LOG_TAG, __FUNCTION__);
#define LOG_FUNCTION2 LogEntry2(LOG_TAG, __FUNCTION__);

#if (DEBUG_HELPER)
#define LogEntry(t, e)  LogHelper logHelper(1, t, e)
#define LogEntry2(t, e) LogHelper logHelper(2, t, e)
#define LogDetail(...)            logHelper.Details(__VA_ARGS__)
#define LogDetail1(...)           logHelper.Details1(__VA_ARGS__)
#define LogDetail2(...)           logHelper.Details2(__VA_ARGS__)
#define LogError(...)             logHelper.Error(__VA_ARGS__)
#define LogInfo(...)              logHelper.Information(__VA_ARGS__)
#define LogWarning(...)           logHelper.Warning(__VA_ARGS__)
#else
#define LogEntry(t, e)
#define LogEntry2(t, e)
#define LogDetail(...)            LOG1(__VA_ARGS__)
#define LogDetail1(...)           LOG1(__VA_ARGS__)
#define LogDetail2(...)           LOG2(__VA_ARGS__)
#define LogError(...)             LOGE(__VA_ARGS__)
#define LogInfo(...)              LOGI(__VA_ARGS__)
#define LogWarning(...)           LOGW(__VA_ARGS__)
#endif

#define SPACE_TRACE_CHAR '-'

namespace android {

class LogHelper {
    static const int MAX_LOG_TAG        = 64;
    static const int MAX_ENTRY_NAME     = 256;
    static const int MAX_SPACES         = 256;
    static const int MAX_TID            = 8;

    short logLevel;
    char logTag[MAX_LOG_TAG];
    char entryName[MAX_ENTRY_NAME];
    char spaces[MAX_SPACES];
    unsigned long tid;
    char sTid[MAX_TID];
    static KeyedVector<unsigned long, size_t> tracePosMap;

    void   DoLog(const char* format, ...);
    char*  GetThreadId();
    size_t GetTracePos();
    char*  ForwardSpaces();
    char*  BackSpaces();

public:
    LogHelper(short level, const char* tag, const char* entryName);
    ~LogHelper();

    void Details(const char* format, ...);
    void Details1(const char* format, ...);
    void Details2(const char* format, ...);
    void Error(const char* format, ...);
    void Information(const char* format, ...);
    void Warning(const char* format, ...);
};

};

#endif //ANDROID_LOG_HELPER_H
