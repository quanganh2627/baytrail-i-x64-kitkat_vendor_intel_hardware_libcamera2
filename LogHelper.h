/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LOGHELPER_H_
#define LOGHELPER_H_


#include <utils/Log.h>
#include <cutils/atomic.h>
#include <utils/KeyedVector.h>
#include <utils/String8.h>

extern int32_t gLogLevel;

static void setLogLevel(int level) {
    android_atomic_write(level, &gLogLevel);
}

enum  {

    CAMERA_DEBUG_LOG_LEVEL1 = 1,
    CAMERA_DEBUG_LOG_LEVEL2 = 2,

    // bitmask of debug features
    // -------------------------

    /* Emit well-formed performance traces */
    CAMERA_DEBUG_LOG_PERF_TRACES = 1<<7,

    /* Print out detailed timing analysis */
    CAMERA_DEBUG_LOG_PERF_TRACES_BREAKDOWN = 1<<8,

    /* following 5 need to be used by CameraDump module */
    CAMERA_DEBUG_DUMP_RAW = 1<<2,
    CAMERA_DEBUG_DUMP_YUV = 1<<3,
    CAMERA_DEBUG_DUMP_PREVIEW = 1<<4,
    CAMERA_DEBUG_DUMP_VIDEO = 1<<5,
    CAMERA_DEBUG_DUMP_SNAPSHOT = 1<<6,

    CAMERA_DEBUG_DUMP_3A_STATISTICS = 1<<9,
    CAMERA_DEBUG_ULL_DUMP = 1<<10,
    CAMERA_DEBUG_JPEG_DUMP = 1<<11
};

#define LOG1(...) LOGD_IF(gLogLevel & CAMERA_DEBUG_LOG_LEVEL1, __VA_ARGS__);
#define LOG2(...) LOGD_IF(gLogLevel & CAMERA_DEBUG_LOG_LEVEL2, __VA_ARGS__);

namespace android {

class CameraParamsLogger {

public:
    CameraParamsLogger(const char * params);
    ~CameraParamsLogger();

    void dump() const;
    void dumpDifference(const CameraParamsLogger &otherParams) const;

private:
    int splitParam(String8 &inParam , String8  &aKey, String8 &aValue);
    void fillMap(KeyedVector<String8,String8> &aMap, String8 &aString);

private:
    String8                         mString;
    KeyedVector<String8,String8>    mPropMap;

    static const char ParamsDelimiter[];
    static const char ValueDelimiter[];

};

namespace LogHelper {

/**
 * Runtime selection of debugging level.
 */
void setDebugLevel(void);

} // namespace LogHelper
} // namespace android;
#endif /* LOGHELPER_H_ */
